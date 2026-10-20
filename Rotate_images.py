import os

import cv2


def flip_images():
    gesture_dir = "gestures"
    if not os.path.isdir(gesture_dir):
        raise FileNotFoundError("No gestures folder found. Capture gestures first.")

    for class_id in os.listdir(gesture_dir):
        folder = os.path.join(gesture_dir, class_id)
        if not os.path.isdir(folder):
            continue
        images = [
            name for name in os.listdir(folder)
            if name.lower().endswith((".jpg", ".jpeg", ".png"))
            and not name.startswith("flip_")
        ]
        for name in images:
            path = os.path.join(folder, name)
            image = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
            if image is None:
                raise ValueError(f"Could not read image: {path}")
            flipped = cv2.flip(image, 1)
            output_path = os.path.join(folder, "flip_" + os.path.splitext(name)[0] + ".jpg")
            if not cv2.imwrite(output_path, flipped):
                raise OSError(f"Could not write image: {output_path}")


if __name__ == "__main__":
    flip_images()
