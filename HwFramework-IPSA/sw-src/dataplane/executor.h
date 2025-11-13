#ifndef RECONF_SWITCH_IPSA_EXECUTOR_H
#define RECONF_SWITCH_IPSA_EXECUTOR_H

#include "defs.h"
#include "neuron_primitive.h"
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

inline Data make_zero_data_bits(int bits) {
    Data zero{};
    zero.data_len = bits;
    int byte_len = bits <= 0 ? 0 : (bits + 7) / 8;
    if (byte_len > 0) {
        zero.val = new uint8_t[byte_len];
        std::memset(zero.val, 0, byte_len);
    } else {
        zero.val = nullptr;
    }
    return zero;
}

inline uint64_t mask_for_width_u64(uint32_t bitwidth) {
    if (bitwidth == 0) {
        return 0;
    }
    if (bitwidth >= 64) {
        return std::numeric_limits<uint64_t>::max();
    }
    return (1ULL << bitwidth) - 1ULL;
}

inline bool extract_chunks_from_data(const Data& data,
                                     uint32_t chunk_width,
                                     uint32_t chunk_count,
                                     std::vector<uint64_t>& out) {
    if (chunk_width == 0 || chunk_count == 0 || data.val == nullptr) {
        return false;
    }
    uint32_t total_bits = chunk_width * chunk_count;
    if (data.data_len < static_cast<int>(total_bits)) {
        return false;
    }
    uint32_t data_bits = static_cast<uint32_t>(data.data_len);
    uint32_t byte_len = (data_bits + 7) / 8;
    out.clear();
    out.reserve(chunk_count);
    for (uint32_t idx = 0; idx < chunk_count; ++idx) {
        uint32_t start_bit = idx * chunk_width;
        uint64_t value = 0;
        for (uint32_t bit = 0; bit < chunk_width; ++bit) {
            uint32_t current_bit = start_bit + bit;
            if (current_bit >= data_bits) {
                return false;
            }
            uint32_t byte_index = current_bit / 8;
            if (byte_index >= byte_len) {
                return false;
            }
            uint32_t bit_index = 7 - (current_bit % 8);
            uint8_t bit_val = static_cast<uint8_t>((data.val[byte_index] >> bit_index) & 0x1);
            value = (value << 1) | bit_val;
        }
        out.push_back(value);
    }
    return true;
}

inline Data pack_chunks_into_data(const std::vector<uint64_t>& values, uint32_t chunk_width) {
    if (values.empty() || chunk_width == 0) {
        return make_zero_data_bits(0);
    }
    uint32_t total_bits = chunk_width * static_cast<uint32_t>(values.size());
    Data packed{};
    packed.data_len = static_cast<int>(total_bits);
    int byte_len = (total_bits + 7) / 8;
    packed.val = new uint8_t[byte_len];
    std::memset(packed.val, 0, byte_len);
    uint64_t mask = mask_for_width_u64(chunk_width);
    for (size_t idx = 0; idx < values.size(); ++idx) {
        uint64_t value = values[idx] & mask;
        for (uint32_t bit = 0; bit < chunk_width; ++bit) {
            uint32_t global_bit = static_cast<uint32_t>(idx) * chunk_width + bit;
            uint32_t byte_index = global_bit / 8;
            uint32_t bit_index = 7 - (global_bit % 8);
            uint64_t mask_bit = 1ULL << (chunk_width - 1 - bit);
            uint8_t bit_value = static_cast<uint8_t>((value & mask_bit) ? 1 : 0);
            packed.val[byte_index] |= static_cast<uint8_t>(bit_value << bit_index);
        }
    }
    return packed;
}

uint32_t get_value(Data data) {
    uint32_t sum = 0;

    int byte_len = ceil(data.data_len * 1.0 / 8);
    int max = byte_len >= 4 ? 4 : byte_len;
    for(int i = max - 1; i >= 0 ; i--) {
        sum += (uint32_t)data.val[i] << ((max - 1 - i) * 8);
    }

    return sum;
}

