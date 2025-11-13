import argparse
import os
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, List, Tuple

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from sklearn.feature_selection import SelectKBest, f_classif
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import LabelEncoder, MinMaxScaler
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import regularizers
from tensorflow.keras.layers import Dense, Dropout, Input
from tensorflow.keras.models import Model
from tensorflow.keras.regularizers import l2

BIT_SHIFT = 16
PRUNE_PERCENTAGES = np.arange(0.00, 0.52, 0.02)
ACCURACY_TOLERANCE = 0.01
FEATURES_TO_SELECT = 16
DEFAULT_EPOCHS = 100
DEFAULT_BATCH_SIZE = 64
DEFAULT_VAL_SPLIT = 0.2
PROJECT_ROOT = Path(__file__).resolve().parents[1]
DATASET_ROOT = PROJECT_ROOT / "dataset"
MODEL_DIR = DATASET_ROOT / "saved_models"
RESULTS_DIR = PROJECT_ROOT / "Results"
MODEL_DIR.mkdir(parents=True, exist_ok=True)
RESULTS_DIR.mkdir(parents=True, exist_ok=True)

gpus = tf.config.list_physical_devices("GPU")
if gpus:
    for gpu in gpus:
        try:
            tf.config.experimental.set_memory_growth(gpu, True)
        except RuntimeError:
            pass


def map_to_binary_label(value):
    if isinstance(value, (int, np.integer, float, np.floating)):
        return "benign" if int(value) == 0 else "attacker"
    value_str = str(value).strip().lower()
    return "benign" if value_str in {"benign", "normal", "0"} else "attacker"


def apply_left_bit_shift(data, shift_bits=BIT_SHIFT):
    return (data * (2 ** shift_bits)).astype(np.int64)


def apply_right_bit_shift(data, shift_bits=BIT_SHIFT):
    return data.astype(np.float64) / (2 ** shift_bits)


def create_exponential_tables():
    integer_exponents = np.arange(-9, 10, 1)
    integer_table = np.exp(integer_exponents)
    fractional_exponents = np.arange(-0.999, 1.000, 0.001)
    fractional_table = np.exp(fractional_exponents)
    return integer_table, fractional_table


INTEGER_EXP_TABLE, FRACTIONAL_EXP_TABLE = create_exponential_tables()


def table_based_exp(x):
    x = np.asarray(x)
    result = np.ones_like(x, dtype=np.float64)
    x_clamped = np.clip(x, -9.999, 9.999)
    integer_part = np.floor(x_clamped).astype(int)
    fractional_part = x_clamped - integer_part
    integer_part = np.clip(integer_part, -9, 9)
    fractional_part = np.clip(fractional_part, -0.999, 0.999)
    integer_indices = integer_part + 9
    integer_values = INTEGER_EXP_TABLE[integer_indices]
    fractional_indices = np.round((fractional_part + 0.999) / 0.001).astype(int)
    fractional_indices = np.clip(fractional_indices, 0, len(FRACTIONAL_EXP_TABLE) - 1)
    fractional_values = FRACTIONAL_EXP_TABLE[fractional_indices]
    result = integer_values * fractional_values
    return result


def fixed_point_dense_layer(inputs, weights, biases, activation="relu"):
    output = np.dot(inputs, weights)
    output = output >> BIT_SHIFT
    output = output + biases
    if activation == "relu":
        output = np.maximum(0, output)
    elif activation == "softmax":
        float_output = apply_right_bit_shift(output)
        exp_vals = table_based_exp(float_output - np.max(float_output, axis=1, keepdims=True))
        softmax_output = exp_vals / np.sum(exp_vals, axis=1, keepdims=True)
        output = apply_left_bit_shift(softmax_output)
    elif activation == "sigmoid":
        float_output = apply_right_bit_shift(output)
        exp_vals = table_based_exp(-float_output)
        sigmoid_output = 1.0 / (1.0 + exp_vals)
        output = apply_left_bit_shift(sigmoid_output)
    return output


