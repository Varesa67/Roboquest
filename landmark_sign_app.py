import csv
import json
import shutil
import time
import urllib.request
from collections import Counter
from pathlib import Path

import cv2
import joblib
import mediapipe as mp
import numpy as np
from mediapipe.tasks import python
from mediapipe.tasks.python import vision
from sklearn.ensemble import RandomForestClassifier

ROOT = Path(__file__).resolve().parent
DATA_PATH = ROOT / "landmark_samples.csv"
MODEL_PATH = ROOT / "landmark_classifier.joblib"
HAND_MODEL_PATH = ROOT / "models" / "hand_landmarker.task"
HAND_MODEL_URL = (
    "https://storage.googleapis.com/mediapipe-models/hand_landmarker/"
    "hand_landmarker/float16/1/hand_landmarker.task"
)
FEATURE_COUNT = 21 * 3
SAMPLE_INTERVAL_SECONDS = 0.2
MIN_SAMPLES_PER_LABEL = 10
CSV_FIELDS = ["label"] + [f"point_{index}" for index in range(FEATURE_COUNT)]
HAND_CONNECTIONS = [
    (0, 1), (1, 2), (2, 3), (3, 4),
    (0, 5), (5, 6), (6, 7), (7, 8),
    (5, 9), (9, 10), (10, 11), (11, 12),
    (9, 13), (13, 14), (14, 15), (15, 16),
    (13, 17), (17, 18), (18, 19), (19, 20),
    (0, 17),
]


def ensure_hand_model():
    if HAND_MODEL_PATH.is_file():
        return

    HAND_MODEL_PATH.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = HAND_MODEL_PATH.with_suffix(".task.download")
    print("Downloading Google's hand landmark model (one-time download)...")
    try:
        with urllib.request.urlopen(HAND_MODEL_URL, timeout=60) as response:
            with temporary_path.open("wb") as output:
                shutil.copyfileobj(response, output)
        if temporary_path.stat().st_size < 1_000_000:
            raise OSError("Downloaded model file is unexpectedly small.")
        temporary_path.replace(HAND_MODEL_PATH)
    finally:
        temporary_path.unlink(missing_ok=True)
    print(f"Hand model saved to {HAND_MODEL_PATH}")


def normalize_landmarks(landmarks, frame_width, frame_height):
    points = np.asarray(
        [
            [point.x * frame_width, point.y * frame_height, point.z * frame_width]
            for point in landmarks
        ],
        dtype=np.float32,
    )
    if points.shape != (21, 3):
        raise ValueError(f"Expected 21 three-dimensional landmarks; got {points.shape}.")

    points -= points[0]
    scale = float(np.linalg.norm(points[9, :2]))
    if scale < 1e-6:
        return None
    return (points / scale).flatten()


def ensure_dataset():
    if DATA_PATH.exists():
        with DATA_PATH.open("r", newline="", encoding="utf-8") as file:
            header = next(csv.reader(file), [])
        if header != CSV_FIELDS:
            raise ValueError(
                f"{DATA_PATH.name} has an unexpected format. "
                "Back it up, then remove it to start a fresh landmark dataset."
            )
        return

    with DATA_PATH.open("w", newline="", encoding="utf-8") as file:
        csv.writer(file).writerow(CSV_FIELDS)


def append_sample(label, features):
    with DATA_PATH.open("a", newline="", encoding="utf-8") as file:
        csv.writer(file).writerow([label, *features.tolist()])


def train_classifier():
    features = []
    labels = []
    with DATA_PATH.open("r", newline="", encoding="utf-8") as file:
        reader = csv.DictReader(file)
        if reader.fieldnames != CSV_FIELDS:
            raise ValueError(f"{DATA_PATH.name} has an invalid header.")
        for line_number, row in enumerate(reader, start=2):
            try:
                label = row["label"].strip()
                values = [float(row[field]) for field in CSV_FIELDS[1:]]
            except (TypeError, ValueError) as error:
                raise ValueError(
                    f"Invalid landmark data on CSV line {line_number}."
                ) from error
            if not label or len(values) != FEATURE_COUNT:
                raise ValueError(f"Incomplete landmark data on CSV line {line_number}.")
            if not np.isfinite(values).all():
                raise ValueError(f"Non-finite landmark data on CSV line {line_number}.")
            labels.append(label)
            features.append(values)

    counts = Counter(labels)
    if len(counts) < 2:
        print("Record at least two different labels before training.")
        return None
    too_small = {
        label: count for label, count in counts.items()
        if count < MIN_SAMPLES_PER_LABEL
    }
    if too_small:
        details = ", ".join(f"{label}: {count}" for label, count in sorted(too_small.items()))
        print(
            f"Record at least {MIN_SAMPLES_PER_LABEL} examples per label before training. "
            f"Currently: {details}"
        )
        return None

    classifier = RandomForestClassifier(
        n_estimators=200,
        class_weight="balanced",
        random_state=42,
        n_jobs=-1,
    )
    classifier.fit(np.asarray(features, dtype=np.float32), labels)
    joblib.dump(classifier, MODEL_PATH)
    print(
        f"Trained {len(counts)} labels from {len(labels)} examples. "
        f"Model saved to {MODEL_PATH.name}."
    )
    return classifier


