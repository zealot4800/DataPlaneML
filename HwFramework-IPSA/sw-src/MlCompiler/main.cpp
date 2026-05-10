#include "../../rP4-compiler/json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

constexpr int kNeuronBitwidth = 16;
constexpr int kProcessorCount = 12;
constexpr int kClusterCount = 4;
constexpr int kProcessorsPerCluster = kProcessorCount / kClusterCount;
constexpr int kOverlayProcessorCount = 16;
constexpr int kMatcherThreadLimit = 16;
constexpr int kActionSlotLimit = 32;
constexpr int kHiddenHeaderId = 31;
constexpr int kInputHeaderId = 30;
constexpr int kFinalHeaderId = 28;
constexpr long double kWeightScale = 32768.0L;

struct DenseLayer {
    int index = 0;
    int input_neurons = 0;
    int output_neurons = 0;
    std::string activation = "none";
    std::vector<std::vector<double>> weights;
    std::vector<double> biases;
};

struct StageMapping {
    int stage_id = 0;
    int processor_id = 0;
    int cluster_id = 0;
    int local_processor_id = 0;
    int layer_index = 0;
    int chunk_index = 0;
    int input_header_id = 0;
    int output_header_id = 0;
    int input_start_bits = 0;
    int output_start_bits = 0;
    int chunk_begin = 0;
    int chunk_end = 0;
};