def fixed_point_model_predict(model, x_input, verbose=False):
    current_input = apply_left_bit_shift(x_input)
    dense_layers = [layer for layer in model.layers if isinstance(layer, keras.layers.Dense)]
    if verbose:
        print(f"Processing {len(dense_layers)} dense layers with fixed-point arithmetic...")
    for i, layer in enumerate(dense_layers):
        weights, biases = layer.get_weights()
        fixed_weights = apply_left_bit_shift(weights)
        fixed_biases = apply_left_bit_shift(biases)
        if verbose:
            print(f"Layer {i + 1}: Input shape {current_input.shape}, Weights shape {fixed_weights.shape}")
        activation = "sigmoid" if i == len(dense_layers) - 1 and layer.units == 1 else "relu"
        if i == len(dense_layers) - 1 and layer.units > 1:
            activation = "softmax"
        current_input = fixed_point_dense_layer(current_input, fixed_weights, fixed_biases, activation)
        if verbose:
            print(f"Layer {i + 1}: Output shape {current_input.shape}, Activation: {activation}")
    final_output = apply_right_bit_shift(current_input)
    if dense_layers:
        last_units = dense_layers[-1].units
        if last_units > 1 and final_output.ndim == 2:
            final_output = final_output / np.sum(final_output, axis=1, keepdims=True)
        elif last_units == 1:
            final_output = final_output.reshape(-1)
    return final_output


def encode_categorical_features(feature_data: pd.DataFrame) -> pd.DataFrame:
    for col in feature_data.select_dtypes(include=["object"]).columns:
        le = LabelEncoder()
        feature_data[col] = le.fit_transform(feature_data[col].astype(str))
    return feature_data


def load_nsl_kdd(file_path: str) -> Tuple[np.ndarray, np.ndarray]:
    data = pd.read_csv(file_path, header=None).fillna(0)
    feature_data = encode_categorical_features(data.iloc[:, :-1].copy())
    labels = data.iloc[:, -1].apply(map_to_binary_label)
    y = LabelEncoder().fit_transform(labels)
    return feature_data.values, y


def load_unsw_nb15(file_path: str) -> Tuple[np.ndarray, np.ndarray]:
    data = pd.read_csv(file_path)
    if "id" in data.columns:
        data = data.drop(columns=["id"])
    data = data.fillna(0)
    feature_columns = [
        col
        for col in data.columns
        if col not in {"label", "attack_cat"}
    ]
    feature_data = encode_categorical_features(data[feature_columns].copy())
    label_series = data["label"] if "label" in data.columns else data.iloc[:, -1]
    binary_labels = label_series.apply(map_to_binary_label)
    y = LabelEncoder().fit_transform(binary_labels)
    return feature_data.values, y


TONIOT_COLUMNS_TO_DROP = [
    "src_ip",
    "dst_ip",
    "ssl_subject",
    "ssl_issuer",
    "http_uri",
    "http_user_agent",
    "http_orig_mime_types",
    "http_resp_mime_types",
    "weird_name",
    "weird_addl",
    "weird_notice",
]


def load_ton_iot(file_path: str) -> Tuple[np.ndarray, np.ndarray]:
    data = pd.read_csv(file_path)
    data = data.drop(columns=[col for col in TONIOT_COLUMNS_TO_DROP if col in data.columns], errors="ignore")
    data = data.fillna(0)
    feature_columns = [col for col in data.columns if col not in {"label", "type"}]
    feature_data = encode_categorical_features(data[feature_columns].copy())
    if "type" in data.columns:
        label_series = data["type"]
    elif "label" in data.columns:
        label_series = data["label"]
    else:
        label_series = data.iloc[:, -1]
    binary_labels = label_series.apply(map_to_binary_label)
    y = LabelEncoder().fit_transform(binary_labels)
    return feature_data.values, y


def select_and_scale_features(X: np.ndarray, y: np.ndarray):
    x_train, x_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, random_state=42, stratify=y
    )
    k_features = max(1, min(FEATURES_TO_SELECT, x_train.shape[1]))
    selector = SelectKBest(score_func=f_classif, k=k_features)
    x_train = selector.fit_transform(x_train, y_train)
    x_test = selector.transform(x_test)
    scaler = MinMaxScaler()
    x_train = scaler.fit_transform(x_train)
    x_test = scaler.transform(x_test)
    return x_train, x_test, y_train, y_test


