import json
from pathlib import Path

import joblib
import numpy as np

ROOT = Path(__file__).resolve().parent
SOURCE_MODEL = ROOT.parent / "landmark_classifier.joblib"
OUTPUT_MODEL = ROOT / "sign_classifier.json"


def export_model():
    if not SOURCE_MODEL.is_file():
        raise FileNotFoundError(
            f"Trained model not found: {SOURCE_MODEL}. Train it in the desktop app first."
        )

    classifier = joblib.load(SOURCE_MODEL)
    if classifier.n_features_in_ != 63:
        raise ValueError(
            f"Expected a 63-feature landmark classifier, got {classifier.n_features_in_}."
        )

    trees = []
    for estimator in classifier.estimators_:
        tree = estimator.tree_
        leaf_values = []
        for node_index in range(tree.node_count):
            is_leaf = tree.children_left[node_index] == tree.children_right[node_index]
            if not is_leaf:
                leaf_values.append(None)
                continue

            values = tree.value[node_index, 0].astype(np.float64)
            total = float(values.sum())
            if total <= 0:
                raise ValueError(f"Tree has an empty leaf at node {node_index}.")
            leaf_values.append((values / total).tolist())

        trees.append({
            "left": tree.children_left.tolist(),
            "right": tree.children_right.tolist(),
            "feature": tree.feature.tolist(),
            "threshold": tree.threshold.tolist(),
            "leafValues": leaf_values,
        })

    web_model = {
        "format": 1,
        "featureCount": classifier.n_features_in_,
        "classes": [str(label) for label in classifier.classes_],
        "trees": trees,
    }
    OUTPUT_MODEL.write_text(
        json.dumps(web_model, separators=(",", ":"), ensure_ascii=False),
        encoding="utf-8",
    )
    print(
        f"Exported {len(web_model['classes'])} labels and {len(trees)} trees "
        f"to {OUTPUT_MODEL} ({OUTPUT_MODEL.stat().st_size:,} bytes)."
    )


if __name__ == "__main__":
    export_model()