int32_t get_signed_value(Data data) {
    if (data.data_len == 0) {
        return 0;
    }
    uint32_t unsigned_value = get_value(data);
    int bits = data.data_len > 32 ? 32 : data.data_len;
    if (bits >= 32) {
        return static_cast<int32_t>(unsigned_value);
    }
    if (bits <= 0) {
        return 0;
    }
    uint32_t sign_bit = 1u << (bits - 1);
    uint32_t value_mask = (sign_bit << 1) - 1;
    uint32_t masked = unsigned_value & value_mask;
    if (masked & sign_bit) {
        masked |= (~value_mask);
    }
    return static_cast<int32_t>(masked);
}

Data get_data (int len, uint32_t d) {
    Data data{};
    data.data_len = len;
    int byte_len = ceil(len * 1.0 / 8);
    data.val = new uint8_t[byte_len];
    for(int i = 0; i < byte_len; i++) {
        data.val[byte_len - 1 - i] = d >> (i * 8);
    }
    return data;
}

Data get_op_val_any(Parameter para, PHV * phv, std::vector<ActionParam*> action_paras) {
    Data res{};
    LOG(INFO) << "      param type(constant, header, field, param): " << (int)para.type;
    switch(para.type) {
        case PT::CONSTANT : {
            res = para.val.constant_data;
            LOG(INFO) << "          length: " << res.data_len;
            LOG(INFO) << "          value: ";
            for(int i = 0 ; i < ceil(res.data_len*1.0/8); i++) {
                LOG(INFO) << "               " << (int)res.val[i];
            }
            break;
        }
        case PT::FIELD : {
            int fd_len = para.val.field.fd_len;
            int internal_offset = para.val.field.internal_offset;
            HeaderInfo * hi = phv->parsed_headers[para.val.field.hdr_id];

            LOG(INFO) << "          obtained from:";
            LOG(INFO) << "          hdr_id: " << (int)para.val.field.hdr_id;
            LOG(INFO) << "          internal_offset: " << para.val.field.internal_offset;
            LOG(INFO) << "          field_length: " << para.val.field.fd_len;


            int start = hi->hdr_offset + internal_offset;
            int end = start + fd_len;

            int byte_start = start / 8;
            int byte_end = end / 8; //ceil(end * 1.0 / 8);
            int byte_len = byte_end - byte_start + 1;

            LOG(INFO) << "          byte_start: " << byte_start;
            LOG(INFO) << "          byte_end: " << byte_end;
            LOG(INFO) << "          original value: ";
            for(int i = byte_start ; i <= byte_end; i++) {
                LOG(INFO) << "               " << (int)phv->packet[i];
            }

            int left_shift = start % 8;
            int right_shift = 8 - end % 8;

//            res.if_sign = false;
            res.data_len = fd_len;
            res.val = new uint8_t[(int)ceil(fd_len * 1.0 / 8)];
            int fd_len_byte = ceil(fd_len * 1.0 / 8);

            auto fd_backup = (uint8_t*) malloc(sizeof(uint8_t) * byte_len);
            memset(fd_backup, 0, sizeof(uint8_t) * byte_len);
            memcpy(fd_backup, phv->packet + byte_start, byte_len);
            fd_backup[0] = fd_backup[0] << left_shift >> left_shift;
            fd_backup[byte_len - 1] = fd_backup[byte_len - 1] >> right_shift << right_shift;

            int idx = fd_len_byte - 1;
            for(int i = byte_len - 1; i > 0; i--) {
                res.val[idx] = (fd_backup[i] >> right_shift) + (fd_backup[i - 1] << (8 - right_shift));
                idx--;
//                std::cout << (int)res.val[idx+1] << std::endl;
            }
            LOG(INFO) << "          length: " << res.data_len;
            LOG(INFO) << "          value: ";
            for(int i = 0 ; i < ceil(res.data_len*1.0/8); i++) {
                LOG(INFO) << "               " << (int)res.val[i];
            }
            break;
        }
        case PT::PARAM : { // if it is value-, just extract it out; if it is field-, should be larger than uint32_t
            ActionParam * ap = action_paras[para.val.action_para_id];
            res.data_len = ap->action_para_len;
//            res.if_sign = ap->if_sign;
            res.val = ap->val;
            LOG(INFO) << "          length: " << res.data_len;
            LOG(INFO) << "          value: ";
            for(int i = 0 ; i < ceil(res.data_len*1.0/8); i++) {
                LOG(INFO) << "               " << (int)res.val[i];
            }
            break;
        }
        default:
            break;
    }
    return res;
}