def build_base_model(input_dim: int) -> keras.Model:
    inputs = Input(shape=(input_dim,))
    x = Dense(16, activation="relu", kernel_regularizer=regularizers.l2(0.001))(inputs)
    x = Dropout(0.2)(x)
    x = Dense(8, activation="relu", kernel_regularizer=regularizers.l2(0.001))(x)
    x = Dropout(0.2)(x)
    x = Dense(4, activation="relu", kernel_regularizer=regularizers.l2(0.001))(x)
    x = Dropout(0.2)(x)
    outputs = Dense(1, activation="sigmoid")(x)
    model = Model(inputs=inputs, outputs=outputs)
    model.compile(optimizer="adam", loss="binary_crossentropy", metrics=["accuracy"])
    return model


def evaluate_model(model, x_test, y_test):
    y_pred_prob = model.predict(x_test, verbose=0)
    if y_pred_prob.ndim > 1 and y_pred_prob.shape[1] == 1:
        y_pred_prob = y_pred_prob.ravel()
        y_pred_classes = (y_pred_prob >= 0.5).astype(int)
    elif y_pred_prob.ndim == 1:
        y_pred_classes = (y_pred_prob >= 0.5).astype(int)
    else:
        y_pred_classes = y_pred_prob.argmax(axis=1)
    accuracy = np.mean(y_pred_classes == y_test)
    return accuracy


def evaluate_model_fixed_point(model, x_test, y_test, verbose=False):
    y_pred_prob = fixed_point_model_predict(model, x_test, verbose=verbose)
    if y_pred_prob.ndim > 1 and y_pred_prob.shape[1] == 1:
        y_pred_prob = y_pred_prob.ravel()
        y_pred_classes = (y_pred_prob >= 0.5).astype(int)
    elif y_pred_prob.ndim == 1:
        y_pred_classes = (y_pred_prob >= 0.5).astype(int)
    else:
        y_pred_classes = y_pred_prob.argmax(axis=1)
    accuracy = np.mean(y_pred_classes == y_test)
    return accuracy


def prune_dense_layer_by_count(weights, biases, num_remove):
    neuron_importance = np.sum(np.abs(weights), axis=0)
    sorted_indices = np.argsort(neuron_importance)
    important_indices = sorted_indices[num_remove:]
    pruned_weights = weights[:, important_indices]
    pruned_biases = biases[important_indices]
    return pruned_weights, pruned_biases, important_indices


def rebuild_pruned_model(input_dim, num_classes, neurons_per_layer):
    input_layer = Input(shape=(input_dim,))
    x = input_layer
    for num_neurons in neurons_per_layer:
        x = Dense(num_neurons, activation="relu", kernel_regularizer=l2(0.001))(x)
    output_units = num_classes if num_classes > 1 else 1
    output_activation = "softmax" if output_units > 1 else "sigmoid"
    output_layer = Dense(output_units, activation=output_activation)(x)
    return Model(inputs=input_layer, outputs=output_layer)


