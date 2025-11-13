#ifndef RECONF_SWITCH_IPSA_NEURON_PRIMITIVE_H
#define RECONF_SWITCH_IPSA_NEURON_PRIMITIVE_H

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <glog/logging.h>

#include "defs.h"

enum class ActivationFunction {
    NONE = 0,
    RELU = 1,
    SIGMOID = 2
};

struct NeuronPrimitiveContext {
    uint16_t context_id = 0;
    uint32_t num_inputs = 0;
    uint32_t num_neurons = 0;
    uint32_t input_bitwidth = 16;
    uint32_t output_bitwidth = 16;
    bool inputs_are_signed = true;
    bool weights_are_signed = true;
    std::vector<int32_t> weights;
    std::vector<int32_t> biases;
    ActivationFunction activation = ActivationFunction::NONE;
};

class NeuronPrimitiveManager {
public:
    static NeuronPrimitiveManager& instance();

    bool upsert_context(const NeuronPrimitiveContext& ctx);
    const NeuronPrimitiveContext* get_context(uint16_t context_id) const;
    bool erase_context(uint16_t context_id);
    void clear();

private:
    NeuronPrimitiveManager() = default;
    NeuronPrimitiveManager(const NeuronPrimitiveManager&) = delete;
    NeuronPrimitiveManager& operator=(const NeuronPrimitiveManager&) = delete;

    mutable std::mutex mutex_;
    std::unordered_map<uint16_t, NeuronPrimitiveContext> contexts_;
};

Data execute_neuron_primitive(const NeuronPrimitiveContext& ctx, const Data& features);

class SigmoidTableManager {
public:
    static SigmoidTableManager& instance();

    bool load_table(int32_t min_input,
                    int32_t max_input,
                    uint32_t value_bitwidth,
                    uint32_t input_multiplier,
                    const std::vector<uint32_t>& values);

    void clear();
    bool is_loaded() const;
    uint32_t lookup(int32_t input) const;
    uint32_t value_bitwidth() const;
    uint32_t input_multiplier() const;

private:
    SigmoidTableManager() = default;
    SigmoidTableManager(const SigmoidTableManager&) = delete;
    SigmoidTableManager& operator=(const SigmoidTableManager&) = delete;

    mutable std::mutex mutex_;
    int32_t min_input_ = 0;
    int32_t max_input_ = -1;
    uint32_t value_bitwidth_ = 0;
    uint32_t input_multiplier_ = 1;
    std::vector<uint32_t> table_;
};

Data apply_sigmoid_lookup(const Data& input, uint32_t desired_bitwidth);


#endif // RECONF_SWITCH_IPSA_NEURON_PRIMITIVE_H