Data bit_compute(int op, Data data1, Data data2) {
    switch (op) {
        case 1: { // <<
            int left_shift_num = get_value(data2);
            uint32_t operand = get_value(data1);
            uint32_t bitmap = 0xffffffff;
            for(int i = 0; i < 32 - data1.data_len; i++) {
                bitmap -= 1 << (31 - i);
            }
            operand = (operand << left_shift_num) & bitmap;
            return get_data(data1.data_len, operand);
        }
        case 2 : { // >>
            int right_shift_num = get_value(data2);
            uint32_t operand = get_value(data1);
            operand = (operand >> right_shift_num);
            return get_data(data1.data_len, operand);
        }
        case 3 : { // &
            uint32_t left_operand = get_value(data1);
            uint32_t right_operand = get_value(data2);
            uint32_t res = left_operand & right_operand;
            return get_data(data1.data_len, res);
        }
        case 4 : { // |
            uint32_t left_operand = get_value(data1);
            uint32_t right_operand = get_value(data2);
            uint32_t res = left_operand | right_operand;
            return get_data(data1.data_len, res);
        }
        case 5 : { // ^
            uint32_t left_operand = get_value(data1);
            uint32_t right_operand = get_value(data2);
            uint32_t res = left_operand ^ right_operand;
            return get_data(data1.data_len, res);
        }
        case 6 : { // ~
            uint32_t left_operand = get_value(data1);
            uint32_t res = 0;
            if(data1.data_len <= 8) {
                res = uint8_t(~left_operand);
            } else if(data1.data_len <= 16) {
                res = uint16_t(~left_operand);
            } else if(data1.data_len <= 32) {
                res = ~left_operand;
            }
            return get_data(data1.data_len, res);
        }
        default:
            return Data();
    }
}

struct ExpTreeNode {
public:
    Parameter opt;
    struct ExpTreeNode * left = nullptr;
    struct ExpTreeNode * right = nullptr;
};

struct Primitive: public ExpTreeNode {
public:
    FieldInfo *lvalue;

    Primitive(struct ExpTreeNode * r = nullptr) : root(r){}

    void set_lvalue(FieldInfo * fdInfo) {
        lvalue = fdInfo;
    }

    ExpTreeNode* get_root() {
        return this->root;
    }

    void set_root(ExpTreeNode * r) {
        root = r;
    }

    void run(PHV * phv, const std::vector<ActionParam*>& action_paras) {
        LOG(INFO) << "  Primitive run...";
        Data res = execute(phv, root, action_paras, 2);

        auto *p = new Parameter();
        p->type = ParameterType::FIELD;
        p->val.field = {lvalue->hdr_id, lvalue->internal_offset,
                        lvalue->fd_len, lvalue->fd_type};
        LOG(INFO) << "      modify_field: ";
        LOG(INFO) << "          header_id: " << (int)p->val.field.hdr_id;
        LOG(INFO) << "          header_offset: " << (int)phv->parsed_headers[p->val.field.hdr_id]->hdr_offset;
        LOG(INFO) << "          internal_offset: " << (int)p->val.field.internal_offset;
        LOG(INFO) << "          field_length: " << (int)p->val.field.fd_len;
        LOG(INFO) << "          field_type: " << (int)p->val.field.fd_type;
        modify_field_with_data(*p, res, phv);
        LOG(INFO) << "      modified with:";
        LOG(INFO) << "          length: " << res.data_len;
        LOG(INFO) << "          value: " << res.data_len;
        for(int i = 0; i < ceil(res.data_len*1.0/8);i++) {
            LOG(INFO) << "              " << (int)res.val[i];
        }
        LOG(INFO) << "      Mod result: ";

        int begin_bit = phv->parsed_headers[p->val.field.hdr_id]->hdr_offset + p->val.field.internal_offset;
        int end_bit = begin_bit + p->val.field.fd_len - 1;

        int begin_byte = begin_bit / 8;
        int end_byte = end_bit / 8;

        for(int i = begin_byte; i <= end_byte; i++) {
            LOG(INFO) << "          " << (int) phv->packet[i];
        }


    }