def prune_model_by_count(original_model, neurons_to_remove_per_layer):
    dense_layers = [layer for layer in original_model.layers if isinstance(layer, keras.layers.Dense)]
    if len(dense_layers) < 2:
        raise ValueError("Model must include at least one hidden layer and an output layer.")
    if len(neurons_to_remove_per_layer) != len(dense_layers) - 1:
        raise ValueError("Mismatch between hidden layers and removal list.")
    pruned_weights = []
    pruned_biases = []
    important_indices_per_layer = []
    w, b = dense_layers[0].get_weights()
    num_remove = neurons_to_remove_per_layer[0]
    pw, pb, idx = prune_dense_layer_by_count(w, b, num_remove)
    pruned_weights.append(pw)
    pruned_biases.append(pb)
    important_indices_per_layer.append(idx)
    for i in range(1, len(dense_layers) - 1):
        w, b = dense_layers[i].get_weights()
        w = w[important_indices_per_layer[-1], :]
        num_remove = neurons_to_remove_per_layer[i]
        pw, pb, idx = prune_dense_layer_by_count(w, b, num_remove)
        pruned_weights.append(pw)
        pruned_biases.append(pb)
        important_indices_per_layer.append(idx)
    w_out, b_out = dense_layers[-1].get_weights()
    w_out = w_out[important_indices_per_layer[-1], :]
    pruned_weights.append(w_out)
    pruned_biases.append(b_out)
    input_dim = dense_layers[0].get_weights()[0].shape[0]
    num_classes = dense_layers[-1].get_weights()[1].shape[0]
    neurons_per_layer = [pw.shape[1] for pw in pruned_weights[:-1]]
    pruned_model = rebuild_pruned_model(input_dim, num_classes, neurons_per_layer)
    dense_layers_new = [layer for layer in pruned_model.layers if isinstance(layer, keras.layers.Dense)]
    for new_layer, pw, pb in zip(dense_layers_new, pruned_weights, pruned_biases):
        new_layer.set_weights([pw, pb])
    loss_fn = "binary_crossentropy" if pruned_model.output_shape[-1] == 1 else "sparse_categorical_crossentropy"
    pruned_model.compile(optimizer="adam", loss=loss_fn, metrics=["accuracy"])
    return pruned_model


def get_model_disk_size(model):
    with tempfile.NamedTemporaryFile(delete=False, suffix=".keras") as tmp:
        model.save(tmp.name, overwrite=True)
        tmp.flush()
        size_kb = os.path.getsize(tmp.name) / 1024
    os.unlink(tmp.name)
    return size_kb


def calculate_operations_per_sample_and_registers(model):
    multiplications = 0
    additions = 0
    total_registers = 0
    max_activations = 0
    for layer in model.layers:
        if isinstance(layer, keras.layers.Dense):
            weights = layer.get_weights()
            if len(weights) > 0:
                input_neurons = weights[0].shape[0]
                output_neurons = weights[0].shape[1]
                multiplications += input_neurons * output_neurons
                additions += input_neurons * output_neurons + output_neurons
                total_registers += input_neurons * output_neurons
                total_registers += output_neurons
                max_activations = max(max_activations, output_neurons)
    total_registers += max_activations
    return multiplications, additions, total_registers


def save_layer_details(model, prune_percentage, accuracy, registers, file_path):
    with open(file_path, "w", encoding="utf-8") as layer_file:
        layer_file.write("Best Model (Greedy Selection):\n")
        layer_file.write(f"  Prune Percentage: {prune_percentage:.2f}%\n")
        layer_file.write(f"  Accuracy: {accuracy:.4f}\n")
        layer_file.write(f"  Total Registers: {registers}\n\n")
        layer_file.write("Layer-wise details for the best model:\n")
        for idx, layer in enumerate(model.layers):
            if isinstance(layer, keras.layers.Dense):
                weights = layer.get_weights()
                if len(weights) > 0:
                    input_neurons = weights[0].shape[0]
                    output_neurons = weights[0].shape[1]
                    num_weights = weights[0].size
                    num_biases = weights[1].size
                    layer_file.write(
                        f"Layer {idx} (Dense): Input Neurons = {input_neurons}, Output Neurons = {output_neurons}, "
                        f"Weights = {num_weights}, Biases = {num_biases}\n"
                    )
                    weights_str = np.array2string(weights[0], threshold=weights[0].size, max_line_width=120)
                    biases_str = np.array2string(weights[1], threshold=weights[1].size, max_line_width=120)
                    layer_file.write(f"Weights array:\n{weights_str}\n")
                    layer_file.write(f"Biases array:\n{biases_str}\n\n")


@dataclass
class DatasetConfig:
    key: str
    display_name: str
    default_filename: str
    loader: Callable[[str], Tuple[np.ndarray, np.ndarray]]
    model_filename: str
    layer_details_filename: str


