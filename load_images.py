import json
import os
import pickle
import random

import cv2

GESTURE_DIR = "gestures"
IMAGE_SIZE = 50
RANDOM_SEED = 42


def load_dataset():
    if not os.path.isdir(GESTURE_DIR):
        raise FileNotFoundError("No gestures folder found. Capture gestures first.")

    class_dirs = [
        name for name in os.listdir(GESTURE_DIR)
        if os.path.isdir(os.path.join(GESTURE_DIR, name)) and name.isdigit()
    ]
    class_dirs.sort(key=int)
    if len(class_dirs) < 2:
        raise ValueError("Capture at least two gesture classes before preparing the dataset.")
    if [int(name) for name in class_dirs] != list(range(len(class_dirs))):
        raise ValueError("Gesture folder IDs must be consecutive, starting at 0 (0, 1, 2, ...).")

    train, validation, test = [], [], []
    for class_index, class_id in enumerate(class_dirs):
        folder = os.path.join(GESTURE_DIR, class_id)
        images = [
            os.path.join(folder, name) for name in os.listdir(folder)
            if name.lower().endswith((".jpg", ".jpeg", ".png"))
        ]
        images.sort()
        if len(images) < 10:
            raise ValueError(f"Gesture {class_id} has only {len(images)} images; capture at least 10.")

        random.Random(RANDOM_SEED + class_index).shuffle(images)
        val_end = max(1, int(len(images) * 0.8))
        test_end = max(val_end + 1, int(len(images) * 0.9))
        if test_end >= len(images):
            raise ValueError(f"Gesture {class_id} needs at least 10 images to split into datasets.")

        for split, paths in (
            (train, images[:val_end]),
            (validation, images[val_end:test_end]),
            (test, images[test_end:]),
        ):
            for path in paths:
                image = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
                if image is None:
                    raise ValueError(f"Could not read image: {path}")
                image = cv2.resize(image, (IMAGE_SIZE, IMAGE_SIZE))
                split.append((image, class_index))

    random.Random(RANDOM_SEED).shuffle(train)
    random.Random(RANDOM_SEED + 1).shuffle(validation)
    random.Random(RANDOM_SEED + 2).shuffle(test)
    return class_dirs, train, validation, test


def save_split(filename, data):
    images, labels = zip(*data)
    with open(filename + "_images", "wb") as file:
        pickle.dump(images, file)
    with open(filename + "_labels", "wb") as file:
        pickle.dump(labels, file)
    print(f"{filename}: {len(images)} images")


def main():
    class_ids, train, validation, test = load_dataset()
    with open("class_ids.json", "w", encoding="utf-8") as file:
        json.dump(class_ids, file)
    save_split("train", train)
    save_split("val", validation)
    save_split("test", test)
    print(f"Prepared {len(class_ids)} classes.")


if __name__ == "__main__":
    main()
