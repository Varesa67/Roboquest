import json
import os
import pickle

os.environ["TF_CPP_MIN_LOG_LEVEL"] = "2"

import numpy as np
import tensorflow as tf
from tensorflow.keras import Sequential
from tensorflow.keras.layers import Conv2D, Dense, Dropout, Flatten, MaxPooling2D
from tensorflow.keras.models import load_model

MODEL_PATH = "cnn_model.keras"
IMAGE_SIZE = 50


def load_pickle(path):
    with open(path, "rb") as file:
        return pickle.load(file)


def train():
    if not os.path.isfile("class_ids.json"):
        raise FileNotFoundError("Run python load_images.py first.")

    with open("class_ids.json", encoding="utf-8") as file:
        class_ids = json.load(file)
    if len(class_ids) < 2:
        raise ValueError("Capture at least two gesture classes before training.")

    train_images = np.asarray(load_pickle("train_images"), dtype=np.float32)
    train_labels = np.asarray(load_pickle("train_labels"), dtype=np.int32)
    val_images = np.asarray(load_pickle("val_images"), dtype=np.float32)
    val_labels = np.asarray(load_pickle("val_labels"), dtype=np.int32)
    test_images = np.asarray(load_pickle("test_images"), dtype=np.float32)
    test_labels = np.asarray(load_pickle("test_labels"), dtype=np.int32)

    train_images = train_images.reshape((-1, IMAGE_SIZE, IMAGE_SIZE, 1)) / 255.0
    val_images = val_images.reshape((-1, IMAGE_SIZE, IMAGE_SIZE, 1)) / 255.0
    test_images = test_images.reshape((-1, IMAGE_SIZE, IMAGE_SIZE, 1)) / 255.0
    train_labels = tf.keras.utils.to_categorical(train_labels, len(class_ids))
    val_labels = tf.keras.utils.to_categorical(val_labels, len(class_ids))
    test_labels = tf.keras.utils.to_categorical(test_labels, len(class_ids))

    model = Sequential([
        Conv2D(16, (3, 3), activation="relu", input_shape=(IMAGE_SIZE, IMAGE_SIZE, 1)),
        MaxPooling2D(pool_size=(2, 2)),
        Conv2D(32, (3, 3), activation="relu"),
        MaxPooling2D(pool_size=(2, 2)),
        Flatten(),
        Dense(128, activation="relu"),
        Dropout(0.2),
        Dense(len(class_ids), activation="softmax"),
    ])
    model.compile(optimizer="adam", loss="categorical_crossentropy", metrics=["accuracy"])
    model.summary()
    model.fit(
        train_images,
        train_labels,
        validation_data=(val_images, val_labels),
        epochs=15,
        batch_size=32,
        callbacks=[
            tf.keras.callbacks.ModelCheckpoint(
                MODEL_PATH, monitor="val_accuracy", save_best_only=True, mode="max"
            )
        ],
    )
    best_model = load_model(MODEL_PATH)
    _, accuracy = best_model.evaluate(test_images, test_labels, verbose=0)
    print(f"Test accuracy: {accuracy:.1%}")
    print(f"Best model saved to {MODEL_PATH}")
    tf.keras.backend.clear_session()


if __name__ == "__main__":
    train()