def get_dataset_configs() -> Dict[str, DatasetConfig]:
    return {
        "nsl-kdd": DatasetConfig(
            key="nsl-kdd",
            display_name="NSL-KDD",
            default_filename="nsl-kdd-dataset.csv",
            loader=load_nsl_kdd,
            model_filename="nsl_kdd_model.keras",
            layer_details_filename="nsl_kdd_best_model_layers.txt",
        ),
        "unsw-nb15": DatasetConfig(
            key="unsw-nb15",
            display_name="UNSW-NB15",
            default_filename="UNSW_NB15_training-set.csv",
            loader=load_unsw_nb15,
            model_filename="unsw_nb15_model.keras",
            layer_details_filename="unsw_nb15_best_model_layers.txt",
        ),
        "ton-iot": DatasetConfig(
            key="ton-iot",
            display_name="ToN-IoT",
            default_filename="toniot_dataset.csv",
            loader=load_ton_iot,
            model_filename="toniot_model.keras",
            layer_details_filename="toniot_best_model_layers.txt",
        ),
    }


def resolve_dataset_path(cfg: DatasetConfig, data_root: Path) -> Path:
    candidate = data_root / cfg.default_filename
    if candidate.exists():
        return candidate
    raise FileNotFoundError(
        f"Dataset file for {cfg.display_name} not found. Expected at {candidate}."
    )


def collect_pruning_metrics(model, x_test, y_test, cfg: DatasetConfig, verbose_fixed_point: bool):
    dense_layers = [layer for layer in model.layers if isinstance(layer, keras.layers.Dense)]
    hidden_layers = dense_layers[:-1]
    metrics = {
        "key": cfg.key,
        "display_name": cfg.display_name,
        "prune_percentages": PRUNE_PERCENTAGES,
        "accuracies": [],
        "fixed_point_accuracies": [],
        "model_sizes_kb": [],
        "registers": [],
        "multipliers": [],
        "additions": [],
    }
    for prune_percentage in PRUNE_PERCENTAGES:
        neurons_to_remove = [
            int(np.floor(prune_percentage * layer.units)) for layer in hidden_layers
        ]
        pruned_model = prune_model_by_count(model, neurons_to_remove)
        accuracy = evaluate_model(pruned_model, x_test, y_test)
        fixed_point_accuracy = evaluate_model_fixed_point(
            pruned_model, x_test, y_test, verbose=verbose_fixed_point
        )
        model_size = get_model_disk_size(pruned_model)
        multiplications, additions, registers = calculate_operations_per_sample_and_registers(pruned_model)
        metrics["accuracies"].append(accuracy)
        metrics["fixed_point_accuracies"].append(fixed_point_accuracy)
        metrics["model_sizes_kb"].append(model_size)
        metrics["registers"].append(registers)
        metrics["multipliers"].append(multiplications)
        metrics["additions"].append(additions)
    best_accuracy = max(metrics["accuracies"])
    accuracy_threshold = best_accuracy - ACCURACY_TOLERANCE
    candidates = [
        (idx, acc, regs)
        for idx, (acc, regs) in enumerate(zip(metrics["accuracies"], metrics["registers"]))
        if acc >= accuracy_threshold
    ]
    if candidates:
        best_idx, best_acc, best_regs = min(candidates, key=lambda item: item[2])
        best_prune_pct = PRUNE_PERCENTAGES[best_idx] * 100
        neurons_to_remove = [
            int(np.floor(PRUNE_PERCENTAGES[best_idx] * layer.units)) for layer in hidden_layers
        ]
        best_pruned_model = prune_model_by_count(model, neurons_to_remove)
        details_path = MODEL_DIR / cfg.layer_details_filename
        save_layer_details(best_pruned_model, best_prune_pct, best_acc, best_regs, details_path)
    return metrics


def plot_accuracy_vs_pruning(results: List[Dict], output_path: Path):
    plt.figure(figsize=(8, 6))
    markers = ["o", "s", "^"]
    for idx, res in enumerate(results):
        prune_axis = res["prune_percentages"] * 100
        plt.plot(
            prune_axis,
            res["accuracies"],
            marker=markers[idx % len(markers)],
            label=res["display_name"],
            linewidth=2,
        )
    plt.xlabel("Prune Percentage (%)")
    plt.ylabel("Test Accuracy")
    plt.ylim(0.3, 0.9)
    plt.grid(True, linestyle="--", linewidth=0.5, alpha=0.7)
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_path, dpi=300)
    plt.close()


