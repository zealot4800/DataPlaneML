#include "neuron_primitive.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

constexpr uint32_t kMaxSupportedBitwidth = 32;
constexpr uint32_t kFixedPointBitwidth = 16;
constexpr uint32_t kFixedPointShift = 16;

Data make_zero_data(uint32_t bit_length) {
    Data zero{};
    zero.data_len = static_cast<int>(bit_length);
    int byte_len = static_cast<int>(std::ceil(bit_length / 8.0));
    if (byte_len <= 0) {
        zero.val = nullptr;
        return zero;
    }
    zero.val = new uint8_t[byte_len];
    std::memset(zero.val, 0, byte_len);
    return zero;
}

int64_t extract_bits(const uint8_t* buffer,
                     uint32_t start_bit,
                     uint32_t bitwidth,
                     bool is_signed,
                     uint32_t total_bits) {
    if (bitwidth == 0 || buffer == nullptr) {
        return 0;
    }
    if (start_bit + bitwidth > total_bits) {
        return 0;
    }

    int64_t value = 0;
    for (uint32_t offset = 0; offset < bitwidth; ++offset) {
        uint32_t current_bit = start_bit + offset;
        uint32_t byte_index = current_bit / 8;
        uint32_t bit_index = 7 - (current_bit % 8);
        uint8_t bit = static_cast<uint8_t>((buffer[byte_index] >> bit_index) & 0x1);
        value = (value << 1) | bit;
    }

    if (is_signed && bitwidth > 0) {
        const int64_t sign_bit = 1LL << (bitwidth - 1);
        if (value & sign_bit) {
            const int64_t mask = (~0LL) << bitwidth;
            value |= mask;
        }
    }

    return value;
}

bool decode_value_vector(const Data& data,
                         uint32_t bitwidth,
                         uint32_t expected_count,
                         bool is_signed,
                         std::vector<int32_t>& out) {
    if (bitwidth == 0) {
        LOG(ERROR) << "Neuron primitive: bitwidth cannot be zero.";
        return false;
    }
    if (bitwidth > kMaxSupportedBitwidth) {
        LOG(ERROR) << "Neuron primitive: unsupported bitwidth " << bitwidth
                   << ", max supported is " << kMaxSupportedBitwidth;
        return false;
    }

    const uint32_t total_bits = static_cast<uint32_t>(data.data_len);
    const uint32_t required_bits = bitwidth * expected_count;
    if (required_bits > total_bits) {
        LOG(ERROR) << "Neuron primitive: insufficient bits in feature blob. Expected "
                   << required_bits << ", actual " << total_bits;
        return false;
    }

    out.clear();
    out.reserve(expected_count);
    for (uint32_t idx = 0; idx < expected_count; ++idx) {
        const uint32_t start_bit = idx * bitwidth;
        int64_t value = extract_bits(data.val, start_bit, bitwidth, is_signed, total_bits);
        out.push_back(static_cast<int32_t>(value));
    }
    return true;
}

Data pack_unsigned_values(const std::vector<uint64_t>& values, uint32_t bitwidth) {
    if (values.empty()) {
        return make_zero_data(0);
    }

    const uint32_t total_bits = bitwidth * static_cast<uint32_t>(values.size());
    Data packed{};
    packed.data_len = static_cast<int>(total_bits);
    const int byte_len = static_cast<int>(std::ceil(total_bits / 8.0));
    packed.val = new uint8_t[byte_len];
    std::memset(packed.val, 0, byte_len);

    for (size_t idx = 0; idx < values.size(); ++idx) {
        uint64_t value = values[idx];
        for (uint32_t bit = 0; bit < bitwidth; ++bit) {
            const uint32_t global_bit = static_cast<uint32_t>(idx) * bitwidth + bit;
            const uint32_t byte_index = global_bit / 8;
            const uint32_t bit_index = 7 - (global_bit % 8);
            const uint64_t mask = 1ULL << (bitwidth - 1 - bit);
            const uint8_t bit_value = static_cast<uint8_t>((value & mask) ? 1 : 0);
            packed.val[byte_index] |= static_cast<uint8_t>(bit_value << bit_index);
        }
    }

    return packed;
}

uint64_t mask_for_width(uint32_t bitwidth) {
    if (bitwidth == 0) {
        return std::numeric_limits<uint32_t>::max();
    }
    if (bitwidth >= 32) {
        return std::numeric_limits<uint32_t>::max();
    }
    return (1ULL << bitwidth) - 1ULL;
}

uint64_t clamp_to_width(int64_t value, uint32_t bitwidth) {
    if (bitwidth == 0) {
        bitwidth = 32;
    }
    int64_t min_value = 0;
    int64_t max_value = static_cast<int64_t>(mask_for_width(bitwidth));
    int64_t clamped = std::min<int64_t>(std::max<int64_t>(value, min_value), max_value);
    return static_cast<uint64_t>(clamped);
}

uint64_t scale_probability(double probability, uint32_t bitwidth) {
    if (bitwidth == 0) {
        bitwidth = 16;
    }
    if (probability < 0.0) {
        probability = 0.0;
    } else if (probability > 1.0) {
        probability = 1.0;
    }
    long double max_val = static_cast<long double>(mask_for_width(bitwidth));
    return static_cast<uint64_t>(std::llround(probability * max_val));
}