    void build_para_tree() {
        root->opt.type = ParameterType::OPERATOR;
        root->opt.val.op = OpCode::BIT_AND;

        root->left->opt.type = ParameterType::CONSTANT;
        uint8_t data0[] = "\xff";
        root->left->opt.val.constant_data = Data{16, data0};

        root->left->opt.type = ParameterType::OPERATOR;
        root->left->opt.val.op = OpCode::ADD;
    }

    Data execute(PHV * phv, ExpTreeNode * etn, const std::vector<ActionParam*>& action_paras, int of) {
        std::string off = "";
        for(int i = 0 ; i < of; i++) off+= "\t";
        LOG(INFO) << off << "node.type(constant, header, field, param, operator):  " <<  (int)etn->opt.type;
        if(etn->opt.type == ParameterType::OPERATOR) {
            LOG(INFO) << off << "   operator.op(+ -):  " <<  (int)etn->opt.val.op;
            switch (etn->opt.val.op) {
                case OpCode::ADD : {
                    uint32_t res = get_value(execute(phv, etn->left, action_paras, of+1))
                            + get_value(execute(phv, etn->right, action_paras, of+1));
                    LOG(INFO) << off << "   value: " << res;
                    return get_data(32, res);
                }
                case OpCode::SUB : {
                    uint32_t res = get_value(execute(phv, etn->left, action_paras, of+1))
                                   - get_value(execute(phv, etn->right, action_paras, of+1));
                    LOG(INFO) << off << "   value: " << res;
                    return get_data(32, res);
                }
                case OpCode::MUL : {
                    uint64_t res = static_cast<uint64_t>(get_value(execute(phv, etn->left, action_paras, of+1))) *
                                   static_cast<uint64_t>(get_value(execute(phv, etn->right, action_paras, of+1)));
                    uint32_t narrowed = static_cast<uint32_t>(res & 0xffffffffu);
                    LOG(INFO) << off << "   value: " << res;
                    return get_data(32, narrowed);
                }
                case OpCode::DIV : {
                    uint32_t divisor = get_value(execute(phv, etn->right, action_paras, of+1));
                    if (divisor == 0) {
                        LOG(WARNING) << off << "   divisor is zero, returning 0.";
                        return get_data(32, 0);
                    }
                    uint32_t dividend = get_value(execute(phv, etn->left, action_paras, of+1));
                    uint32_t res = dividend / divisor;
                    LOG(INFO) << off << "   value: " << res;
                    return get_data(32, res);
                }
                case OpCode::SHIFT_LEFT : {
                    Data data = execute(phv, etn->left, action_paras, of+1);
                    Data shift = execute(phv, etn->right, action_paras, of+1);
                    Data res = bit_compute(1, data, shift);
                    return res;
                }
                case OpCode::SHIFT_RIGHT : {
                    Data data = execute(phv, etn->left, action_paras, of+1);
                    Data shift = execute(phv, etn->right, action_paras, of+1);
                    Data res = bit_compute(2, data, shift);
                    return res;
                }
                case OpCode::BIT_AND : {
                    Data data1 = execute(phv, etn->left, action_paras, of+1);
                    Data data2 = execute(phv, etn->right, action_paras, of+1);
                    Data res = bit_compute(3, data1, data2);
                    return res;
                }
                case OpCode::BIT_OR : {
                    Data data1 = execute(phv, etn->left, action_paras, of+1);
                    Data data2 = execute(phv, etn->right, action_paras, of+1);
                    Data res = bit_compute(4, data1, data2);
                    return res;
                }
                case OpCode::BIT_XOR : {
                    Data data1 = execute(phv, etn->left, action_paras, of+1);
                    Data data2 = execute(phv, etn->right, action_paras, of+1);
                    Data res = bit_compute(5, data1, data2);
                    return res;
                }
                case OpCode::BIT_NEG : {
                    Data data1 = execute(phv, etn->left, action_paras, of+1);
                    Data res = bit_compute(6, data1, Data{});
                    return res;
                }
                case OpCode::NEURON_PRIMITIVE : {
                    if (etn->left == nullptr || etn->right == nullptr) {
                        LOG(ERROR) << off << "   neuron primitive expects feature and context operands.";
                        int target_bits = lvalue ? lvalue->fd_len : 32;
                        return make_zero_data_bits(target_bits);
                    }

                    Data features = execute(phv, etn->left, action_paras, of+1);
                    Data context_meta = execute(phv, etn->right, action_paras, of+1);
                    uint32_t context_id = get_value(context_meta);
                    const auto *ctx = NeuronPrimitiveManager::instance().get_context(static_cast<uint16_t>(context_id));
                    if (ctx == nullptr) {
                        LOG(WARNING) << off << "   neuron primitive context " << context_id << " not found.";
                        int target_bits = lvalue ? lvalue->fd_len : 32;
                        return make_zero_data_bits(target_bits);
                    }
                    int expected_bits = static_cast<int>(ctx->num_neurons * (ctx->output_bitwidth == 0 ? 32 : ctx->output_bitwidth));
                    if (lvalue && expected_bits != lvalue->fd_len) {
                        LOG(WARNING) << off << "   neuron primitive output width (" << expected_bits
                                     << ") mismatches field width " << lvalue->fd_len;
                    }
                    return execute_neuron_primitive(*ctx, features);
                }
                case OpCode::SIGMOID_LOOKUP : {
                    Data input_data = execute(phv, etn->left, action_paras, of+1);
                    uint32_t out_bits = 0;
                    if (etn->right != nullptr) {
                        Data width_data = execute(phv, etn->right, action_paras, of+1);
                        out_bits = get_value(width_data);
                    }
                    Data res = apply_sigmoid_lookup(input_data, out_bits);
                    return res;
                }
                case OpCode::SUM_BLOCK : {
                    auto zero_with_log = [&](const std::string &reason) -> Data {
                        LOG(ERROR) << off << "   sum_block: " << reason;
                        int target_bits = lvalue ? lvalue->fd_len : 32;
                        return make_zero_data_bits(target_bits);
                    };

                    std::vector<Data> operand_blocks;
                    uint32_t neuron_count = 0;
                    std::function<bool(ExpTreeNode*)> gather = [&](ExpTreeNode* current) -> bool {
                        if (current == nullptr) {
                            return false;
                        }
                        if (current->left == nullptr || current->right == nullptr) {
                            return false;
                        }
                        operand_blocks.push_back(execute(phv, current->left, action_paras, of+1));
                        if (current->right->opt.type == ParameterType::OPERATOR &&
                            current->right->opt.val.op == OpCode::SUM_BLOCK) {
                            return gather(current->right);
                        }
                        Data neurons = execute(phv, current->right, action_paras, of+1);
                        neuron_count = get_value(neurons);
                        return true;
                    };

                    if (!gather(etn)) {
                        return zero_with_log("invalid expression tree.");
                    }
                    if (operand_blocks.empty()) {
                        return zero_with_log("requires at least one operand.");
                    }
                    if (neuron_count == 0) {
                        return zero_with_log("number of neurons must be > 0.");
                    }

                    int bits_per_block = operand_blocks[0].data_len;
                    if (bits_per_block <= 0) {
                        return zero_with_log("operand width must be positive.");
                    }
                    for (const auto &block : operand_blocks) {
                        if (block.data_len != bits_per_block) {
                            return zero_with_log("all operands must have the same width.");
                        }
                    }
                    if (bits_per_block % static_cast<int>(neuron_count) != 0) {
                        return zero_with_log("operand width must be divisible by neuron count.");
                    }
                    uint32_t chunk_width = static_cast<uint32_t>(bits_per_block / static_cast<int>(neuron_count));
                    if (chunk_width == 0) {
                        return zero_with_log("chunk width evaluated to zero.");
                    }

                    std::vector<uint64_t> accum(neuron_count, 0);
                    std::vector<uint64_t> chunks;
                    for (const auto &block : operand_blocks) {
                        if (!extract_chunks_from_data(block, chunk_width, neuron_count, chunks)) {
                            return zero_with_log("failed to split operand bits.");
                        }
                        for (uint32_t idx = 0; idx < neuron_count; ++idx) {
                            accum[idx] += chunks[idx];
                        }
                    }

                    uint64_t max_value = mask_for_width_u64(chunk_width);
                    for (auto &value : accum) {
                        if (value > max_value) {
                            value = max_value;
                        }
                    }

                    Data packed = pack_chunks_into_data(accum, chunk_width);
                    if (lvalue && packed.data_len != lvalue->fd_len) {
                        LOG(WARNING) << off << "   sum_block output width (" << packed.data_len
                                     << ") mismatches field width " << lvalue->fd_len;
                    }
                    return packed;
                }
                default:
                    return Data();
            }
        } else {
            LOG(INFO) << off << "   get operand: ";
            return get_op_val_any(etn->opt, phv, action_paras);
        }
    }

