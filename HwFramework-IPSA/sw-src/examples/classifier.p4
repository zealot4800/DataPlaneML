/* -*- P4_16 -*- */
#include <core.p4>
#include <v1model.p4>

const bit<16> TYPE_IPV4          = 0x0800;
const bit<8>  IPV4_OPT_FEATS     = 0x9e;
const bit<8>  IPV4_BASE_IHL      = 5;
const bit<8>  FEATURE_OPTION_LEN = 20; /* type+len + 8×16b + 2B pad */
const bit<16> FEATURE_TL         = (((bit<16>)IPV4_OPT_FEATS) << 8) | (bit<16>)FEATURE_OPTION_LEN;

typedef bit<9>  egressSpec_t;
typedef bit<48> macAddr_t;
typedef bit<32> ip4Addr_t;

header ethernet_t {
    macAddr_t dstAddr;
    macAddr_t srcAddr;
    bit<16>   etherType;
}

header ipv4_t {
    bit<4>    version;
    bit<4>    ihl;
    bit<8>    diffserv;
    bit<16>   totalLen;
    bit<16>   identification;
    bit<3>    flags;
    bit<13>   fragOffset;
    bit<8>    ttl;
    bit<8>    protocol;
    bit<16>   hdrChecksum;
    ip4Addr_t srcAddr;
    ip4Addr_t dstAddr;
}

header ipv4_features_opt_t {
    bit<8>  option_type;
    bit<8>  option_length;
    bit<16> feat0;
    bit<16> feat1;
    bit<16> feat2;
    bit<16> feat3;
    bit<16> feat4;
    bit<16> feat5;
    bit<16> feat6;
    bit<16> feat7;
    bit<16> pad;
}

struct metadata {
    bit<1>   features_valid;
    bit<128> packed_features;
    bit<8>   classification_result;
}

struct headers {
    ethernet_t          ethernet;
    ipv4_t              ipv4;
    ipv4_features_opt_t feat_opt;
}

parser MyParser(packet_in packet,
                out headers hdr,
                inout metadata meta,
                inout standard_metadata_t standard_metadata) {

    bit<8>  options_left;
    bit<1>  feature_present;

    state start {
        transition parse_ethernet;
    }

    state parse_ethernet {
        packet.extract(hdr.ethernet);
        transition select(hdr.ethernet.etherType) {
            TYPE_IPV4: parse_ipv4;
            default: accept;
        }
    }

    state parse_ipv4 {
        packet.extract(hdr.ipv4);
        meta.features_valid = 0;

        bit<8> header_bytes = ((bit<8>)hdr.ipv4.ihl) << 2;
        bit<8> base_bytes   = IPV4_BASE_IHL * 4;

        options_left   = (header_bytes >= base_bytes) ? (header_bytes - base_bytes) : 0;
        feature_present = (options_left >= FEATURE_OPTION_LEN) ? 1w1 : 1w0;

        transition select(feature_present) {
            1: feature_guard;
            default: accept;
        }
    }

    state feature_guard {
        transition select(packet.lookahead<bit<16>>()) {
            FEATURE_TL: feature_extract;
            default: accept;
        }
    }

    state feature_extract {
        packet.extract(hdr.feat_opt);
        options_left = options_left - FEATURE_OPTION_LEN;
        meta.features_valid = 1;
        transition feature_skip_rest;
    }

    state feature_skip_rest {
        transition select(options_left) {
            0: accept;
            default: discard_remaining_options;
        }
    }

    state discard_remaining_options {
        packet.advance((bit<32>)options_left * 8);
        options_left = 0;
        transition accept;
    }
}

control MyVerifyChecksum(inout headers hdr, inout metadata meta) {
    apply { }
}

control MyIngress(inout headers hdr,
                  inout metadata meta,
                  inout standard_metadata_t standard_metadata) {

    action process_features() {
        bit<128> packed = (bit<128>)hdr.feat_opt.feat0;
        packed = (packed << 16) | (bit<128>)hdr.feat_opt.feat1;
        packed = (packed << 16) | (bit<128>)hdr.feat_opt.feat2;
        packed = (packed << 16) | (bit<128>)hdr.feat_opt.feat3;
        packed = (packed << 16) | (bit<128>)hdr.feat_opt.feat4;
        packed = (packed << 16) | (bit<128>)hdr.feat_opt.feat5;
        packed = (packed << 16) | (bit<128>)hdr.feat_opt.feat6;
        packed = (packed << 16) | (bit<128>)hdr.feat_opt.feat7;
        meta.packed_features = packed;
    }

    action no_features() {
        meta.packed_features = 0;
    }

    action forward(macAddr_t dst, egressSpec_t port) {
        standard_metadata.egress_spec = port;
        hdr.ethernet.dstAddr = dst;
    }

    action drop() {
        mark_to_drop(standard_metadata);
    }

    table feature_logic {
        key = {
            hdr.feat_opt.isValid(): exact;
            meta.features_valid: exact;
        }
        actions = {
            process_features;
            no_features;
        }
        const entries = {
            (true, 1w1): process_features;
        }
        default_action = no_features;
    }

    apply {
        if (hdr.ipv4.isValid()) {
            feature_logic.apply();
            // Placeholder for neuron primitive block
            // meta.classification_result = NEURON_PRIMITIVE(meta.packed_features);
            if (meta.classification_result == 1) {
                drop();
            } else {
                forward(hdr.ethernet.dstAddr, 1);
            }
        }
    }
}

control MyEgress(inout headers hdr,
                 inout metadata meta,
                 inout standard_metadata_t standard_metadata) {
    apply { }
}

control MyComputeChecksum(inout headers hdr, inout metadata meta) {
    apply {
        update_checksum(
            hdr.ipv4.isValid(),
            { hdr.ipv4.version,
              hdr.ipv4.ihl,
              hdr.ipv4.diffserv,
              hdr.ipv4.totalLen,
              hdr.ipv4.identification,
              hdr.ipv4.flags,
              hdr.ipv4.fragOffset,
              hdr.ipv4.ttl,
              hdr.ipv4.protocol,
              hdr.ipv4.srcAddr,
              hdr.ipv4.dstAddr },
            hdr.ipv4.hdrChecksum,
            HashAlgorithm.csum16);
    }
}

control MyDeparser(packet_out packet, in headers hdr) {
    apply {
        packet.emit(hdr.ethernet);
        packet.emit(hdr.ipv4);
    }
}

V1Switch(
    MyParser(),
    MyVerifyChecksum(),
    MyIngress(),
    MyEgress(),
    MyComputeChecksum(),
    MyDeparser()
) main;