bool is_json_input(const std::string &path) {
    return path.size() >= 5 && path.substr(path.size() - 5) == ".json";
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

int ceil_div(int value, int divisor) {
    return (value + divisor - 1) / divisor;
}

std::vector<int> allocate_stage_counts(const std::vector<DenseLayer> &layers) {
    if (layers.empty()) {
        throw std::runtime_error("model manifest does not contain any Dense layers");
    }

    if (static_cast<int>(layers.size()) > kProcessorCount) {
        throw std::runtime_error("layer count exceeds available processor stages");
    }

    int total_outputs = 0;
    for (const auto &layer : layers) {
        total_outputs += layer.output_neurons;
    }
    if (total_outputs <= 0) {
        throw std::runtime_error("model contains no output neurons");
    }

    const int extra_stages = kProcessorCount - static_cast<int>(layers.size());
    std::vector<int> counts(layers.size(), 1);
    std::vector<double> fractional(layers.size(), 0.0);
    int assigned_extra = 0;

    for (size_t i = 0; i < layers.size(); ++i) {
        const double ideal_extra = static_cast<double>(layers[i].output_neurons) * static_cast<double>(extra_stages)
                                 / static_cast<double>(total_outputs);
        const int floor_extra = static_cast<int>(std::floor(ideal_extra));
        counts[i] += floor_extra;
        fractional[i] = ideal_extra - static_cast<double>(floor_extra);
        assigned_extra += floor_extra;
    }

    int remaining = extra_stages - assigned_extra;
    while (remaining > 0) {
        size_t best_index = 0;
        for (size_t i = 1; i < layers.size(); ++i) {
            if (fractional[i] > fractional[best_index]) {
                best_index = i;
            }
        }
        ++counts[best_index];
        fractional[best_index] = -1.0;
        --remaining;
    }

    return counts;
}

std::vector<int> split_evenly(int total, int chunks) {
    if (chunks <= 0) {
        throw std::runtime_error("chunk count must be positive");
    }
    if (total < chunks) {
        throw std::runtime_error("cannot split fewer neurons than requested chunks");
    }

    std::vector<int> sizes(chunks, total / chunks);
    int remainder = total % chunks;
    for (int i = 0; i < chunks; ++i) {
        if (remainder > 0) {
            ++sizes[i];
            --remainder;
        }
    }
    return sizes;
}

std::vector<double> parse_number_list(const json &value) {
    std::vector<double> numbers;
    for (const auto &entry : value) {
        numbers.push_back(entry.get<double>());
    }
    return numbers;
}

int16_t quantize_to_int16(long double value) {
    const long double scaled = std::llround(value * kWeightScale);
    const long double min_value = static_cast<long double>(std::numeric_limits<int16_t>::min());
    const long double max_value = static_cast<long double>(std::numeric_limits<int16_t>::max());
    const long double clamped = std::max(min_value, std::min(max_value, scaled));
    return static_cast<int16_t>(clamped);
}

std::vector<std::vector<double>> parse_weight_matrix(const json &value) {
    std::vector<std::vector<double>> matrix;
    for (const auto &row : value) {
        matrix.push_back(parse_number_list(row));
    }
    return matrix;
}

DenseLayer parse_dense_layer(const json &layer_json, int layer_index) {
    DenseLayer layer;
    layer.index = layer_json.value("index", layer_index);
    layer.input_neurons = layer_json.at("input_neurons").get<int>();
    layer.output_neurons = layer_json.at("output_neurons").get<int>();
    layer.activation = to_lower(layer_json.value("activation", std::string("none")));

    if (!layer_json.contains("weights") || !layer_json.at("weights").is_array()) {
        throw std::runtime_error("layer " + std::to_string(layer.index) + " missing weight matrix");
    }
    if (!layer_json.contains("biases") || !layer_json.at("biases").is_array()) {
        throw std::runtime_error("layer " + std::to_string(layer.index) + " missing bias vector");
    }

    layer.weights = parse_weight_matrix(layer_json.at("weights"));
    layer.biases = parse_number_list(layer_json.at("biases"));

    if (static_cast<int>(layer.weights.size()) != layer.input_neurons) {
        throw std::runtime_error("layer " + std::to_string(layer.index) + " weight row count mismatch");
    }
    for (const auto &row : layer.weights) {
        if (static_cast<int>(row.size()) != layer.output_neurons) {
            throw std::runtime_error("layer " + std::to_string(layer.index) + " weight column count mismatch");
        }
    }
    if (static_cast<int>(layer.biases.size()) != layer.output_neurons) {
        throw std::runtime_error("layer " + std::to_string(layer.index) + " bias count mismatch");
    }

    return layer;
}

std::vector<DenseLayer> load_model_manifest(const std::string &input_path, json &root_out) {
    std::ifstream input(input_path);
    if (!input.is_open()) {
        throw std::runtime_error("unable to open input file: " + input_path);
    }

    json root;
    input >> root;
    root_out = root;

    if (!root.contains("layers") || !root.at("layers").is_array()) {
        throw std::runtime_error("model manifest must contain a layers array");
    }

    std::vector<DenseLayer> layers;
    int index = 0;
    for (const auto &layer_json : root.at("layers")) {
        const std::string layer_type = to_lower(layer_json.value("type", std::string("dense")));
        if (layer_type != "dense") {
            continue;
        }
        layers.push_back(parse_dense_layer(layer_json, index));
        ++index;
    }

    return layers;
}

json make_default_matcher() {
    return json::array({{
        {"id", 0},
        {"match_type", "EXACT"},
        {"miss_action_id", 0},
        {"no_table", 1},
        {"field_infos", json::array()},
        {"key_memory", json::array()}
    }});
}

std::vector<StageMapping> build_stage_mappings(const std::vector<DenseLayer> &layers) {
    const std::vector<int> stage_counts = allocate_stage_counts(layers);
    std::vector<StageMapping> mappings;
    int stage_cursor = 0;
    int hidden_input_cursor_bits = 0;

    for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
        const auto &layer = layers[layer_index];
        const int chunk_count = stage_counts.at(layer_index);
        if (stage_cursor + chunk_count > kProcessorCount) {
            throw std::runtime_error("model requires more than 12 processor stages");
        }

        const int input_header_id = (layer_index == 0) ? kInputHeaderId : kHiddenHeaderId;
        const int output_header_id = (layer_index + 1 == layers.size()) ? kFinalHeaderId : kHiddenHeaderId;
        const int input_start_bits = (layer_index == 0) ? 0 : hidden_input_cursor_bits;
        const int output_start_bits = (layer_index + 1 == layers.size()) ? 0 : hidden_input_cursor_bits;
        const auto chunk_sizes = split_evenly(layer.output_neurons, chunk_count);
        int chunk_offset = 0;

        for (int chunk = 0; chunk < chunk_count; ++chunk) {
            const int chunk_size = chunk_sizes.at(chunk);
            const int processor_id = stage_cursor + chunk;
            StageMapping mapping;
            mapping.stage_id = processor_id;
            mapping.processor_id = processor_id;
            mapping.cluster_id = processor_id / kProcessorsPerCluster;
            mapping.local_processor_id = processor_id % kProcessorsPerCluster;
            mapping.layer_index = layer.index;
            mapping.chunk_index = chunk;
            mapping.input_header_id = input_header_id;
            mapping.output_header_id = output_header_id;
            mapping.input_start_bits = input_start_bits;
            mapping.output_start_bits = output_start_bits + chunk_offset * kNeuronBitwidth;
            mapping.chunk_begin = chunk_offset;
            mapping.chunk_end = chunk_offset + chunk_size - 1;
            mappings.push_back(mapping);
            chunk_offset += chunk_size;
        }

        stage_cursor += chunk_count;
        if (layer_index + 1 != layers.size()) {
            hidden_input_cursor_bits += layer.output_neurons * kNeuronBitwidth;
        }
    }

    return mappings;
}

