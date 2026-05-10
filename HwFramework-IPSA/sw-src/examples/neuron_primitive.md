# Description on 
# Classifier Packet Flow (Essentials)

This is the minimal path a packet follows through the IPv4-option classifier.

## Parser
- Extract Ethernet + IPv4 headers (`classifier.p4`).
- If IHL shows options and the first option is `0x9e`, pull eight 16-bit feature words (`feat0`–`feat7`) and set `meta.features_valid = 1`.
- Otherwise skip options; missing features default to zero later.

## Ingress Packing
- Table `feature_logic` concatenates the eight 16-bit feature words into `metadata[30]` when the option is present, or clears it on miss.
- Neuron weights and biases are loaded from JSON as Q16 fixed-point coefficients. The feature words themselves remain signed 16-bit inputs.
- Each neuron context can apply an `output_shift` after the MAC stage and before activation so the example pipeline can be calibrated without rewriting the coefficients.
- A context-level `sigmoid` activation returns a 16-bit score, not a hard class bit. For binary classification in the dataplane, use a final linear neuron with `activation: "none"` and then apply the standalone `sigmoid` operator with output width `1`.

## Dataplane Primitive Chain
1. **Processors 0–5**: run contexts 0–5 (`npb`) on sliding 48-bit windows of `metadata[30]`:
   - p0 reads feature words 0..2
   - p1 reads feature words 1..3
   - p2 reads feature words 2..4
   - p3 reads feature words 3..5
   - p4 reads feature words 4..6
   - p5 reads feature words 5..7
   Each 32-bit result (2 neurons) lands in its own slice of `metadata[31]` (offsets 0–191).
2. **Processor 6**: uses back-to-back `sum_block`s to form four aggregated lanes:
   - p0+p1+p2 (first halves) → offset 192  
   - p0+p1+p2 (second halves) → offset 208  
   - p3+p4+p5 (first halves) → offset 224  
   - p3+p4+p5 (second halves) → offset 240  
   - padding regions are zeroed so later stages get deterministic inputs.
3. **Processor 7 (ctx 6)**: consumes the first 48-bit window (two sums + zero) and writes its output pair to offset 288.
4. **Processor 8 (ctx 7)**: consumes the second window and writes its output to offset 320.
5. **Processor 9 (ctx 8)**: reads 48 bits spanning offsets 288–335, runs the final neuron pair, and writes the classification value to `metadata[28]` (offset 656).

All offsets are defined in `sw-src/tables/metadata_context.json`.

## Decision & Egress
- Control logic copies `metadata[28]` into `meta.classification_result`.
- If `meta.classification_result` must be a true class label instead of a score, use `sigmoid(logit, 1)`: values below `0.5` become `0`, and values at or above `0.5` become `1`.
- Ingress drops or forwards based on that value; egress only rewrites MAC / recomputes checksum. No additional ML processing occurs past processor 9.
