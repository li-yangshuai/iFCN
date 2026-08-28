#!/usr/bin/env python3

import csv
import json
from pathlib import Path


def next_state(state: int, rst: int, en: int) -> int:
    if rst:
        return 0
    if en:
        return (state + 1) & 0b11
    return state


def normalized_equations(state: int, rst: int, en: int) -> int:
    q0 = state & 1
    q1 = (state >> 1) & 1
    q0_inc = 1 - q0
    q1_inc = q1 ^ q0
    d0_pre = q0_inc if en else q0
    d1_pre = q1_inc if en else q1
    d0 = 0 if rst else d0_pre
    d1 = 0 if rst else d1_pre
    return d0 | (d1 << 1)


def validate_contract(example_dir: Path) -> None:
    constraints = json.loads(
        (example_dir / "counter2_sync.constraints.json").read_text(encoding="utf-8")
    )
    seqir = json.loads(
        (example_dir / "counter2_sync.seqir.example.json").read_text(encoding="utf-8")
    )

    if constraints["top"] != "counter2_sync":
        raise AssertionError("constraints top module does not match the RTL")
    if constraints["clock_domains"] != [
        {"id": "clk0", "port": "clk", "edge": "posedge"}
    ]:
        raise AssertionError("unexpected logical clock contract")
    if constraints["physical"]["phase_origin"] != 0:
        raise AssertionError("the v0 example requires an explicit phase origin of zero")

    semantics = seqir["semantics"]
    if semantics["control_priority"] != ["reset", "enable", "data_or_hold"]:
        raise AssertionError("reset-over-enable priority is not explicit in SeqIR")
    if semantics["register_controls"] != "executable_high_level_transition_semantics":
        raise AssertionError("SeqIR register controls must be the sole executable semantics")

    registers = {register["id"]: register for register in seqir["registers"]}
    expected_registers = {
        "reg.q0": ("q0_inc", "q[0]"),
        "reg.q1": ("q1_inc", "q[1]"),
    }
    if set(registers) != set(expected_registers):
        raise AssertionError("SeqIR must contain exactly the two counter state bits")

    for register_id, (data, q) in expected_registers.items():
        register = registers[register_id]
        if register["data"] != data or register["q"] != q:
            raise AssertionError(f"unexpected data/Q mapping for {register_id}")
        if register["clock_domain"] != "clk0":
            raise AssertionError(f"unexpected clock domain for {register_id}")
        if register["enable"] != {"signal": "en", "polarity": "active_high"}:
            raise AssertionError(f"unexpected enable contract for {register_id}")
        if register["reset"] != {
            "signal": "rst",
            "kind": "synchronous",
            "polarity": "active_high",
            "value": 0,
        }:
            raise AssertionError(f"unexpected reset contract for {register_id}")


def main() -> None:
    example_dir = Path(__file__).parent
    trace_path = example_dir / "expected_trace.csv"
    state = None
    row_count = 0

    validate_contract(example_dir)

    with trace_path.open(newline="", encoding="utf-8") as trace_file:
        for expected_capture, row in enumerate(csv.DictReader(trace_file)):
            capture = int(row["capture"])
            rst = int(row["rst"])
            en = int(row["en"])

            if capture != expected_capture:
                raise AssertionError(
                    f"non-contiguous capture index: expected {expected_capture}, got {capture}"
                )

            if rst:
                state = 0
            elif state is None:
                raise AssertionError("state was observed before a valid reset capture")
            else:
                state = next_state(state, rst, en)

            observed = int(row["q"], 2)
            if observed != state:
                raise AssertionError(
                    f"capture {capture}: expected {state:02b}, trace contains {observed:02b}"
                )

            row_count += 1

    if row_count == 0:
        raise AssertionError("expected trace is empty")

    transition_count = 0
    for current_state in range(4):
        for rst in range(2):
            for en in range(2):
                expected = next_state(current_state, rst, en)
                observed = normalized_equations(current_state, rst, en)
                if observed != expected:
                    raise AssertionError(
                        "normalized equation mismatch: "
                        f"q={current_state:02b} rst={rst} en={en} "
                        f"expected={expected:02b} got={observed:02b}"
                    )
                transition_count += 1

    print(
        "counter2_sync reference model: PASS "
        f"({transition_count} transitions, {row_count} captures)"
    )


if __name__ == "__main__":
    main()