uint64_t rescale_value(uint64_t value, uint32_t from_width, uint32_t to_width) {
    if (from_width == 0) {
        from_width = 16;
    }
    if (to_width == 0 || to_width == from_width) {
        return clamp_to_width(static_cast<int64_t>(value), to_width == 0 ? from_width : to_width);
    }
    long double from_max = static_cast<long double>(mask_for_width(from_width));
    long double to_max = static_cast<long double>(mask_for_width(to_width));
    if (from_max <= 0.0L) {
        return 0;
    }
    long double ratio = static_cast<long double>(value) / from_max;
    if (ratio < 0.0L) {
        ratio = 0.0L;
    } else if (ratio > 1.0L) {
        ratio = 1.0L;
    }
    return static_cast<uint64_t>(std::llround(ratio * to_max));
}

} // namespace

NeuronPrimitiveManager& NeuronPrimitiveManager::instance() {
    static NeuronPrimitiveManager manager;
    return manager;
}

SigmoidTableManager& SigmoidTableManager::instance() {
    static SigmoidTableManager manager;
    return manager;
}

bool SigmoidTableManager::load_table(int32_t min_input,
                                     int32_t max_input,
                                     uint32_t value_bitwidth,
                                     uint32_t input_multiplier,
                                     const std::vector<uint32_t>& values) {
    if (min_input > max_input) {
        LOG(ERROR) << "Sigmoid table: invalid range.";
        return false;
    }
    size_t expected = static_cast<size_t>(static_cast<int64_t>(max_input) - min_input + 1);
    if (values.size() != expected) {
        LOG(ERROR) << "Sigmoid table: expected " << expected
                   << " entries but received " << values.size();
        return false;
    }
    if (value_bitwidth == 0 || value_bitwidth > 32) {
        LOG(ERROR) << "Sigmoid table: unsupported output width " << value_bitwidth;
        return false;
    }
    if (input_multiplier == 0) {
        LOG(ERROR) << "Sigmoid table: input multiplier cannot be zero.";
        return false;
    }

    uint64_t mask = (value_bitwidth == 32)
            ? std::numeric_limits<uint32_t>::max()
            : ((1ull << value_bitwidth) - 1ull);
    for (auto v : values) {
        if (static_cast<uint64_t>(v) > mask) {
            LOG(ERROR) << "Sigmoid table: value " << v << " exceeds bitwidth " << value_bitwidth;
            return false;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    min_input_ = min_input;
    max_input_ = max_input;
    value_bitwidth_ = value_bitwidth;
    input_multiplier_ = input_multiplier;
    table_ = values;
    return true;
}

void SigmoidTableManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    table_.clear();
    min_input_ = 0;
    max_input_ = -1;
    value_bitwidth_ = 0;
}

bool SigmoidTableManager::is_loaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !table_.empty();
}

uint32_t SigmoidTableManager::lookup(int32_t input) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (table_.empty()) {
        LOG(ERROR) << "Sigmoid table: lookup attempted before load.";
        return 0;
    }
    long double scaled = static_cast<long double>(input) * static_cast<long double>(input_multiplier_);
    int64_t scaled_int = static_cast<int64_t>(std::llround(scaled));
    if (scaled_int < static_cast<int64_t>(min_input_)) {
        scaled_int = min_input_;
    } else if (scaled_int > static_cast<int64_t>(max_input_)) {
        scaled_int = max_input_;
    }
    size_t idx = static_cast<size_t>(scaled_int - min_input_);
    return table_[idx];
}

uint32_t SigmoidTableManager::value_bitwidth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return value_bitwidth_;
}

uint32_t SigmoidTableManager::input_multiplier() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return input_multiplier_;
}

Data apply_sigmoid_lookup(const Data& input, uint32_t desired_bitwidth) {
    auto &manager = SigmoidTableManager::instance();
    if (!manager.is_loaded()) {
        LOG(ERROR) << "Sigmoid table: not loaded.";
        return make_zero_data(desired_bitwidth == 0 ? 32 : desired_bitwidth);
    }

    const uint32_t bit_len = static_cast<uint32_t>(input.data_len == 0 ? 32 : input.data_len);
    const int64_t raw = extract_bits(input.val, 0, bit_len, true, bit_len);
    uint32_t width = desired_bitwidth == 0 ? manager.value_bitwidth() : desired_bitwidth;
    if (width == 0 || width > 32) {
        width = 32;
    }
    uint32_t lut_val = manager.lookup(static_cast<int32_t>(raw));
    uint32_t value = rescale_value(lut_val, manager.value_bitwidth(), width);
    std::vector<uint64_t> packed{value};
    return pack_unsigned_values(packed, width);
}

