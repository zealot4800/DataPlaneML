# MlCompiler

`ml_compiler` is a small C++ utility that reads a JSON manifest exported from a trained Keras model and emits the two overlay files used by the IPSA neuron pipeline: `metadata_context.json` and `processor.json`.

The current allocator balances the Dense layers across all 12 processor stages so each stage carries only a small chunk of the model.

Weights and biases are emitted as signed 16-bit integer values with `weights_quantization_bits = 16` and `biases_quantization_bits = 16`.

## Build

The target is added to the normal `HwFramework-IPSA` CMake build:

```bash
cmake -S /home/zealot/ResarchWork/DataPlaneML/HwFramework-IPSA -B /home/zealot/ResarchWork/DataPlaneML/HwFramework-IPSA/build_ml
cmake --build /home/zealot/ResarchWork/DataPlaneML/HwFramework-IPSA/build_ml --target ml_compiler -j
```

## Run

```bash
./build_ml/bin/ml_compiler <model_manifest.json> [output_dir]
```

## Input

The compiler expects a JSON manifest with a top-level `layers` array. Each Dense layer entry should provide:

```text
{
	"model_name": "nsl_kdd_model",
	"source_model": "SwFramework/dataset/saved_models/nsl_kdd_model.keras",
	"layers": [
		{
			"type": "Dense",
			"input_neurons": 16,
			"output_neurons": 16,
			"activation": "relu",
			"weights": [[...]],
			"biases": [...]
		}
	]
}
```

## Output

The generated files are:

- `metadata_context.json` with `metadata` and `neuron_primitive_contexts`
- `processor.json` with the processor overlays

The balanced plan used for the NSL-KDD model splits the 16->8->4->1 Dense stack into 12 active stages, so every processor entry in `processor.json` does real work.

If you pass a legacy text summary instead of JSON, the tool still writes the old flat report mode for compatibility.