std::vector<double> flatten_chunk_weights(const DenseLayer &layer, int chunk_begin, int chunk_end) {
    std::vector<double> flat;
    for (int input_index = 0; input_index < layer.input_neurons; ++input_index) {
        for (int output_index = chunk_begin; output_index <= chunk_end; ++output_index) {
            flat.push_back(layer.weights.at(input_index).at(output_index));
        }
    }
    return flat;
}

json build_neuron_contexts(const std::vector<DenseLayer> &layers) {
    const std::vector<int> stage_counts = allocate_stage_counts(layers);
    json contexts = json::array();
    int context_id = 0;

    for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
        const auto &layer = layers[layer_index];
        const int chunk_count = stage_counts.at(layer_index);
        const auto chunk_sizes = split_evenly(layer.output_neurons, chunk_count);
        int chunk_offset = 0;
        for (int chunk = 0; chunk < chunk_count; ++chunk) {
            const int begin = chunk_offset;
            const int end = chunk_offset + chunk_sizes.at(chunk) - 1;
            json ctx;
            ctx["id"] = context_id++;
            ctx["num_inputs"] = layer.input_neurons;
            ctx["num_neurons"] = end - begin + 1;
            ctx["input_bitwidth"] = kNeuronBitwidth;
            ctx["output_bitwidth"] = kNeuronBitwidth;
            ctx["output_shift"] = 0;
            ctx["inputs_signed"] = true;
            ctx["weights_signed"] = true;
            ctx["weights_quantization_bits"] = 16;
            ctx["biases_quantization_bits"] = 16;
            ctx["weights_quantization_scale"] = static_cast<int>(kWeightScale);
            ctx["biases_quantization_scale"] = static_cast<int>(kWeightScale);
            ctx["activation"] = layer.activation;
            const auto raw_weights = flatten_chunk_weights(layer, begin, end);
            json weights = json::array();
            for (const auto value : raw_weights) {
                weights.push_back(quantize_to_int16(static_cast<long double>(value)));
            }
            json biases = json::array();
            for (int bias_index = begin; bias_index <= end; ++bias_index) {
                biases.push_back(quantize_to_int16(static_cast<long double>(layer.biases.at(bias_index))));
            }
            ctx["weights"] = weights;
            ctx["biases"] = biases;
            contexts.push_back(ctx);
            chunk_offset += chunk_sizes.at(chunk);
        }
    }

    return contexts;
}

json build_metadata_context(const std::vector<DenseLayer> &layers, const json &manifest_root) {
    const int input_bits = layers.front().input_neurons * kNeuronBitwidth;
    int hidden_bits = 0;
    for (size_t i = 0; i + 1 < layers.size(); ++i) {
        hidden_bits += layers[i].output_neurons * kNeuronBitwidth;
    }
    const int final_bits = layers.back().output_neurons * kNeuronBitwidth;

    json metadata = json::array();
    metadata.push_back({{"id", kInputHeaderId}, {"length", input_bits}, {"offset", 0}});
    metadata.push_back({{"id", kHiddenHeaderId}, {"length", hidden_bits}, {"offset", input_bits}});
    metadata.push_back({{"id", 29}, {"length", final_bits}, {"offset", input_bits + hidden_bits}});
    metadata.push_back({{"id", kFinalHeaderId}, {"length", 32}, {"offset", input_bits + hidden_bits + final_bits}});

    json root;
    root["model_name"] = manifest_root.value("model_name", std::string("unknown_model"));
    root["source_model"] = manifest_root.value("source_model", std::string(""));
    root["metadata"] = metadata;
    root["neuron_primitive_contexts"] = build_neuron_contexts(layers);
    return root;
}

json build_processor_stage(const StageMapping &mapping,
                           const DenseLayer &layer,
                           int context_id,
                           bool is_final_layer) {
    const int output_bits = (mapping.chunk_end - mapping.chunk_begin + 1) * kNeuronBitwidth;
    const int input_bits = layer.input_neurons * kNeuronBitwidth;
    const int destination_header = is_final_layer ? kFinalHeaderId : kHiddenHeaderId;

    json primitive = {
        {"lvalue", {
            {"header_id", destination_header},
            {"field_internal_offset", mapping.output_start_bits},
            {"field_length", output_bits},
            {"type", "FIELD"}
        }},
        {"rvalue", {
            {"type", "EXPR"},
            {"value", {
                {"op", "npb"},
                {"left", {
                    {"type", "FIELD"},
                    {"value", {
                        {"header_id", mapping.input_header_id},
                        {"field_internal_offset", mapping.input_start_bits},
                        {"field_length", input_bits},
                        {"type", "FIELD"}
                    }}
                }},
                {"right", {{"type", "CONST"}, {"value", context_id}}}
            }}
        }}
    };

    json processor;
    processor["id"] = mapping.processor_id;
    processor["parser"] = {{"levels", json::array()}, {"parser_level", 0}};
    processor["matcher"] = make_default_matcher();
    processor["executor"] = json::array({{{"id", 0}, {"parameter_num", 0}, {"primitives", json::array({primitive})}}});
    processor["metadata"] = json::array();
    processor["next_proc"] = is_final_layer ? -1 : mapping.processor_id + 1;
    return processor;
}