def plot_resource_usage(reference_metrics: Dict, output_path: Path):
    prune_axis = [f"{int(p * 100)}%" for p in reference_metrics["prune_percentages"]]
    registers = np.array(reference_metrics["registers"]) / 1000
    multipliers = np.array(reference_metrics["multipliers"]) / 1000
    additions = np.array(reference_metrics["additions"]) / 1000
    plt.figure(figsize=(8, 6))
    x_positions = np.arange(len(prune_axis))
    bottom_registers = registers
    bottom_multipliers = registers + multipliers
    plt.bar(x_positions, registers, label="Register", color="#b5e48c")
    plt.bar(x_positions, multipliers, bottom=registers, label="Multipliers", color="#f4a261")
    plt.bar(x_positions, additions, bottom=bottom_multipliers, label="Adders", color="#74c0e3")
    plt.xticks(x_positions, prune_axis)
    plt.xlabel("Prune Percentage (%)")
    plt.ylabel("Resources Used (in thousands)")
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_path, dpi=300)
    plt.close()


def parse_args():
    parser = argparse.ArgumentParser(description="Run training and pruning for intrusion datasets.")
    parser.add_argument(
        "--dataset",
        choices=["nsl-kdd", "unsw-nb15", "ton-iot", "all"],
        default="all",
        help="Select which dataset pipeline to run.",
    )
    parser.add_argument(
        "--data-root",
        default=str(DATASET_ROOT),
        help="Directory that contains the dataset CSV files.",
    )
    parser.add_argument("--epochs", type=int, default=DEFAULT_EPOCHS)
    parser.add_argument("--batch-size", type=int, default=DEFAULT_BATCH_SIZE)
    parser.add_argument("--validation-split", type=float, default=DEFAULT_VAL_SPLIT)
    parser.add_argument(
        "--verbose-fixed-point",
        action="store_true",
        help="Print detailed logs while running the fixed-point simulation.",
    )
    return parser.parse_args()


def run_dataset(cfg: DatasetConfig, args, dataset_path: Path):
    print(f"\n=== Running pipeline for {cfg.display_name} ===")
    X, y = cfg.loader(str(dataset_path))
    x_train, x_test, y_train, y_test = select_and_scale_features(X, y)
    model = build_base_model(x_train.shape[1])
    model.fit(
        x_train,
        y_train,
        epochs=args.epochs,
        batch_size=args.batch_size,
        validation_split=args.validation_split,
        verbose=0,
    )
    loss, acc = model.evaluate(x_test, y_test, verbose=0)
    print(f"{cfg.display_name} -> Test Accuracy: {acc:.4f}, Loss: {loss:.4f}")
    model_save_path = MODEL_DIR / cfg.model_filename
    model.save(model_save_path)
    metrics = collect_pruning_metrics(
        model, x_test, y_test, cfg, verbose_fixed_point=args.verbose_fixed_point
    )
    return metrics


def main():
    args = parse_args()
    dataset_configs = get_dataset_configs()
    data_root = Path(args.data_root).expanduser().resolve()
    selected_keys = list(dataset_configs.keys()) if args.dataset == "all" else [args.dataset]
    results = []
    for key in selected_keys:
        cfg = dataset_configs[key]
        dataset_path = resolve_dataset_path(cfg, data_root)
        metrics = run_dataset(cfg, args, dataset_path)
        results.append(metrics)
    if args.dataset == "all" and results:
        accuracy_plot = RESULTS_DIR / "combined_accuracy_vs_pruning.png"
        resource_plot = RESULTS_DIR / "operationsRequired.png"
        plot_accuracy_vs_pruning(results, accuracy_plot)
        plot_resource_usage(results[0], resource_plot)
        print(f"\nSaved plots to {accuracy_plot} and {resource_plot}")


if __name__ == "__main__":
    main()