bool NeuronPrimitiveManager::upsert_context(const NeuronPrimitiveContext& ctx) {
    if (ctx.num_inputs == 0 || ctx.num_neurons == 0) {
        LOG(ERROR) << "Neuron primitive: context must specify non-zero inputs and neurons.";
        return false;
    }
    if (ctx.weights.size() != ctx.num_inputs * ctx.num_neurons) {
        LOG(ERROR) << "Neuron primitive: weights size mismatch. Expected "
                   << ctx.num_inputs * ctx.num_neurons << " but got "
                   << ctx.weights.size();
        return false;
    }
    if (ctx.biases.size() != ctx.num_neurons) {
        LOG(ERROR) << "Neuron primitive: biases size mismatch. Expected "
                   << ctx.num_neurons << " but got " << ctx.biases.size();
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    contexts_[ctx.context_id] = ctx;
    return true;
}

const NeuronPrimitiveContext* NeuronPrimitiveManager::get_context(uint16_t context_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(context_id);
    if (it == contexts_.end()) {
        return nullptr;
    }
    return &it->second;
}

bool NeuronPrimitiveManager::erase_context(uint16_t context_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return contexts_.erase(context_id) > 0;
}

void NeuronPrimitiveManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    contexts_.clear();
}

Data execute_neuron_primitive(const NeuronPrimitiveContext& ctx, const Data& features) {
    if (ctx.num_inputs == 0 || ctx.num_neurons == 0) {
        LOG(ERROR) << "Neuron primitive: invalid context dimensions.";
        return make_zero_data(32);
    }
    const uint32_t out_width = kFixedPointBitwidth;
    if (ctx.output_bitwidth != kFixedPointBitwidth && ctx.output_bitwidth != 0) {
        LOG(WARNING) << "Neuron primitive: forcing output bitwidth to "
                     << kFixedPointBitwidth << " bits instead of "
                     << ctx.output_bitwidth;
    }

    if (ctx.weights.size() != ctx.num_inputs * ctx.num_neurons ||
        ctx.biases.size() != ctx.num_neurons) {
        LOG(ERROR) << "Neuron primitive: context weight/bias mismatch.";
        return make_zero_data(32);
    }

    const uint32_t input_bitwidth = kFixedPointBitwidth;
    if (ctx.input_bitwidth != kFixedPointBitwidth && ctx.input_bitwidth != 0) {
        LOG(WARNING) << "Neuron primitive: forcing input bitwidth to "
                     << kFixedPointBitwidth << " bits instead of "
                     << ctx.input_bitwidth;
    }

    std::vector<int32_t> input_vector;
    if (!decode_value_vector(features,
                              input_bitwidth,
                              ctx.num_inputs,
                              ctx.inputs_are_signed,
                              input_vector)) {
        LOG(ERROR) << "Neuron primitive: unable to decode input vector.";
        return make_zero_data(ctx.num_neurons * out_width);
    }

    std::vector<int64_t> mac_outputs(ctx.num_neurons, 0);
    for (uint32_t neuron = 0; neuron < ctx.num_neurons; ++neuron) {
        int64_t acc = static_cast<int64_t>(ctx.biases[neuron]) << kFixedPointShift;
        for (uint32_t input_idx = 0; input_idx < ctx.num_inputs; ++input_idx) {
            const int64_t feature = static_cast<int64_t>(input_vector[input_idx]);
            const int64_t weight =
                static_cast<int64_t>(ctx.weights[neuron * ctx.num_inputs + input_idx]);
            acc += feature * weight;
        }
        mac_outputs[neuron] = acc;
    }

    std::vector<uint64_t> encoded_outputs;
    encoded_outputs.reserve(ctx.num_neurons);
    uint32_t pack_width = out_width;
    ActivationFunction activation = ctx.activation;

    switch (activation) {
        case ActivationFunction::SIGMOID: {
            auto &sigmoid_mgr = SigmoidTableManager::instance();
            if (!sigmoid_mgr.is_loaded()) {
                LOG(ERROR) << "Neuron primitive: sigmoid activation requested but "
                              "no lookup table is loaded.";
                return make_zero_data(ctx.num_neurons * pack_width);
            }
            uint32_t table_width = sigmoid_mgr.value_bitwidth();
            for (uint32_t i = 0; i < ctx.num_neurons; ++i) {
                int32_t scaled_input =
                    static_cast<int32_t>(mac_outputs[i] >> kFixedPointShift);
                uint32_t lookup_value = sigmoid_mgr.lookup(scaled_input);
                encoded_outputs.push_back(
                    rescale_value(lookup_value, table_width, pack_width));
            }
            break;
        }
        case ActivationFunction::RELU: {
            for (uint32_t i = 0; i < ctx.num_neurons; ++i) {
                int64_t val = mac_outputs[i] >> kFixedPointShift;
                if (val < 0) {
                    val = 0;
                }
                encoded_outputs.push_back(clamp_to_width(val, pack_width));
            }
            break;
        }
        case ActivationFunction::NONE:
        default: {
            for (uint32_t i = 0; i < ctx.num_neurons; ++i) {
                int64_t val = mac_outputs[i] >> kFixedPointShift;
                encoded_outputs.push_back(clamp_to_width(val, pack_width));
            }
            break;
        }
    }

    return pack_unsigned_values(encoded_outputs, pack_width);
}