json build_processor_overlay(const std::vector<DenseLayer> &layers, const std::vector<StageMapping> &mappings) {
    json root;
    int context_id = 0;
    size_t mapping_index = 0;

    for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
        const auto &layer = layers[layer_index];
        const int chunk_count = allocate_stage_counts(layers).at(layer_index);
        for (int chunk = 0; chunk < chunk_count; ++chunk, ++mapping_index, ++context_id) {
            const auto &mapping = mappings.at(mapping_index);
            root["processor_" + std::to_string(mapping.processor_id)] =
                build_processor_stage(mapping, layer, context_id, layer_index + 1 == layers.size());
        }
    }

    for (int proc = kProcessorCount; proc < kOverlayProcessorCount; ++proc) {
        const std::string key = "processor_" + std::to_string(proc);
        if (!root.contains(key)) {
            root[key] = {
                {"id", proc},
                {"parser", {{"levels", json::array()}, {"parser_level", 0}}},
                {"matcher", make_default_matcher()},
                {"executor", json::array()},
                {"metadata", json::array()},
                {"next_proc", -1}
            };
        }
    }

    return root;
}

void write_json_file(const std::string &path, const json &value) {
    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("unable to open output file: " + path);
    }
    output << value.dump(2) << "\n";
}

std::vector<std::pair<int, int>> parse_text_layers(const std::string &input_path) {
    std::ifstream input(input_path);
    if (!input.is_open()) {
        throw std::runtime_error("unable to open input file: " + input_path);
    }

    std::vector<std::pair<int, int>> layers;
    std::string line;
    const std::regex layer_regex(
        R"(^Layer\s+(\d+)\s+\(Dense\):\s+Input Neurons = (\d+),\s+Output Neurons = (\d+),)",
        std::regex::ECMAScript);

    while (std::getline(input, line)) {
        std::smatch match;
        if (!std::regex_search(line, match, layer_regex)) {
            continue;
        }
        layers.emplace_back(std::stoi(match[2].str()), std::stoi(match[3].str()));
    }

    if (layers.empty()) {
        throw std::runtime_error("no Dense layer summaries were found in text mode");
    }

    return layers;
}

void write_legacy_report(const std::vector<std::pair<int, int>> &layers, const std::string &output_path) {
    std::ofstream output(output_path);
    if (!output.is_open()) {
        throw std::runtime_error("unable to open output file: " + output_path);
    }

    output << "Legacy summary mode\n";
    for (size_t i = 0; i < layers.size(); ++i) {
        output << "Layer " << i << ": inputs=" << layers[i].first
               << " outputs=" << layers[i].second << "\n";
    }
}

void print_usage() {
    std::cerr << "Usage:\n"
              << "  ml_compiler <model_manifest.json> [output_dir]\n"
              << "  ml_compiler <layer_summary.txt> [output_report.txt]  # legacy fallback\n";
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        print_usage();
        return 1;
    }

    const std::string input_path = argv[1];
    const bool json_mode = is_json_input(input_path);

    try {
        if (json_mode) {
            json manifest_root;
            const auto layers = load_model_manifest(input_path, manifest_root);
            const auto mappings = build_stage_mappings(layers);
            const auto metadata_context = build_metadata_context(layers, manifest_root);
            const auto processor_overlay = build_processor_overlay(layers, mappings);

            const std::string output_dir = (argc == 3)
                ? argv[2]
                : std::string();
            const std::string metadata_path = output_dir.empty()
                ? std::string("metadata_context.json")
                : output_dir + "/metadata_context.json";
            const std::string processor_path = output_dir.empty()
                ? std::string("processor.json")
                : output_dir + "/processor.json";

            write_json_file(metadata_path, metadata_context);
            write_json_file(processor_path, processor_overlay);
            std::cout << "wrote " << metadata_path << " and " << processor_path << std::endl;
        } else {
            const std::string output_path = (argc == 3) ? argv[2] : input_path + ".ml_map.txt";
            const auto layers = parse_text_layers(input_path);
            write_legacy_report(layers, output_path);
            std::cout << "wrote legacy report to " << output_path << std::endl;
        }
    } catch (const std::exception &error) {
        std::cerr << "ml_compiler error: " << error.what() << std::endl;
        return 1;
    }

    return 0;
}