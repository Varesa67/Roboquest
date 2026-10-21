export function normalizeLandmarks(landmarks, frameWidth, frameHeight) {
  if (!Array.isArray(landmarks) || landmarks.length !== 21) {
    throw new Error("Expected 21 hand landmarks.");
  }

  const points = landmarks.map((point) => [
    Math.fround(point.x * frameWidth),
    Math.fround(point.y * frameHeight),
    Math.fround(point.z * frameWidth),
  ]);
  const wrist = [...points[0]];
  for (const point of points) {
    point[0] = Math.fround(point[0] - wrist[0]);
    point[1] = Math.fround(point[1] - wrist[1]);
    point[2] = Math.fround(point[2] - wrist[2]);
  }

  const xSquared = Math.fround(points[9][0] * points[9][0]);
  const ySquared = Math.fround(points[9][1] * points[9][1]);
  const scale = Math.fround(Math.sqrt(Math.fround(xSquared + ySquared)));
  if (scale < 1e-6) return null;
  return points.flatMap((point) =>
    point.map((coordinate) => Math.fround(coordinate / scale))
  );
}

export function predictForest(model, features) {
  if (
    !model ||
    model.featureCount !== 63 ||
    !Array.isArray(model.classes) ||
    !Array.isArray(model.trees) ||
    features.length !== model.featureCount
  ) {
    throw new Error("The classifier or hand-feature format is invalid.");
  }

  const probabilities = new Float64Array(model.classes.length);
  for (const tree of model.trees) {
    let node = 0;
    while (tree.left[node] !== tree.right[node]) {
      const feature = tree.feature[node];
      node = features[feature] <= tree.threshold[node]
        ? tree.left[node]
        : tree.right[node];
    }

    const values = tree.leafValues[node];
    if (!values || values.length !== probabilities.length) {
      throw new Error("The classifier contains an invalid leaf.");
    }
    for (let index = 0; index < values.length; index += 1) {
      probabilities[index] += values[index];
    }
  }

  for (let index = 0; index < probabilities.length; index += 1) {
    probabilities[index] /= model.trees.length;
  }
  let best = 0;
  for (let index = 1; index < probabilities.length; index += 1) {
    if (probabilities[index] > probabilities[best]) best = index;
  }
  return {
    label: model.classes[best],
    confidence: probabilities[best],
    probabilities: Array.from(probabilities),
  };
}

export function appendLabel(sentence, label) {
  const isLetter = /^[A-Z]$/i.test(label);
  if (isLetter) return sentence + label.toUpperCase();
  const prefix = sentence && !sentence.endsWith(" ") ? `${sentence} ` : sentence;
  return `${prefix}${label} `;
}
