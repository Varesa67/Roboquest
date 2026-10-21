import { FilesetResolver, HandLandmarker } from
  "https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.21/vision_bundle.mjs";
import { appendLabel, normalizeLandmarks, predictForest } from "./classifier.mjs";

const MODEL_URL = "./sign_classifier.json";
const HAND_MODEL_URL = "./hand_landmarker.task";
const WASM_URL = "https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.21/wasm";
const CONNECTIONS = [
  [0, 1], [1, 2], [2, 3], [3, 4],
  [0, 5], [5, 6], [6, 7], [7, 8],
  [5, 9], [9, 10], [10, 11], [11, 12],
  [9, 13], [13, 14], [14, 15], [15, 16],
  [13, 17], [17, 18], [18, 19], [19, 20],
  [0, 17],
];
const video = document.querySelector("#camera");
const preview = document.querySelector("#preview");
const previewContext = preview.getContext("2d");
const processCanvas = document.createElement("canvas");
const processContext = processCanvas.getContext("2d");
const cameraButton = document.querySelector("#camera-button");
const status = document.querySelector("#status");
const placeholder = document.querySelector("#camera-placeholder");
const handIndicator = document.querySelector("#hand-indicator");
const resultHeading = document.querySelector("#result-heading");
const confidence = document.querySelector("#confidence");
const addButton = document.querySelector("#add-button");
const sentence = document.querySelector("#sentence");

let classifier = null;
let landmarker = null;
let cameraStream = null;
let animationFrame = 0;
let lastDetectionTime = 0;
let lastTimestamp = 0;
let recentPredictions = [];
let stablePrediction = null;
let isDetecting = false;
let currentLandmarks = null;

function setStatus(message, isError = false) {
  status.textContent = message;
  status.classList.toggle("status-error", isError);
}

async function loadClassifier() {
  const response = await fetch(MODEL_URL, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`Could not load sign_classifier.json (${response.status}).`);
  }
  const model = await response.json();
  if (model.featureCount !== 63 || !Array.isArray(model.classes) || !model.trees?.length) {
    throw new Error("The exported classifier file is not in the expected format.");
  }
  classifier = model;
}

async function createLandmarker() {
  if (landmarker) return landmarker;
  const vision = await FilesetResolver.forVisionTasks(WASM_URL);
  landmarker = await HandLandmarker.createFromOptions(vision, {
    baseOptions: { modelAssetPath: HAND_MODEL_URL },
    runningMode: "VIDEO",
    numHands: 1,
    minHandDetectionConfidence: 0.5,
    minHandPresenceConfidence: 0.5,
    minTrackingConfidence: 0.5,
  });
  return landmarker;
}

function drawMirroredVideo() {
  const width = video.videoWidth;
  const height = video.videoHeight;
  if (!width || !height) return;
  if (preview.width !== width || preview.height !== height) {
    preview.width = width;
    preview.height = height;
    processCanvas.width = width;
    processCanvas.height = height;
  }

  previewContext.save();
  previewContext.translate(width, 0);
  previewContext.scale(-1, 1);
  previewContext.drawImage(video, 0, 0, width, height);
  previewContext.restore();
}

function drawLandmarks(landmarks) {
  const width = preview.width;
  const height = preview.height;
  const points = landmarks.map((point) => ({
    x: point.x * width,
    y: point.y * height,
  }));

  previewContext.lineWidth = Math.max(2, width / 320);
  previewContext.strokeStyle = "#46e3a2";
  for (const [start, end] of CONNECTIONS) {
    previewContext.beginPath();
    previewContext.moveTo(points[start].x, points[start].y);
    previewContext.lineTo(points[end].x, points[end].y);
    previewContext.stroke();
  }
  previewContext.fillStyle = "#fff";
  for (const point of points) {
    previewContext.beginPath();
    previewContext.arc(point.x, point.y, Math.max(3, width / 180), 0, Math.PI * 2);
    previewContext.fill();
  }
}

function updateStablePrediction(prediction) {
  const now = performance.now();
  recentPredictions = recentPredictions.filter((entry) => now - entry.time < 900);
  recentPredictions.push({ ...prediction, time: now });
  if (recentPredictions.length > 7) recentPredictions.shift();

  const counts = new Map();
  for (const entry of recentPredictions) {
    counts.set(entry.label, (counts.get(entry.label) || 0) + 1);
  }
  let bestLabel = null;
  let bestCount = 0;
  for (const [label, count] of counts) {
    if (count > bestCount) {
      bestLabel = label;
      bestCount = count;
    }
  }
  const latest = recentPredictions.at(-1);
  if (bestCount >= 4 && latest?.label === bestLabel && latest.confidence >= 0.4) {
    stablePrediction = latest;
    resultHeading.textContent = latest.label;
    confidence.textContent = `Steady · ${Math.round(latest.confidence * 100)}% confidence`;
    addButton.disabled = false;
    handIndicator.textContent = "Hand tracked";
    return;
  }

  stablePrediction = null;
  addButton.disabled = true;
  if (latest) {
    resultHeading.textContent = latest.label;
    confidence.textContent = `Hold steady · ${Math.round(latest.confidence * 100)}% confidence`;
  }
  handIndicator.textContent = "Hand tracked";
}