    static void modify_field_with_data(Parameter para, Data data, PHV * phv) {
        int internal_offset = para.val.field.internal_offset; 
        int fd_len = para.val.field.fd_len; 
        auto hi = phv->parsed_headers[para.val.field.hdr_id];

        int begin_bit = hi->hdr_offset + internal_offset;
        int end_bit = begin_bit + fd_len - 1;

        int begin_byte = begin_bit / 8, end_byte = end_bit / 8;

        int byte_len = end_byte - begin_byte + 1;
        auto span_byte = new uint8_t[byte_len]{0};

        auto new_data = new uint8_t[byte_len];
        memset(new_data, 0, byte_len * sizeof(uint8_t));
        int data_len = ceil(data.data_len * 1.0 / 8);
        if(data_len < byte_len) {
            memcpy(new_data+1, data.val, byte_len-1);
        } else if(data_len == byte_len) {
            memcpy(new_data, data.val, byte_len);
        } else{
            memcpy(new_data, data.val+data_len-byte_len, byte_len);
        }

        int right_empty = (end_bit / 8 + 1) * 8 - end_bit - 1;

        for(int i = byte_len - 1; i >= 1; i--) {
            span_byte[i] += (new_data[i] << right_empty);
            span_byte[i-1] += (new_data[i] >> (8 - right_empty));
        }
        span_byte[0] += (new_data[0] << right_empty);

        auto bitmap = new uint8_t[byte_len]{0};
        int left_empty = begin_bit % 8;

        for(int i = 0; i < left_empty; i++) {
            bitmap[0] += ( 1 << (7 - i) );
        }

        for(int i = 0; i < right_empty; i++) {
            bitmap[byte_len-1] += (1 << i);
        }

        int idx = 0;
        for(int i = begin_byte; i <= end_byte; i++) {
            phv->packet[i] = (phv->packet[i] & bitmap[idx]) + span_byte[idx];
            idx++;
        }
    }

private:
    struct ExpTreeNode * root;
};


