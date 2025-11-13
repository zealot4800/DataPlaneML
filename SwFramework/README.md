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
