//
// Created by xilinx_0 on 1/26/22.
//

#ifndef RECONF_SWITCH_IPSA_CTRL_INIT_DATAPLANE_H
#define RECONF_SWITCH_IPSA_CTRL_INIT_DATAPLANE_H

#include "../api/controller_client.h"

#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cmath>

#include "json.hpp"

using json = nlohmann::json;
using namespace std;

//#define NO_CFG

FieldType get_field_type(std::string type) {
    std::unordered_map<std::string, FieldType> ft_map = {
            {"FIELD" , FieldType::FIELD},
            {"VALID", FieldType::VALID},
            {"HIT", FieldType::HIT},
            {"MISS", FieldType::MISS}
    };
    return ft_map[type];
}

ActivationFunction get_activation_function(const std::string &act) {
    std::string lowered = act;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c){ return std::tolower(c); });
    if (lowered == "relu") {
        return ActivationFunction::RELU;
    }
    if (lowered == "sigmoid") {
        return ActivationFunction::SIGMOID;
    }
    return ActivationFunction::NONE;
}

#ifdef NO_CFG
void extract_neuron_primitives(json j) {
#else
void extract_neuron_primitives(json j, api::CfgClient & cfg) {
#endif
    if (j.is_null()) {
        return;
    }

#ifdef NO_CFG
    NeuronPrimitiveManager::instance().clear();
#else
    cfg.clear_neuron_primitive_contexts();
#endif

    for (auto &ctx_json : j) {
        NeuronPrimitiveContext ctx{};
        ctx.context_id = static_cast<uint16_t>(ctx_json.at("id").get<int>());
        ctx.num_inputs = ctx_json.at("num_inputs").get<int>();
        ctx.num_neurons = ctx_json.at("num_neurons").get<int>();
        ctx.input_bitwidth = ctx_json.value("input_bitwidth", 16);
        ctx.output_bitwidth = ctx_json.value("output_bitwidth", 16);
        ctx.inputs_are_signed = ctx_json.value("inputs_signed", true);
        ctx.weights_are_signed = ctx_json.value("weights_signed", true);
        std::string activation_str = ctx_json.value("activation", std::string(""));
        if (!activation_str.empty()) {
            ctx.activation = get_activation_function(activation_str);
        } else {
            ctx.activation = ActivationFunction::RELU;
        }

        if (ctx_json.contains("weights")) {
            for (auto &weight : ctx_json.at("weights")) {
                ctx.weights.push_back(weight.get<int>());
            }
        }
        if (ctx_json.contains("biases")) {
            for (auto &bias : ctx_json.at("biases")) {
                ctx.biases.push_back(bias.get<int>());
            }
        }

        size_t expected_weights = static_cast<size_t>(ctx.num_inputs) * static_cast<size_t>(ctx.num_neurons);
        if (ctx.weights.size() != expected_weights) {
            LOG(ERROR) << "neuron primitive context " << ctx.context_id
                       << " weight count mismatch. expected " << expected_weights
                       << " actual " << ctx.weights.size();
            continue;
        }
        if (ctx.biases.size() != ctx.num_neurons) {
            LOG(ERROR) << "neuron primitive context " << ctx.context_id
                       << " bias count mismatch. expected " << ctx.num_neurons
                       << " actual " << ctx.biases.size();
            continue;
        }

#ifdef NO_CFG
        bool ok = NeuronPrimitiveManager::instance().upsert_context(ctx);
#else
        bool ok = cfg.load_neuron_primitive_context(ctx) == 1;
#endif
        std::cout << "neuron primitive context " << ctx.context_id
                  << (ok ? " loaded" : " failed") << std::endl;
    }
}

#ifdef NO_CFG
void extract_sigmoid_table(json j) {
#else
void extract_sigmoid_table(json j, api::CfgClient & cfg) {
#endif
    if (j.is_null()) {
        return;
    }

    uint32_t value_bitwidth = j.value("output_bitwidth", 16);
    double step = j.value("step", 1.0);
    if (step <= 0.0) {
        LOG(ERROR) << "sigmoid table: step must be positive";
        return;
    }
    double multiplier_d = 1.0 / step;
    uint32_t input_multiplier = static_cast<uint32_t>(std::llround(multiplier_d));
    if (input_multiplier == 0) {
        LOG(ERROR) << "sigmoid table: invalid step";
        return;
    }
    if (std::fabs(multiplier_d - static_cast<double>(input_multiplier)) > 1e-6) {
        LOG(ERROR) << "sigmoid table: step must be the reciprocal of an integer";
        return;
    }

    std::vector<std::pair<int32_t, uint32_t>> points;
    for (auto &entry : j.at("entries")) {
        double input_f = entry.at("input").get<double>();
        int32_t scaled_input = static_cast<int32_t>(std::llround(input_f * static_cast<double>(input_multiplier)));
        uint32_t value = entry.at("value").get<uint32_t>();
        points.emplace_back(scaled_input, value);
    }
    if (points.empty()) {
        LOG(ERROR) << "sigmoid table: no entries provided";
        return;
    }

    std::sort(points.begin(), points.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });

#ifdef NO_CFG
    SigmoidTableManager::instance().clear();
    std::vector<uint32_t> values;
    int32_t min_input = points.front().first;
    int32_t max_input = points.back().first;
    values.assign(static_cast<size_t>(max_input - min_input + 1), 0);
    bool ok = true;
    for (size_t i = 0; i < points.size(); ++i) {
        int32_t expected = min_input + static_cast<int32_t>(i);
        if (points[i].first != expected) {
            ok = false;
            break;
        }
        values[i] = points[i].second;
    }
    if (!ok) {
        LOG(ERROR) << "sigmoid table: invalid entries";
        return;
    }
    SigmoidTableManager::instance().load_table(min_input, max_input, value_bitwidth, input_multiplier, values);
#else
    cfg.clear_sigmoid_table();
    int rc = cfg.load_sigmoid_table(static_cast<int>(value_bitwidth), input_multiplier, points);
    std::cout << "sigmoid table load: " << rc << std::endl;
#endif
}

#ifdef NO_CFG
void extract_exp_table(json j) {
#else
void extract_exp_table(json j, api::CfgClient & cfg) {
#endif
    if (j.is_null()) {
        return;
    }

    double step = j.value("step", 0.01);
    if (step <= 0.0) {
        LOG(ERROR) << "exp table: step must be positive";
        return;
    }
    double multiplier_d = 1.0 / step;
    uint32_t input_multiplier = static_cast<uint32_t>(std::llround(multiplier_d));
    if (input_multiplier == 0) {
        LOG(ERROR) << "exp table: invalid step";
        return;
    }
    if (std::fabs(multiplier_d - static_cast<double>(input_multiplier)) > 1e-6) {
        LOG(ERROR) << "exp table: step must be the reciprocal of an integer";
        return;
    }

    uint32_t value_scale = j.value("value_scale", 4096);
    if (value_scale == 0) {
        LOG(ERROR) << "exp table: value_scale must be positive";
        return;
    }

    std::vector<std::pair<int32_t, uint32_t>> entries;
    for (auto &entry : j.at("entries")) {
        double input_f = entry.at("input").get<double>();
        double value_f = entry.at("value").get<double>();
        int32_t scaled_input = static_cast<int32_t>(std::llround(input_f * static_cast<double>(input_multiplier)));
        uint32_t scaled_value = static_cast<uint32_t>(std::llround(value_f * static_cast<double>(value_scale)));
        entries.emplace_back(scaled_input, scaled_value);
    }
    if (entries.empty()) {
        LOG(ERROR) << "exp table: no entries provided";
        return;
    }

    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });

#ifdef NO_CFG
#else
    cfg.clear_exp_table();
    int rc = cfg.load_exp_table(input_multiplier, value_scale, entries);
    std::cout << "exp table load: " << rc << std::endl;
#endif
}

