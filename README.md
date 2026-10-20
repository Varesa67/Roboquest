# Webcam Sign Landmark Trainer

This project uses Google's MediaPipe Hand Landmarker to track the 21 points
on one hand, then trains a small classifier from signs you record yourself.
The camera app draws the hand landmarks on screen, saves labeled examples, and
can show live predictions after training.

This starter recognizes **static hand shapes** only. Moving signs such as ASL
J and Z, continuous signing, facial expressions, body position, and ASL grammar
are not included.

## Website version

The phone-friendly static site is in [`WebsiteCode/`](WebsiteCode/). It uses
your trained classifier in the browser and has no sign-recording or training
controls. See [`WebsiteCode/README.md`](WebsiteCode/README.md) for the upload
files, HTTPS/camera requirements, and how to refresh the model after
retraining.

## Install (Windows, Python 3.12)

Open PowerShell in this folder:

```powershell
.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

If `.venv` does not exist yet:

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

## Record signs, train, and try them

Start the app from this folder:

```powershell
.\.venv\Scripts\python.exe landmark_sign_app.py
```

The first run downloads Google's hand-landmarker model into
`models\hand_landmarker.task`. It needs internet for this one-time download;
later runs can use that local file.

With the camera window focused:

1. Press **L**. In the PowerShell window, type a short label for the sign,
   such as `hello`, and press Enter.
2. Put one hand clearly in view and make that sign. Press **C** to start
   collecting examples; press **C** again to stop. Examples are collected
   about five times per second and saved to `landmark_samples.csv`.
3. Change your hand position or orientation slightly, then record another
   short batch with the same label. Aim for at least **30 examples per sign**
   across a few batches. Use a steady sign: this version learns hand shapes,
   not sign motion.
4. Repeat steps 1–3 for at least **two different labels**. Keep your hand
   visible and use similar framing and lighting.
5. Press **T** to train. The app needs at least 10 recorded examples for each
   of at least two labels. It saves the classifier as
   `landmark_classifier.joblib` and turns live predictions on.
6. Try each sign again under slightly different positioning. Press **P** to
   turn live predictions on or off, and **Q** to quit.

The app accepts the controls in either uppercase or lowercase. Press **Q** in
the focused camera window to exit. Your landmark samples, trained
classifier, and downloaded MediaPipe model are kept between runs.

## What the app learns

MediaPipe locates 21 hand landmarks in each camera frame. The app moves those
points relative to the wrist and scales them by hand size, then saves their
coordinates with the label you entered. Pressing **T** trains a Random Forest
classifier on the saved examples. MediaPipe tracks the hand; the classifier
learns the labels. The app does not train MediaPipe itself.

## Tips

- Start with two or three hand shapes that look noticeably different.
- Use the same hand and keep your whole hand in the camera frame.
- Record several separate batches per label and vary distance and position a
  little; do not capture all examples from one unmoving frame.
- Keep the camera window focused when using its keyboard controls. Pressing L
  pauses the video while you type the label in PowerShell.
- A displayed prediction is a best guess among the labels you recorded. The
  app shows "Uncertain" for lower-confidence predictions, but the classifier
  does not know unrecorded signs and is not a full ASL translator.
- This project does not use the old color-histogram hand segmentation or the
  old CNN model. The earlier scripts and their existing data/model files are
  left in the folder unchanged; use `landmark_sign_app.py` for this workflow.
- Do not load a `.joblib` model from a source you do not trust.
cd C:\Users\hp\Downloads\SchoolProject