struct Action {
    std::vector<Primitive*> prims;
    int primitive_num;

    std::vector<Data> primitive_res;

    int para_num = 0;
    std::vector<ActionParam*> action_paras;
    std::vector<int> action_para_lens;

    Action() = default;

    void set_action_para_lens(std::vector<int> para_lens) {
        action_para_lens = std::move(para_lens);
    }

    void set_action_paras(uint8_t * value) {
        action_paras.clear();
        LOG(INFO) << "      action param num is: " << para_num;
        int cur_offset = 16;
        LOG(INFO) << "      param: ";
        for(int i = 0; i < para_num; i++) {
            int para_start = cur_offset;
            int para_end = cur_offset + action_para_lens[i];
            LOG(INFO) << "          length: " << action_para_lens[i];
            LOG(INFO) << "          start idx: " << para_start;
            LOG(INFO) << "          end idx: " << para_end;

            int left_offset = para_start % 8;
            int right_offset = para_end % 8;

            int byte_start = cur_offset / 8;
            int byte_end = (para_end / 8) - (right_offset == 0 ? 1 : 0);

            int new_len = byte_end - byte_start + 1;
            auto * value_new = new uint8_t[byte_end - byte_start + 1];
            memcpy(value_new, value + byte_start, byte_end - byte_start + 1);
            value_new[0] = (uint8_t(value_new[0] << left_offset) >> left_offset);
            value_new[new_len - 1] = right_offset == 0 ? value_new[new_len - 1] :
                    (value_new[new_len - 1] >> (8 - right_offset) << (8-right_offset));

            LOG(INFO) << "          value: ";
            for(int k = 0; k < new_len; k++) {
                LOG(INFO) << "              " <<(int)value_new[k];
            }

            auto data = new ActionParam();
            data->action_para_len = action_para_lens[i];
            int byte_len = (int)ceil(data->action_para_len * 1.0 / 8);
            data->val = new uint8_t[byte_len];
            memset(data->val, 0, sizeof(uint8_t) * byte_len);

            int idx = new_len - 1;

            for(int j = byte_len - 1; j >= 0; j--) {
                if(right_offset == 0) {
                    data->val[j] += value_new[idx];
                }else{
                    data->val[j] += value_new[idx] >> (8 - right_offset);
                    if(idx > 0){
                        data->val[j] += uint8_t(value_new[idx-1] << right_offset);
                    }
                }
                idx--;
            }
            LOG(INFO) << "          action para value: ";
            for(int k = 0; k < byte_len; k++) {
                LOG(INFO) << "              " <<(int)data->val[k];
            }
            action_paras.push_back(data);
            cur_offset = para_end;
        }
        LOG(INFO) << "      set action paras end! ";
    }

