**rP4->JSON Controller Information Definition** (2022.01.19)

*Global*

- SRAM: array, SRAM_NUM_ALL
    - width: SRAM_WIDTH
    - depth: SRAM_DEPTH
    - uint8_t tbl[SRAM_WIDTH * SRAM_DEPTH / 8]
- TCAM: array, TCAM_NUM_ALL
    - width: TCAM_WIDTH
    - depth: TCAM_DEPTH
    - uint8_t tbl[TCAM_WIDTH * TCAM_DEPTH / 8]
    - uint8_t mask[TCAM_WIDTH * TCAM_DEPTH / 8]

1. parser
    - parser_level: indexed by procId
    - parser_tcam_entry (key):
        - current_state: 8-bit
        - entry: 32-bit
        - mask: 32-bit
    - parser_sram_entry (value):
        - hdr_id: 8-bit
        - hdr_len: 16-bit
        - next_state: 8-bit
        - transition_field_num: 8-bit (Number of fields in the transition select)
        - transition_fields: list
            - hdr_id: 8-bit
            - field_internal_offset: 16-bit
            - field_length: 16-bit
            - field_type: FIELD, VALID, HIT, MISS (if VALID, only hdr_id, field_length work; if HIT/MISS, all three don't work)
        - miss_act: 8-bit, Indicates the action to execute in the next_state transition if no matching field is found: 0 accept, 1 drop
    - **update function**
        - clearParser(procId, parser_level)
        - insertParserEntry(current_state, entry, mask, hdr_id, hdr_len, next_state, transition_field_num, transition_fields)

2. gateway
    - bitmap: 8-bit, Value from switch()
    - exps: vector\<RelationExp\> Set of logical expressions
        - param1: GateParam
            - type: FIELD, CONSTANT
            - union value
                - constant_data: CONSTANT
                    - data_len
                    - val: uint8_t[]
                - field: FIELD
                    - hdr_id: 8-bit
                    - field_internal_offset: 16-bit
                    - field_len: 16-bit
                    - field_type: FIELD, VALID, HIT, MISS
        - param2: GateParam
        - relation
            - \> < >= <= == !=
    - res_next_proc: map\<bitmap, int\> Dictionary, the corresponding judgment value maps to the corresponding next_proc_id. If it equals cur_proc_id, execute the matcher below; otherwise, jump to the next proc.
    - res_next_matcher: map\<bitmap, int\> Dictionary, get the corresponding matcher_id based on the judgment value (if cur_proc_id == next_proc_id)
    - **update function**
        - clear_all()
        - insert_exp(exp)
        - clear_res_next()
        - modify_res_map(key, proc_id, matcher_id, action_id)


3. matcher
    - proc_id
    - miss_act_bitmap: uint8_t[MATCHER_THREAD_NUM], indicate whether to execute default action by 0 1
    - matcher_thread[16]: Each matcher will have 16 matcher_threads
        - proc_id
        - no_table: true, false, Whether this thread has a table
        - match_type: Exact, Ternary, LPM
        - SRAMs[SRAM_NUM_PER_CLUSTER]: pointer to global SRAMs
        - TCAMs[TCAM_NUM_PER_CLUSTER]: pointer to global TCAMs
        - union key_config
            - sram_key_config: uint8_t[SRAM_NUM_PER_CLUSTER]
            - tcam_key_config: uint8_t[TCAM_NUM_PER_CLUSTER]
        - union key_width
            - sram_slice_key_width: e.g., 200-bit key, 128-bit SRAM_WIDTH, the value is 2
            - tcam_slice_key_width
        - union key_depth
            - sram_slice_depth
            - tcam_slice_depth
        - sram_value_config: uint8_t[SRAM_NUM_PER_CLUSTER]
        - sram_slice_value_width
        - tcam_idx: incremental to insert TCAM entry
        - miss_act_id: if table miss, execute this action in executor
        - fieldInfos: the matching fields (vector)
            - hdr_id: 8-bit
            - field_internal_offset: 16-bit
            - field_length: 16-bit
            - field_type: FIELD, VALID, HIT, MISS
    - Note: In a matcher, the value is definitely SRAM. The key is determined to be SRAM or TCAM by the match_type. Each matcher_thread will point to the SRAM and TCAM of its cluster.
    - **update function**
        - clear_old_config(proc_id, matcher_id)
            - clear configuration
            - clear table
        - init/set_match_type(match_type)
        - set_no_table
        - set_mem_config(proc_id, matcher_id, key_width, value_width, depth, key_config, value_config)
        - set_match_field_info(vector\<FieldInfo\>)
            - hdr_id: 8-bit
            - field_internal_offset: 16-bit
            - field_len: 16-bit
            - field_type: FIELD, VALID, HIT, MISS
        - insert_sram_entry(key, value, key_byte_len, value_byte_len)
        - insert_tcam_entry(key, mask, value, key_byte_len, value_byte_len)

4. executor
    - action_num
    - actions: (vector)
        - parameter_num: Number of input parameters
        - action_para_lens: vector\<int\> Lengths of input parameters
        - action_paras: vector\<ActionParam\> Input parameters
            - action_para_len
            - val: uint8_t[]
        - primitives: vector\<Primitive\>
            - op: ADD, SUB, SET_FIELD, COPY_FIELD, NPB (write `"npb"` in JSON), SIGMOID (write `"sigmoid"`)
            - operand_num: Number of operands required by this primitive
            - operands: array(Operand)
                - type: CONSTANT, FIELD, PARAM (input parameter), HEADER(for push/pop)
                - union val
                    - header_id: HEADER
                    - action_para_id: PARAM
                    - field: FIELD
                        - hdr_id: 8-bit
                        - field_internal_offset: 16-bit
                        - field_len: 16-bit
                        - field_type: FIELD, VALID, HIT, MISS
                    - constant_data: CONSTANT
                        - data_len
                        - val: uint8_t[]
    - **update function**
        - clear_action()
        - delete_action(action_id)
        - set_action_para_value(value: uint8_t[])
        - insert_action(action: Action)

---

**rP4->JSON Description**

> Jumps between Processors: In the software switch, there is no concept of a crossbar between processors. After processing is completed within one processor, the next_proc_id is set, and this variable determines the next processor.

> Clustering: Currently configured with 16 processors, 4 clusters in total. Total of 128 SRAMs (128bit*1024) and 64 TCAMs (64bit*1024);

> Note: There is a case where after the judgment, no table is looked up, and an action is executed directly. Another map should be added to the gateway to indicate the next_action. When proc_id == cur_proc_id and matcher_id == -1, execute the next_action directly.

1. Parser Initialization and Online Updates
    - **parser_level** (set/init_parser_level): The number of parser levels for each processor;
    - **parser_entry**: The content of each parser level;
    - **Online Updates**
        - Clear the original parser content and re-insert new parser entries;
        - Note: Need to specify which processor
        - Three functions: ModParserEntry, ClearParser, SetParserLevel

2. Gateway Initialization and Online Updates
    - **Gateway Content**
        - The gateway within each processor consists of 8 logical judgments (8-bit bitmap). Logical judgment types: ==, !=, >, >=, <, <=
        - The comparison objects are header fields, metadata fields, constants, etc. Each logical judgment has two operands;
    - **Data Structure**
        - There are two maps in the gateway: next_proc_map and next_matcher_map. After the judgment is completed, the map is looked up based on the bitmap to get next_proc and next_matcher. If next_proc is the current proc, execute the matcher specified by next_matcher; otherwise, jump according to next_proc;
    - **Online Updates**
        - When this processor needs to be updated, first clear the gateway, then insert;
        - Three functions: InsertRelationExp, ModResMap, ClearResMap

3. Matcher Initialization and Online Updates
    - Each processor supports up to N matchers (currently 16). The gateway specifies which matcher to execute next;
    - **Need to know the matcher's config**: matchType, procId, matcherId, keyWidth, valueWidth, keyConfig, valueConfig. Among them, keyWidth is the width of the match key, and valueWidth is the width of the value, including the width of the action_id and the width of the action parameters.
        - keyConfig: Index array of SRAM/TCAM used by this matcher;
        - valueConfig: SRAM index array used for the value of this matcher;
        - match_type: Three types are currently supported: Exact, LPM, Ternary;
    - **Need to know the matcher's field set**: procId, matcherId, fieldInfos(array);
        - fieldInfo: hdrId, internalOffset, fdLen. A total of three attributes determine a field. metadata, standard_metadata, etc., are also treated as headers and given specific IDs. For example, if the maximum number of headers is set to 64, then standard_metadata's id is 62, and metadata's id is 63.
    - **Need to set each matcher's miss_act**: Since there are 16 matchers, this forms a bitmap. If the matcher misses and miss_act is 0, no operation is performed; if the matcher misses and miss_act is 1, the miss_action is executed.
    - When updating, all previous configurations need to be cleared and table contents deleted, and then the above content needs to be reset.
    - **Regarding miss/hit**: If the gateway check fails, it goes directly to the next proc. If it passes and the table lookup is a hit, the corresponding action is executed, and it jumps to the next_proc corresponding to that action. If it is a miss, the action specified by default is executed (this can be optional), but the next_proc must be specified in the action. (Question: Can the same action correspond to different next_procs?)

4. Executor Initialization and Online Updates
    - Each executor consists of one or more actions, and each action consists of multiple primitives;
    - **Primitive**: OpCode, paraNum, params
        - OpCode: ADD, SUB, SET_FIELD, COPY_FIELD
        - paraNum: Each Op has a different number of operands
        - params: Specify the operands. Their types are: field, constant, action's input parameter, and *header_id (used for push/pop, not considered for now)*;
    - **Action**: paraNum, prims, actionParams, actionParaLens, nextProcId
        - paraNum: Number of action input parameters
        - prims: Set of primitives that make up the action
        - actionParams: Types of input parameters
        - actionParaLens: Length of each input parameter
        - nextProcId: Each action is followed by the ID of the next processor
    - **Online Updates**
        - Unlimited number of actions are supported; existing actions do not need to be deleted;
        - InsertAction, and assign an ID to it.

---

**Online Updates**

1. Controller-side update command + JSON configuration
2. Parser update
    - First, flush the parser content and level within the proc_id;
    - Then, set the new parser_level and insert parser content at the specific level
    - Content: procId, parserLevel, state, key, mask, hdrId, hdrLen, nextState, transFieldNum, **transFds**
    - transFds: list(hdrId, internalOffset, fieldLen)
    - Update commands
        - delete_link father_protocol_node child_protocol_node: Delete the entry from where
        - add_link father_protocol_node child_protocol_node: Add the entry where
        - *Seems like flushing is not needed?*

3. Matcher update