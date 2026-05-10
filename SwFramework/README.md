# Running `run_datasets.py`

This walkthrough explains how to train, prune, and plot results across the NSL-KDD, UNSW-NB15, and ToN-IoT datasets using the unified pipeline.

## 1. Environment setup
- Use Python 3.10+ with virtualenv/conda if possible.
- From the repository root, install dependencies:
  ```bash
  cd /home/zealot/IPSA
  pip install -r Software/requirements.txt
  ```
- TensorFlow is required; if installation fails on CPU-only machines, install `tensorflow-cpu` instead.

## 2. Prepare datasets
Place the CSV files under `Software/dataset/` using the expected names:

| Dataset    | Default file name                    |
|-----------|--------------------------------------|
| NSL-KDD   | `nsl-kdd-dataset.csv`                |
| UNSW-NB15 | `UNSW_NB15_training-set.csv`         |
| ToN-IoT   | `toniot_dataset.csv`                 |

## 3. Run a single dataset
```
python Software/src/run_datasets.py --dataset unsw-nb15
```
Optional flags:
- `--epochs`, `--batch-size`, `--validation-split`
- `--verbose-fixed-point` (prints detailed fixed-point inference logs)

Models and layer summaries are saved in `Software/dataset/saved_models/`.

## 4. Run all datasets and generate plots
```
python Software/src/run_datasets.py --dataset all
```
This sequentially processes NSL-KDD, UNSW-NB15, and ToN-IoT, then writes:
- `Software/Results/combined_accuracy_vs_pruning.png`
- `Software/Results/operationsRequired.png`

## 5. Neuron primitive cost insights
**Primitive Mapping Stage.** Maps pruned models to hardware, balancing cost and system efficiency. Results is present in `Results/Normalized_resource.png`.

**Primitive Latency.** Latency increases with primitive size, requiring a hardware/latency budget trade-off. Results are in 'Results/latency_vs_pruning.png'.

## 6. Compile trained weights for hardware
After training, compile the exported dense-layer summaries into hardware overlays:
```bash
python src/weight_compiler.py --dataset all
```

This writes per-dataset outputs under `SwFramework/hw-Compiler/<dataset>/`:
- `metadata_context.json` with metadata layout, neuron contexts, and compiler contract details
- `processor.json` with a weight-stationary ping-pong schedule
- `neuron_primitive_contexts.json` for direct merge into the hardware tables
- `sigmoid_table.json` sized to the calibrated final-logit range
- `manifest.json` with layer summary and calibration details
- `context_*_weights.coe` / `context_*_weights.mif`
- `context_*_biases.coe` / `context_*_biases.mif`

The compiler reads the exported `*_best_model_layers.txt` files, so it does not require TensorFlow at compile time.

By default the compiler now emits smaller physical primitives by tiling the neuron dimension (`tile_neurons=4`) and chaining those tiles with a weight-stationary ping-pong schedule. Example: a logical `16 -> 11` layer is emitted as three contexts `16x4`, `16x4`, and `16x3`.

Input-side temporal folding is still intentionally disabled. The current dataplane `sum_block` only accumulates unsigned packed chunks, and `npb` does not emit signed partial sums, so splitting one neuron across multiple input tiles would be numerically wrong until the primitive contract is extended.

To install a compiled overlay straight into the dataplane tables for one dataset:
```bash
python src/weight_compiler.py --dataset unsw-nb15 --install-hardware-tables
```