    void set_primitive(const std::vector<Primitive*>& _prims) {
        for(auto it : _prims) {
            prims.push_back(it);
        }
    }

    void del() {
        auto p_len = prims.size();
        for(int i = 0; i < p_len; i++) {
            delete prims[i];
        }

        auto a_len = action_paras.size();
        for(int i = 0; i < a_len; i++) {
            delete action_paras[i]->val;
            delete action_paras[i];
        }
    }

    void set_action_para(const std::vector<ActionParam*>& _action_paras) {
        for(auto it : _action_paras) {
            action_paras.push_back(it);
        }
    }

    void execute(PHV * phv) {
        LOG(INFO) << "Primitive num: " << prims.size();
        for(auto it : prims) {
            LOG(INFO)<< "The type of lvalue of primitive: " << (int)it->lvalue->fd_type << std::endl;
            it->run(phv, action_paras);
        }
        LOG(INFO) << "Primitive execute end!";
    }
};

class Executor {
public:

    Action * _actions[EXECUTOR_ACTION_NUM];
    Executor() {

    }

    void insert_action(Action *ac, int action_id) {
        _actions[action_id] = ac;
    }

    void execute_miss_action(int action_id, PHV * phv) {
        Action * action = _actions[action_id];
        action->execute(phv);
    }

    void execute(PHV * phv) {
        LOG(INFO) << "  Executor...";
        int32_t action_id = phv->next_action_id;
        LOG(INFO) << "      action_id: " << action_id;
        auto action = _actions[action_id];
        action->set_action_paras(phv->match_value);
        action->execute(phv);
    }

    void execute_action(int action_id, PHV * phv) {
        Action * action = _actions[action_id];
        action->execute(phv);
    }

    void set_action_para_value(uint8_t * value) {
        int action_id = uint32_t(value[1]) + ((uint32_t)value[0] << 8);
        auto action = _actions[action_id];
        action->set_action_paras(value);
    }

    void del_action(int action_id) {
        _actions[action_id]->del();
        delete _actions[action_id];
    }

    void clear_action() {
        for(int i = 0; i < EXECUTOR_ACTION_NUM; i++) {
            if(_actions[i] != nullptr) {
                _actions[i]->del();
                delete _actions[i];
            }
        }
    }
};

#endif //RECONF_SWITCH_IPSA_EXECUTOR_H