function clearPrediction() {
  recentPredictions = [];
  stablePrediction = null;
  resultHeading.textContent = "—";
  confidence.textContent = "Show one hand";
  addButton.disabled = true;
  handIndicator.textContent = "No hand detected";
}

function renderFrame(now) {
  if (!cameraStream || isDetecting) return;
  drawMirroredVideo();

  if (now - lastDetectionTime >= 100) {
    lastDetectionTime = now;
    isDetecting = true;
    try {
      processContext.save();
      processContext.translate(preview.width, 0);
      processContext.scale(-1, 1);
      processContext.drawImage(video, 0, 0, preview.width, preview.height);
      processContext.restore();
      const timestamp = Math.max(now, lastTimestamp + 1);
      const result = landmarker.detectForVideo(processCanvas, timestamp);
      lastTimestamp = timestamp;
      if (result.landmarks.length) {
        currentLandmarks = result.landmarks[0];
        const features = normalizeLandmarks(
          currentLandmarks, preview.width, preview.height
        );
        if (features) updateStablePrediction(predictForest(classifier, features));
        else {
          currentLandmarks = null;
          clearPrediction();
        }
      } else {
        currentLandmarks = null;
        clearPrediction();
      }
    } catch (error) {
      stopCamera();
      setStatus(`Hand tracking failed: ${error.message}`, true);
      return;
    } finally {
      isDetecting = false;
    }
  }

  if (currentLandmarks) drawLandmarks(currentLandmarks);
  animationFrame = requestAnimationFrame(renderFrame);
}

async function startCamera() {
  if (!classifier) {
    setStatus("The sign model has not loaded.", true);
    return;
  }
  cameraButton.disabled = true;
  setStatus("Requesting camera access…");
  try {
    await createLandmarker();
    cameraStream = await navigator.mediaDevices.getUserMedia({
      audio: false,
      video: {
        facingMode: "user",
        width: { ideal: 640 },
        height: { ideal: 480 },
      },
    });
    video.srcObject = cameraStream;
    await video.play();
    placeholder.hidden = true;
    preview.hidden = false;
    cameraButton.textContent = "Stop camera";
    setStatus("Camera is running. Hold a sign steady, then add it.");
    handIndicator.textContent = "Looking for a hand…";
    animationFrame = requestAnimationFrame(renderFrame);
  } catch (error) {
    stopCamera();
    const message = error.name === "NotAllowedError"
      ? "Camera permission was denied. Allow camera access in your browser and retry."
      : `Could not start camera: ${error.message}`;
    setStatus(message, true);
  } finally {
    cameraButton.disabled = false;
  }
}

function stopCamera() {
  if (animationFrame) cancelAnimationFrame(animationFrame);
  animationFrame = 0;
  isDetecting = false;
  if (cameraStream) {
    for (const track of cameraStream.getTracks()) track.stop();
  }
  cameraStream = null;
  video.srcObject = null;
  previewContext.clearRect(0, 0, preview.width, preview.height);
  currentLandmarks = null;
  preview.hidden = true;
  placeholder.hidden = false;
  cameraButton.textContent = "Start camera";
  handIndicator.textContent = "Camera is off";
  clearPrediction();
}

cameraButton.addEventListener("click", () => {
  if (cameraStream) {
    stopCamera();
    setStatus("Camera stopped.");
  } else {
    startCamera();
  }
});

addButton.addEventListener("click", () => {
  if (!stablePrediction) return;
  sentence.value = appendLabel(sentence.value, stablePrediction.label);
  sentence.dispatchEvent(new Event("input", { bubbles: true }));
  sentence.focus();
});

document.querySelector("#space-button").addEventListener("click", () => {
  if (!sentence.value.endsWith(" ")) sentence.value += " ";
  sentence.focus();
});

document.querySelector("#clear-button").addEventListener("click", () => {
  sentence.value = "";
  sentence.focus();
});

document.querySelector("#copy-button").addEventListener("click", async () => {
  try {
    await navigator.clipboard.writeText(sentence.value);
    setStatus("Sentence copied.");
  } catch (error) {
    setStatus(`Could not copy sentence: ${error.message}`, true);
  }
});

window.addEventListener("pagehide", stopCamera);

loadClassifier()
  .then(() => setStatus(`Ready · ${classifier.classes.length} trained signs loaded.`))
  .catch((error) => setStatus(error.message, true));