def draw_hand(frame, landmarks):
    height, width = frame.shape[:2]
    coordinates = [
        (int(point.x * width), int(point.y * height))
        for point in landmarks
    ]
    for start, end in HAND_CONNECTIONS:
        cv2.line(frame, coordinates[start], coordinates[end], (40, 220, 40), 2)
    for x, y in coordinates:
        cv2.circle(frame, (x, y), 4, (0, 180, 255), -1)


def open_camera():
    camera = cv2.VideoCapture(0)
    if not camera.isOpened():
        camera.release()
        camera = cv2.VideoCapture(1)
    if not camera.isOpened():
        camera.release()
        raise RuntimeError(
            "Could not open a webcam. Check that it is connected and not in use."
        )
    return camera


def run_app():
    ensure_dataset()
    ensure_hand_model()
    classifier = joblib.load(MODEL_PATH) if MODEL_PATH.is_file() else None
    camera = open_camera()
    active_label = ""
    recording = False
    predicting = False
    saved_count = 0
    last_sample_time = 0.0
    last_key = -1
    last_key_time = 0.0
    previous_timestamp_ms = 0

    options = vision.HandLandmarkerOptions(
        base_options=python.BaseOptions(model_asset_path=str(HAND_MODEL_PATH)),
        running_mode=vision.RunningMode.VIDEO,
        num_hands=1,
        min_hand_detection_confidence=0.5,
        min_hand_presence_confidence=0.5,
        min_tracking_confidence=0.5,
    )
    print(
        "Keys: L=set label, C=start/stop recording, T=train, "
        "P=toggle predictions, Q=quit."
    )

    try:
        with vision.HandLandmarker.create_from_options(options) as landmarker:
            while True:
                ok, frame = camera.read()
                if not ok:
                    raise RuntimeError("Could not read a frame from the webcam.")
                frame = cv2.flip(frame, 1)
                rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb_frame)
                timestamp_ms = max(
                    previous_timestamp_ms + 1,
                    int(time.monotonic() * 1000),
                )
                previous_timestamp_ms = timestamp_ms
                result = landmarker.detect_for_video(image, timestamp_ms)

                features = None
                if result.hand_landmarks:
                    landmarks = result.hand_landmarks[0]
                    draw_hand(frame, landmarks)
                    frame_height, frame_width = frame.shape[:2]
                    features = normalize_landmarks(
                        landmarks, frame_width, frame_height
                    )
                    if recording and features is not None:
                        now = time.monotonic()
                        if now - last_sample_time >= SAMPLE_INTERVAL_SECONDS:
                            append_sample(active_label, features)
                            saved_count += 1
                            last_sample_time = now

                if predicting and classifier is not None and features is not None:
                    probabilities = classifier.predict_proba(features.reshape(1, -1))[0]
                    best = int(np.argmax(probabilities))
                    prediction = str(classifier.classes_[best])
                    confidence = float(probabilities[best])
                    if confidence < 0.55:
                        text = f"Uncertain ({confidence:.0%})"
                        color = (0, 180, 255)
                    else:
                        text = f"Prediction: {prediction} ({confidence:.0%})"
                        color = (0, 255, 0)
                    cv2.putText(
                        frame, text, (20, 125), cv2.FONT_HERSHEY_SIMPLEX,
                        0.8, color, 2,
                    )
                elif predicting and features is None:
                    cv2.putText(
                        frame, "Show one hand to the camera", (20, 125),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 180, 255), 2,
                    )

                status = f"Label: {active_label or '(press L to choose)'}"
                if recording:
                    status += f" | RECORDING: {saved_count} samples"
                else:
                    status += " | Not recording"
                cv2.putText(
                    frame, status, (20, 35), cv2.FONT_HERSHEY_SIMPLEX,
                    0.65, (255, 255, 255), 2,
                )
                cv2.putText(
                    frame, "L label  C record  T train  P predict  Q quit",
                    (20, 65), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 2,
                )
                cv2.imshow("MediaPipe Sign Landmark Trainer", frame)

                key = cv2.waitKey(1) & 0xFF
                if key == 255:
                    continue
                if ord("A") <= key <= ord("Z"):
                    key += ord("a") - ord("A")
                now = time.monotonic()
                if key == last_key and now - last_key_time < 0.5:
                    continue
                last_key, last_key_time = key, now

                if key == ord("q"):
                    break
                if key == ord("l"):
                    if recording:
                        print("Stop recording with C before changing the label.")
                        continue
                    entered_label = input(
                        "Label for this sign (for example, hello): "
                    ).strip()
                    if entered_label:
                        active_label = entered_label
                        print(f"Active label: {active_label}")
                    else:
                        print("Label cannot be empty; the active label was not changed.")
                elif key == ord("c"):
                    if recording:
                        recording = False
                        print(f"Stopped recording {saved_count} samples for {active_label}.")
                    elif not active_label:
                        print("Press L and enter a sign label before recording.")
                    else:
                        recording = True
                        saved_count = 0
                        last_sample_time = 0.0
                        print(f"Recording {active_label}. Press C again to stop.")
                elif key == ord("t"):
                    if recording:
                        print("Stop recording with C before training.")
                    else:
                        trained = train_classifier()
                        if trained is not None:
                            classifier = trained
                            predicting = True
                elif key == ord("p"):
                    if classifier is None:
                        print("Train a classifier with T before enabling predictions.")
                    else:
                        predicting = not predicting
                        print(f"Live predictions {'on' if predicting else 'off'}.")
    finally:
        camera.release()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    run_app()