#ifdef NO_CFG
void extract_parser(json j, int proc_id) {
#else
void extract_parser(json j, int proc_id, api::CfgClient & cfg) {
#endif
    int parser_level = j.at("parser_level").get<int>();
#ifndef NO_CFG
    int rc0 = cfg.init_parser_level(proc_id, parser_level);
    std::cout << "parser init_parser_level: " << rc0 << std::endl;
#endif
    int level_id = 0;
    for(auto & level_json : j.at("levels")) {
        for(auto & parser_entry_json : level_json) {
            auto parser_tcam_entry = parser_entry_json.at("parser_tcam_entry");
            int current_state = parser_tcam_entry.at("current_state").get<int>();
            int entry = parser_tcam_entry.at("entry").get<int>();
            int mask = parser_tcam_entry.at("mask").get<int>();

            auto parser_sram_entry = parser_entry_json.at("parser_sram_entry");
            int header_id = parser_sram_entry.at("header_id").get<int>();
            int header_length = parser_sram_entry.at("header_length").get<int>();
            int next_state = parser_sram_entry.at("next_state").get<int>();
            int transition_field_num = parser_sram_entry.at("transition_field_num").get<int>();
            int miss_act = parser_sram_entry.at("accept_drop").get<int>();

            std::vector<FieldInfo*> fd_infos;
            for(auto & tf : parser_sram_entry.at("transition_fields")) {
                FieldInfo fd = {
                        static_cast<uint8_t>(tf.at("header_id").get<int>()),
                         static_cast<uint16_t>(tf.at("field_internal_offset").get<int>()),
                        static_cast<uint16_t>(tf.at("field_length").get<int>()),
                        get_field_type(tf.at("type").get<std::string>())
                };
                fd_infos.push_back(&fd);
            }
#ifndef NO_CFG
            int rc = cfg.mod_parser_entry(proc_id, level_id, current_state, entry, mask,
                                 header_id, header_length, next_state, transition_field_num,
                                 fd_infos, miss_act);
            std::cout << "parser mod_parser_entry: " << rc << std::endl;
#endif
        }
        level_id += 1;
    }
}

RelationCode get_relation_code(const std::string & op) {
    std::unordered_map<std::string, RelationCode> op_map = {
            {"==" , RelationCode::EQ},
            {"!=", RelationCode::NEQ},
            {">", RelationCode::GT},
            {">=", RelationCode::GTE},
            {"<", RelationCode::LT},
            {"<=", RelationCode::LTE}
    };
    return op_map[op];
}

ParameterType get_parameter_type (const std::string & pt) {
    std::unordered_map<std::string, ParameterType> pt_map = {
            {"CONST", ParameterType::CONSTANT},
            {"FIELD", ParameterType::FIELD},
            {"PARAM", ParameterType::PARAM},
            {"EXPR", ParameterType::OPERATOR}
    };
    return pt_map[pt];
}

void set_gate_param(GateParam & gp, json j) {
    auto type = j.at("type").get<std::string>();
    gp.type = get_parameter_type(type);

    switch (gp.type) {
        case ParameterType::CONSTANT : {
            gp.constant_data = {32,
                                get_uints(j.at("value").get<int>(),32)};
            break;
        }
        case ParameterType::FIELD : {
            auto value = j.at("value");
            gp.field = {
                  static_cast<uint8_t>(value.at("header_id").get<int>()),
                  static_cast<uint16_t>(value.at("field_internal_offset").get<int>()),
                  static_cast<uint16_t>(value.at("field_length").get<int>()),
                  get_field_type(value.at("type").get<std::string>())
            };
            break;
        }
        default:
            break;
    }
}

GateEntryType get_gate_entry_type (const std::string & get) {
    std::unordered_map<std::string, GateEntryType> get_map = {
            {"TABLE", GateEntryType::TABLE},
            {"STAGE", GateEntryType::STAGE}
    };
    return get_map[get];
}

#ifdef NO_CFG
void extract_gateway(json j, int proc_id) {
#else
void extract_gateway(json j, int proc_id, api::CfgClient & cfg) {
#endif

#ifndef NO_CFG
    cfg.clear_relation_exp(proc_id);
#endif
    for(auto & expr : j.at("expressions")) {
        auto value = expr.at("value");
        auto re = new RelationExp();
        auto op = value.at("op").get<std::string>();
        re->relation = get_relation_code(op);

        auto left_json = value.at("left");
        set_gate_param(re->param1, left_json);

        auto right_json = value.at("right");
        set_gate_param(re->param2, right_json);

#ifndef NO_CFG
        int rc = cfg.insert_relation_exp(proc_id, re);
        std::cout << "gateway insert_relation_exp: " << rc << std::endl;
#endif
    }

    auto next_table = j.at("next_table");
    auto default_entry_json = next_table.at("default");

    get_gate_entry_type(default_entry_json.at("type"));
    default_entry_json.at("value");

#ifndef NO_CFG
    int rc = cfg.set_default_gate_entry(proc_id,
                               get_gate_entry_type(default_entry_json.at("type").get<std::string>()),
                               default_entry_json.at("value").get<int>());
    std::cout << "gateway set_default_gate_entry: " << rc << std::endl;
#endif

    for(auto & gate_entry : next_table.at("entries")) {
        int bitmap = gate_entry.at("bitmap").get<int>();
        auto value = gate_entry.at("value");
#ifndef NO_CFG
        int rc0 = cfg.mod_res_map(proc_id, bitmap,
                        get_gate_entry_type(value.at("type").get<std::string>()),
                        value.at("value").get<int>());
        std::cout << "gateway mod_res_map: " << rc0 << std::endl;
#endif
    }

}

MatchType get_match_type (const std::string & mt) {
    std::unordered_map<std::string, MatchType> mt_map = {
            {"LPM", MatchType::LPM},
            {"TERNARY", MatchType::TERNARY},
            {"EXACT", MatchType::EXACT}
    };
    return mt_map[mt];
}

#ifdef NO_CFG
void extract_matcher(json j, int proc_id) {
#else
void extract_matcher(json j, int proc_id, api::CfgClient & cfg) {
#endif
    for(auto & matcher_thread_json : j) {
        int matcher_id = matcher_thread_json.at("id").get<int>();

        int miss_act_id = matcher_thread_json.at("miss_action_id").get<int>();
        int no_table = matcher_thread_json.at("no_table").get<int>();
        MatchType mt = get_match_type(matcher_thread_json.at("match_type").get<std::string>());

        auto key_memory = matcher_thread_json.at("key_memory");
        if (key_memory.is_array() && key_memory.empty()) {
            key_memory = json::object();
        }
        int depth = key_memory.value("depth", 0);
        int key_width = key_memory.value("width", 0);
        std::vector<uint8_t> key_config_vec;
        if (key_memory.contains("config")) {
            for(auto key_idx : key_memory.at("config")) {
                key_config_vec.push_back(static_cast<uint8_t>(key_idx.get<int>()));
            }
        }
        uint8_t* key_config = key_config_vec.data();

        auto value_memory = matcher_thread_json.value("value_memory", json::object());
        if (value_memory.is_array() && value_memory.empty()) {
            value_memory = json::object();
        }
        int value_width = value_memory.value("width", 0);
        std::vector<uint8_t> value_config_vec;
        if (value_memory.contains("config")) {
            for(auto value_idx : value_memory.at("config")) {
                value_config_vec.push_back(static_cast<uint8_t>(value_idx.get<int>()));
            }
        }
        uint8_t* value_config = value_config_vec.data();
#ifndef NO_CFG
        int rc = cfg.set_mem_config(proc_id, matcher_id,
                           key_width, value_width,
                           key_config, value_config,
                           mt, depth, miss_act_id);
        std::cout << "matcher set_mem_config: " << rc << std::endl;

#endif

        std::unordered_map<int, int> action_proc_map;
        if (matcher_thread_json.contains("action_to_proc")) {
            for(auto& action_proc : matcher_thread_json.at("action_to_proc")) {
                action_proc_map[action_proc.at("action_id").get<int>()]
                        = action_proc.at("proc_id").get<int>();
            }
        }
#ifndef NO_CFG
        rc = cfg.set_action_proc(proc_id, matcher_id, action_proc_map);
        std::cout << "matcher set_action_proc: " << rc << std::endl;
#endif

        std::vector<FieldInfo*> _fd_infos;
        for(auto & fd_info : matcher_thread_json.at("field_infos")) {
            auto fd = new FieldInfo{
                static_cast<uint8_t>(fd_info.at("header_id").get<int>()),
                        static_cast<uint16_t>(fd_info.at("field_internal_offset").get<int>()),
                static_cast<uint16_t>(fd_info.at("field_length").get<int>()),
                get_field_type(fd_info.at("type").get<std::string>())
            };
            _fd_infos.push_back(fd);
        }
#ifndef NO_CFG
        rc = cfg.set_field_info(proc_id, matcher_id, _fd_infos);
        std::cout << "matcher set_field_info: " << rc << std::endl;
#endif
    }
}

OpCode get_op_code (const std::string & op) {
    std::unordered_map<std::string, OpCode> op_map = {
            {"+", OpCode::ADD},
            {"-", OpCode::SUB},
            {"*", OpCode::MUL},
            {"/", OpCode::DIV},
            {"&", OpCode::BIT_AND},
            {"|", OpCode::BIT_OR},
            {"~", OpCode::BIT_NEG},
            {"^", OpCode::BIT_XOR},
            {">>", OpCode::SHIFT_RIGHT},
            {"<<", OpCode::SHIFT_LEFT},
            {"npb", OpCode::NEURON_PRIMITIVE},
            {"sigmoid", OpCode::SIGMOID_LOOKUP},
            {"sum_block", OpCode::SUM_BLOCK}
    };
    return op_map[op];
}

void build_exp_tree(ExpTreeNode * node, json j_node){
    auto node_type = get_parameter_type(j_node.at("type").get<std::string>());
    switch(node_type) {
        case ParameterType::OPERATOR : {
            node->opt.type = node_type;
            auto value = j_node.at("value");
            node->opt.val.op = get_op_code(value.at("op").get<std::string>());
            break;
        }
        case ParameterType::FIELD : {
            auto value = j_node.at("value");
            node->opt.type = node_type;
            node->opt.val.field = {
                    static_cast<uint8_t>(value.at("header_id").get<int>()),
                    static_cast<uint16_t>(value.at("field_internal_offset").get<int>()),
                    static_cast<uint16_t>(value.at("field_length").get<int>()),
                    get_field_type(value.at("type").get<std::string>())
            };
            break;
        }
        case ParameterType::PARAM : {
            node->opt.type = node_type;
            node->opt.val.action_para_id = j_node.at("value").get<int>();
            break;
        }
        case ParameterType::CONSTANT : {
            node->opt.type = node_type;
            node->opt.val.constant_data = {32,
                                           get_uints(j_node.at("value").get<int>(),32)};
            break;
        }
        default:
            break;
    }

    auto value = j_node.at("value");
    if(value.contains("left")) {
        auto c_node = new ExpTreeNode();
        build_exp_tree(c_node, value.at("left"));
        node->left = c_node;
    }

    if(value.contains("right")) {
        auto c_node = new ExpTreeNode();
        build_exp_tree(c_node, value.at("right"));
        node->right = c_node;
    }

}
#ifdef NO_CFG
void extract_executor(json j, int proc_id) {
#else
void extract_executor(json j, int proc_id, api::CfgClient & cfg) {
#endif
    for(auto & action : j) {
        int action_id = action.at("id").get<int>();
        int parameter_num = action.at("parameter_num").get<int>();
        std::vector<int> parameter_lengths;
        if (action.contains("action_parameters_lengths")) {
            for(auto &it : action.at("action_parameters_lengths")) {
                parameter_lengths.push_back(it.get<int>());
            }
        }

        std::vector<Primitive*> prims;
        for(auto & primitive : action.at("primitives")) {
            auto prim = new Primitive();
            auto lvalue = primitive.at("lvalue");
            prim->lvalue = new FieldInfo{
                    static_cast<uint8_t>(lvalue.at("header_id").get<int>()),
                    static_cast<uint16_t>(lvalue.at("field_internal_offset").get<int>()),
                    static_cast<uint16_t>(lvalue.at("field_length").get<int>()),
                    get_field_type(lvalue.at("type").get<std::string>())
            };
            auto root = new ExpTreeNode();
            build_exp_tree(root, primitive.at("rvalue"));
            prim->set_root(root);
            prims.push_back(prim);
        }
#ifndef NO_CFG
        int rc = cfg.insert_action(proc_id, action_id, parameter_num, prims, parameter_lengths);
        std::cout << "executor insert_action: " << rc << std::endl;
#endif
    }
}

#ifdef NO_CFG
void extract_metadata(json j) {
#else
void extract_metadata(json j, api::CfgClient & cfg) {
#endif
    std::vector<HeaderInfo*> metas;
    for(auto & meta : j) {
        auto hdr = new HeaderInfo();
        hdr->hdr_id = meta.at("id").get<int>();
        hdr->hdr_len = meta.at("length").get<int>();
        hdr->hdr_offset = meta.at("offset").get<int>();
        LOG(INFO) << "      header_id: " << hdr->hdr_id;
        LOG(INFO) << "      header_len: " << hdr->hdr_len;
        LOG(INFO) << "      header_offset: " << hdr->hdr_offset;
        metas.push_back(hdr);
    }

#ifndef NO_CFG
    int rc = cfg.set_metadata(metas);
    std::cout << "metadata set: " << rc << std::endl;
#endif

}

#ifdef NO_CFG
void extract(const std::string & json_path) {
#else
void extract(const std::string & json_path, api::CfgClient & cfg) {
#endif
    json j;
    std::ifstream(json_path) >> j;

    if (j.contains("neuron_primitive_contexts")) {
#ifdef NO_CFG
        extract_neuron_primitives(j.at("neuron_primitive_contexts"));
#else
        extract_neuron_primitives(j.at("neuron_primitive_contexts"), cfg);
#endif
    }

    if (j.contains("sigmoid_table")) {
#ifdef NO_CFG
        extract_sigmoid_table(j.at("sigmoid_table"));
#else
        extract_sigmoid_table(j.at("sigmoid_table"), cfg);
#endif
    }

    if (j.contains("exp_table")) {
#ifdef NO_CFG
        extract_exp_table(j.at("exp_table"));
#else
        extract_exp_table(j.at("exp_table"), cfg);
#endif
    }

    for(int i = 0; i < 16; i++) {
        auto key = "processor_" + std::to_string(i);
        auto processor = j.at(key);
        if(processor == nullptr) {
            std::cout << key << std::endl;
            continue;
        }
        int processor_id = processor.at("id").get<int>(); // TODO: ??

        auto & parser = processor.at("parser");
        auto & gateway = processor.at("gateway");
        auto & matcher = processor.at("matcher");
        auto & executor = processor.at("executor");
        auto & metadata = j.at("metadata");

#ifdef NO_CFG
        extract_parser(parser, i);
        if (processor.contains("gateway")) {
            extract_gateway(processor.at("gateway"), i);
        }
        extract_matcher(matcher, i);
        extract_executor(executor, i);
        extract_metadata(metadata);
#else
        extract_parser(parser, i, cfg);
        if (processor.contains("gateway")) {
            extract_gateway(processor.at("gateway"), i, cfg);
        }
        extract_matcher(matcher, i, cfg);
        extract_executor(executor, i, cfg);
        extract_metadata(metadata, cfg);
#endif
    }
}

#endif //RECONF_SWITCH_IPSA_CTRL_INIT_DATAPLANE_H
