#!/usr/bin/env python3
"""Import a bounded Yosys ``write_json`` netlist into iFCN SeqIR v0.

The importer is intentionally independent of Qt and the legacy CircuitGraph.
It accepts a flattened, process-free Yosys module, bit-blasts the supported
combinational cells, cuts state at positive-edge DFFs, and lowers synchronous
reset/enable controls exactly once into explicit muxes on each register D pin.

Supported combinational cells:
  $not/$and/$or/$xor/$mux and their $_..._ simple-cell forms.  $buf and
  $pos are accepted as explicit wire/buffer nodes because they commonly remain
  around cutpoints.

Supported register cells:
  $dff, $dffe, $sdff, $sdffe, $sdffce, and $_DFF_P_.  Only positive-edge
  clocks are accepted.  Asynchronous state, latches, memories, hierarchy, and
  every unknown cell fail closed with a diagnostic naming the offending cell.
"""

from __future__ import annotations

import argparse
import heapq
import json
import os
import re
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Mapping, MutableMapping, Optional, Sequence, Set, Tuple


SCHEMA = "ifcn.seqir.v0"
GENERATOR_NAME = "yosys_json_to_seqir"
GENERATOR_VERSION = 1


class SeqirImportError(ValueError):
    """A deterministic, user-actionable import failure."""


RawBit = Any


@dataclass
class _TempNode:
    key: str
    stable_key: Tuple[Any, ...]
    op: str
    inputs: Dict[str, str]
    output: str
    source: Dict[str, Any]
    node_id: str = ""


@dataclass
class _TempRegister:
    register_id: str
    q: str
    d: str
    clock: str
    source_cell: str
    source_type: str
    source_bit: int
    priority: List[str]
    control_provenance: Dict[str, Any] = field(default_factory=dict)
    clock_domain: str = ""


@dataclass(frozen=True)
class LegacyCutArtifacts:
    """Legacy ``Parse`` cut-DAG source plus its sequential state boundary map."""

    verilog: str
    state_manifest: Dict[str, Any]


def _clean_name(value: Any) -> str:
    text = str(value)
    return text[1:] if text.startswith("\\") else text


def _as_mapping(value: Any, description: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise SeqirImportError(f"{description} must be a JSON object")
    return value


def _as_bit_list(value: Any, description: str) -> List[RawBit]:
    if not isinstance(value, list):
        raise SeqirImportError(f"{description} must be a JSON bit array")
    return list(value)


def _constant_token(bit: RawBit) -> Optional[str]:
    if isinstance(bit, str) and bit.lower() in {"0", "1", "x", "z"}:
        return bit.lower()
    return None


def _wire_number(bit: RawBit, description: str) -> int:
    if isinstance(bit, bool) or not isinstance(bit, int) or bit < 0:
        raise SeqirImportError(f"{description} is not a non-negative Yosys wire bit: {bit!r}")
    return bit


def _truthy_encoded(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return value != 0
    if isinstance(value, str):
        text = value.strip().lower()
        if text in {"true", "yes", "on"}:
            return True
        if text in {"false", "no", "off", ""}:
            return False
        if re.fullmatch(r"[01]+", text):
            return "1" in text
        if re.fullmatch(r"[-+]?\d+", text):
            return int(text, 10) != 0
    return False


def _parameter_int(parameters: Mapping[str, Any], name: str, default: Optional[int] = None) -> int:
    if name not in parameters:
        if default is None:
            raise SeqirImportError(f"missing required Yosys parameter {name}")
        return default
    value = parameters[name]
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        text = value.strip().lower()
        if re.fullmatch(r"[01]+", text):
            return int(text, 2)
        if re.fullmatch(r"[-+]?\d+", text):
            return int(text, 10)
    raise SeqirImportError(f"Yosys parameter {name} is not a concrete integer: {value!r}")


def _parameter_bit(parameters: Mapping[str, Any], name: str, default: int = 1) -> int:
    value = _parameter_int(parameters, name, default)
    if value not in {0, 1}:
        raise SeqirImportError(f"Yosys parameter {name} must be 0 or 1, got {value}")
    return value


def _parameter_bits(parameters: Mapping[str, Any], name: str, width: int) -> List[str]:
    if width <= 0:
        raise SeqirImportError("register width must be positive")
    if name not in parameters:
        return ["0"] * width
    value = parameters[name]
    if isinstance(value, bool):
        value = int(value)
    if isinstance(value, int):
        if value < 0:
            value &= (1 << width) - 1
        encoded = format(value, f"0{width}b")[-width:]
    elif isinstance(value, str):
        encoded = value.strip().lower()
        if not re.fullmatch(r"[01xz]+", encoded):
            raise SeqirImportError(f"Yosys parameter {name} is not a bit vector: {value!r}")
        encoded = encoded[-width:].rjust(width, "0")
    else:
        raise SeqirImportError(f"Yosys parameter {name} is not a bit vector: {value!r}")
    if "x" in encoded or "z" in encoded:
        raise SeqirImportError(f"Yosys parameter {name} contains unsupported x/z bits")
    # Yosys connection arrays are LSB first; parameter strings are printed MSB first.
    return list(reversed(encoded))


def _display_bit_names(name: str, entry: Mapping[str, Any], bits: Sequence[RawBit]) -> List[str]:
    clean = _clean_name(name)
    if len(bits) == 1:
        return [clean]
    offset = entry.get("offset", 0)
    if isinstance(offset, bool) or not isinstance(offset, int):
        offset = 0
    return [f"{clean}[{offset + index}]" for index in range(len(bits))]


class _SignalNames:
    def __init__(self, module: Mapping[str, Any]):
        ports = _as_mapping(module.get("ports", {}), "module ports")
        netnames = _as_mapping(module.get("netnames", {}), "module netnames")
        cells = _as_mapping(module.get("cells", {}), "module cells")

        raw_numbers: Set[int] = set()
        aliases: MutableMapping[int, Set[str]] = {}
        candidates: MutableMapping[int, List[Tuple[int, str]]] = {}

        def observe_bits(bits: Sequence[RawBit]) -> None:
            for bit in bits:
                if _constant_token(bit) is None:
                    raw_numbers.add(_wire_number(bit, "signal"))

        for port_name, raw_entry in sorted(ports.items()):
            entry = _as_mapping(raw_entry, f"port {port_name}")
            bits = _as_bit_list(entry.get("bits"), f"port {port_name}.bits")
            observe_bits(bits)
            labels = _display_bit_names(port_name, entry, bits)
            for bit, label in zip(bits, labels):
                if _constant_token(bit) is not None:
                    continue
                number = _wire_number(bit, f"port {port_name}")
                aliases.setdefault(number, set()).add(label)
                candidates.setdefault(number, []).append((0, label))

        for net_name, raw_entry in sorted(netnames.items()):
            entry = _as_mapping(raw_entry, f"netname {net_name}")
            bits = _as_bit_list(entry.get("bits"), f"netname {net_name}.bits")
            observe_bits(bits)
            labels = _display_bit_names(net_name, entry, bits)
            hidden = _truthy_encoded(entry.get("hide_name", 0))
            for bit, label in zip(bits, labels):
                if _constant_token(bit) is not None:
                    continue
                number = _wire_number(bit, f"netname {net_name}")
                aliases.setdefault(number, set()).add(label)
                if not hidden and not label.startswith("$"):
                    candidates.setdefault(number, []).append((1, label))

        for cell_name, raw_cell in sorted(cells.items()):
            cell = _as_mapping(raw_cell, f"cell {cell_name}")
            connections = _as_mapping(cell.get("connections", {}), f"cell {cell_name}.connections")
            for port_name, raw_bits in connections.items():
                observe_bits(_as_bit_list(raw_bits, f"cell {cell_name}.{port_name}"))

        self._id_by_number: Dict[int, str] = {}
        self._aliases_by_number: Dict[int, List[str]] = {}
        used_ids: Dict[str, int] = {}
        for number in sorted(raw_numbers):
            choices = sorted(set(candidates.get(number, [])))
            signal_id = choices[0][1] if choices else f"bit.{number}"
            if signal_id in used_ids and used_ids[signal_id] != number:
                signal_id = f"{signal_id}@{number}"
            used_ids[signal_id] = number
            self._id_by_number[number] = signal_id
            self._aliases_by_number[number] = sorted(aliases.get(number, set()))

        self._used_constants: Set[str] = set()
        self._generated: Dict[str, Dict[str, Any]] = {}
        self._used_ids: Set[str] = set(self._id_by_number.values()) | {
            "const.0", "const.1", "const.x", "const.z"
        }

    def resolve(self, bit: RawBit, description: str) -> str:
        token = _constant_token(bit)
        if token is not None:
            if token in {"x", "z"}:
                raise SeqirImportError(f"{description} uses unsupported constant {token}")
            signal = f"const.{token}"
            self._used_constants.add(signal)
            return signal
        number = _wire_number(bit, description)
        try:
            return self._id_by_number[number]
        except KeyError as exc:
            raise SeqirImportError(f"{description} refers to unknown Yosys bit {number}") from exc

    def output(self, bit: RawBit, description: str) -> str:
        if _constant_token(bit) is not None:
            raise SeqirImportError(f"{description} drives a constant token")
        return self.resolve(bit, description)

    def generated(self, requested: str, kind: str, provenance: Mapping[str, Any]) -> str:
        signal_id = requested
        suffix = 0
        while signal_id in self._used_ids:
            suffix += 1
            signal_id = f"{requested}.{suffix}"
        self._used_ids.add(signal_id)
        self._generated[signal_id] = {
            "id": signal_id,
            "kind": kind,
            "provenance": dict(provenance),
        }
        return signal_id

    def payload(self) -> List[Dict[str, Any]]:
        result: List[Dict[str, Any]] = []
        for number, signal_id in sorted(self._id_by_number.items(), key=lambda item: item[1]):
            result.append({
                "id": signal_id,
                "kind": "wire",
                "yosys_bit": number,
                "aliases": self._aliases_by_number.get(number, []),
            })
        for signal in sorted(self._used_constants):
            result.append({"id": signal, "kind": "constant", "value": int(signal[-1])})
        result.extend(self._generated[key] for key in sorted(self._generated))
        return sorted(result, key=lambda entry: entry["id"])


_COMB_CELL_OPS = {
    "$not": "not",
    "$_NOT_": "not",
    "$and": "and",
    "$_AND_": "and",
    "$or": "or",
    "$_OR_": "or",
    "$xor": "xor",
    "$_XOR_": "xor",
    "$mux": "mux",
    "$_MUX_": "mux",
    "$buf": "buf",
    "$pos": "buf",
    "$_BUF_": "buf",
}

_REGISTER_CELLS = {"$dff", "$dffe", "$sdff", "$sdffe", "$sdffce", "$_DFF_P_"}


def _connection(cell: Mapping[str, Any], cell_name: str, port: str) -> List[RawBit]:
    connections = _as_mapping(cell.get("connections", {}), f"cell {cell_name}.connections")
    if port not in connections:
        raise SeqirImportError(f"cell {cell_name} ({cell.get('type')}) lacks port {port}")
    return _as_bit_list(connections[port], f"cell {cell_name}.{port}")


def _extend_bits(bits: Sequence[RawBit], width: int, signed: bool) -> List[RawBit]:
    if not bits:
        raise SeqirImportError("cannot extend an empty Yosys signal")
    result = list(bits[:width])
    extension: RawBit = bits[-1] if signed else "0"
    result.extend(extension for _ in range(width - len(result)))
    return result


def _build_comb_nodes(
    cells: Mapping[str, Any], signals: _SignalNames
) -> Tuple[List[_TempNode], List[Tuple[str, Mapping[str, Any]]]]:
    nodes: List[_TempNode] = []
    register_cells: List[Tuple[str, Mapping[str, Any]]] = []
    for raw_name, raw_cell in sorted(cells.items(), key=lambda item: _clean_name(item[0])):
        cell_name = _clean_name(raw_name)
        cell = _as_mapping(raw_cell, f"cell {cell_name}")
        cell_type = str(cell.get("type", ""))
        if cell_type in _REGISTER_CELLS:
            register_cells.append((cell_name, cell))
            continue
        if cell_type not in _COMB_CELL_OPS:
            if cell_type.startswith("$_DFF") or cell_type.startswith("$_SDFF") or cell_type.startswith("$_DFFE"):
                hint = "; run dffunmap or provide a supported positive-edge register cell"
            elif cell_type in {"$adff", "$adffe", "$aldff", "$aldffe", "$dlatch", "$adlatch", "$sr"}:
                hint = "; asynchronous state and latches are outside ifcn.seqir.v0"
            else:
                hint = ""
            raise SeqirImportError(
                f"unsupported Yosys cell {cell_name!r} of type {cell_type!r}{hint}"
            )

        op = _COMB_CELL_OPS[cell_type]
        parameters = _as_mapping(cell.get("parameters", {}), f"cell {cell_name}.parameters")
        outputs = _connection(cell, cell_name, "Y")
        if not outputs:
            raise SeqirImportError(f"cell {cell_name} has zero-width output")
        width = len(outputs)

        if op in {"not", "buf"}:
            a_raw = _connection(cell, cell_name, "A")
            signed = bool(_parameter_bit(parameters, "A_SIGNED", 0)) if cell_type.startswith("$") and not cell_type.startswith("$_") else False
            a_bits = _extend_bits(a_raw, width, signed)
            per_bit_inputs = [{"A": a_bits[index]} for index in range(width)]
        elif op in {"and", "or", "xor"}:
            a_raw = _connection(cell, cell_name, "A")
            b_raw = _connection(cell, cell_name, "B")
            coarse = cell_type.startswith("$") and not cell_type.startswith("$_")
            a_signed = bool(_parameter_bit(parameters, "A_SIGNED", 0)) if coarse else False
            b_signed = bool(_parameter_bit(parameters, "B_SIGNED", 0)) if coarse else False
            a_bits = _extend_bits(a_raw, width, a_signed)
            b_bits = _extend_bits(b_raw, width, b_signed)
            per_bit_inputs = [
                {"A": a_bits[index], "B": b_bits[index]} for index in range(width)
            ]
        elif op == "mux":
            a_bits = _extend_bits(_connection(cell, cell_name, "A"), width, False)
            b_bits = _extend_bits(_connection(cell, cell_name, "B"), width, False)
            select = _connection(cell, cell_name, "S")
            if len(select) != 1:
                raise SeqirImportError(f"cell {cell_name} mux select must be one bit")
            per_bit_inputs = [
                {"A": a_bits[index], "B": b_bits[index], "S": select[0]}
                for index in range(width)
            ]
        else:  # pragma: no cover - guarded by the table above
            raise AssertionError(op)

        for bit_index, (output_bit, raw_inputs) in enumerate(zip(outputs, per_bit_inputs)):
            inputs = {
                port: signals.resolve(bit, f"cell {cell_name}.{port}[{bit_index}]")
                for port, bit in raw_inputs.items()
            }
            nodes.append(_TempNode(
                key=f"cell:{cell_name}:{bit_index}",
                stable_key=(0, cell_name, bit_index),
                op=op,
                inputs=inputs,
                output=signals.output(output_bit, f"cell {cell_name}.Y[{bit_index}]"),
                source={"cell": cell_name, "type": cell_type, "bit": bit_index},
            ))
    return nodes, register_cells


def _parse_register_bits(
    register_cells: Sequence[Tuple[str, Mapping[str, Any]]], signals: _SignalNames
) -> Tuple[List[_TempRegister], List[_TempNode]]:
    raw_registers: List[Tuple[str, str, Mapping[str, Any], int, RawBit, RawBit, RawBit]] = []
    for cell_name, cell in register_cells:
        cell_type = str(cell.get("type"))
        parameters = _as_mapping(cell.get("parameters", {}), f"cell {cell_name}.parameters")
        clock_port = "C" if cell_type == "$_DFF_P_" else "CLK"
        clocks = _connection(cell, cell_name, clock_port)
        if len(clocks) != 1:
            raise SeqirImportError(f"register cell {cell_name} must have a one-bit clock")
        if cell_type != "$_DFF_P_" and _parameter_bit(parameters, "CLK_POLARITY", 1) != 1:
            raise SeqirImportError(f"register cell {cell_name} is not positive-edge triggered")
        q_bits = _connection(cell, cell_name, "Q")
        d_bits = _connection(cell, cell_name, "D")
        if not q_bits:
            raise SeqirImportError(f"register cell {cell_name} has zero width")
        if len(d_bits) != len(q_bits):
            raise SeqirImportError(
                f"register cell {cell_name} has mismatched D/Q widths {len(d_bits)}/{len(q_bits)}"
            )
        for bit_index, (d_bit, q_bit) in enumerate(zip(d_bits, q_bits)):
            raw_registers.append((cell_name, cell_type, cell, bit_index, d_bit, q_bit, clocks[0]))

    raw_registers.sort(key=lambda item: (item[0], item[3]))
    registers: List[_TempRegister] = []
    lower_nodes: List[_TempNode] = []

    for register_index, (cell_name, cell_type, cell, bit_index, d_bit, q_bit, clock_bit) in enumerate(raw_registers):
        register_id = f"reg.{register_index:04d}"
        parameters = _as_mapping(cell.get("parameters", {}), f"cell {cell_name}.parameters")
        q_signal = signals.output(q_bit, f"register {cell_name}.Q[{bit_index}]")
        current_d = signals.resolve(d_bit, f"register {cell_name}.D[{bit_index}]")
        clock_signal = signals.resolve(clock_bit, f"register {cell_name}.clock")
        provenance: Dict[str, Any] = {}

        enable_signal: Optional[str] = None
        enable_polarity = 1
        if cell_type in {"$dffe", "$sdffe", "$sdffce"}:
            enable_bits = _connection(cell, cell_name, "EN")
            if len(enable_bits) != 1:
                raise SeqirImportError(f"register cell {cell_name} enable must be one bit")
            enable_signal = signals.resolve(enable_bits[0], f"register {cell_name}.EN")
            enable_polarity = _parameter_bit(parameters, "EN_POLARITY", 1)
            provenance["enable"] = {
                "signal": enable_signal,
                "polarity": "active_high" if enable_polarity else "active_low",
            }

        reset_signal: Optional[str] = None
        reset_polarity = 1
        reset_value: Optional[str] = None
        if cell_type in {"$sdff", "$sdffe", "$sdffce"}:
            reset_bits = _connection(cell, cell_name, "SRST")
            if len(reset_bits) != 1:
                raise SeqirImportError(f"register cell {cell_name} synchronous reset must be one bit")
            reset_signal = signals.resolve(reset_bits[0], f"register {cell_name}.SRST")
            reset_polarity = _parameter_bit(parameters, "SRST_POLARITY", 1)
            reset_value = f"const.{_parameter_bits(parameters, 'SRST_VALUE', len(_connection(cell, cell_name, 'Q')))[bit_index]}"
            # Mark the generated literal as used through the normal resolver.
            signals.resolve(reset_value[-1], f"register {cell_name}.SRST_VALUE[{bit_index}]")
            provenance["reset"] = {
                "signal": reset_signal,
                "kind": "synchronous",
                "polarity": "active_high" if reset_polarity else "active_low",
                "value": int(reset_value[-1]),
            }

        def add_enable_mux(data_signal: str, stage: int) -> str:
            assert enable_signal is not None
            output = signals.generated(
                f"internal.{register_id}.d_enable",
                "lowered_control",
                {"register": register_id, "control": "enable"},
            )
            if enable_polarity:
                inputs = {"A": q_signal, "B": data_signal, "S": enable_signal}
            else:
                inputs = {"A": data_signal, "B": q_signal, "S": enable_signal}
            lower_nodes.append(_TempNode(
                key=f"lower:{register_id}:enable",
                stable_key=(1, register_index, stage, "enable"),
                op="mux",
                inputs=inputs,
                output=output,
                source={"cell": cell_name, "type": cell_type, "lowering": "enable", "bit": bit_index},
            ))
            return output

        def add_reset_mux(data_signal: str, stage: int) -> str:
            assert reset_signal is not None and reset_value is not None
            output = signals.generated(
                f"internal.{register_id}.d_reset",
                "lowered_control",
                {"register": register_id, "control": "synchronous_reset"},
            )
            if reset_polarity:
                inputs = {"A": data_signal, "B": reset_value, "S": reset_signal}
            else:
                inputs = {"A": reset_value, "B": data_signal, "S": reset_signal}
            lower_nodes.append(_TempNode(
                key=f"lower:{register_id}:reset",
                stable_key=(1, register_index, stage, "reset"),
                op="mux",
                inputs=inputs,
                output=output,
                source={"cell": cell_name, "type": cell_type, "lowering": "synchronous_reset", "bit": bit_index},
            ))
            return output

        if cell_type == "$dffe":
            current_d = add_enable_mux(current_d, 0)
            priority = ["enable", "data_or_hold"]
        elif cell_type == "$sdff":
            current_d = add_reset_mux(current_d, 0)
            priority = ["reset", "data"]
        elif cell_type == "$sdffe":
            # $sdffe has synchronous reset priority over enable.
            current_d = add_enable_mux(current_d, 0)
            current_d = add_reset_mux(current_d, 1)
            priority = ["reset", "enable", "data_or_hold"]
        elif cell_type == "$sdffce":
            # $sdffce has clock-enable priority over synchronous reset.
            current_d = add_reset_mux(current_d, 0)
            current_d = add_enable_mux(current_d, 1)
            priority = ["enable", "reset", "data_or_hold"]
        else:
            priority = ["data"]

        registers.append(_TempRegister(
            register_id=register_id,
            q=q_signal,
            d=current_d,
            clock=clock_signal,
            source_cell=cell_name,
            source_type=cell_type,
            source_bit=bit_index,
            priority=priority,
            control_provenance=provenance,
        ))

    return registers, lower_nodes


def _topological_nodes(nodes: Sequence[_TempNode]) -> List[_TempNode]:
    by_key = {node.key: node for node in nodes}
    if len(by_key) != len(nodes):
        raise SeqirImportError("internal node key collision while importing Yosys JSON")
    producer: Dict[str, str] = {}
    for node in nodes:
        if node.output in producer:
            raise SeqirImportError(
                f"signal {node.output} has multiple combinational drivers: {producer[node.output]} and {node.key}"
            )
        producer[node.output] = node.key

    dependencies: Dict[str, Set[str]] = {node.key: set() for node in nodes}
    users: Dict[str, Set[str]] = {node.key: set() for node in nodes}
    for node in nodes:
        for signal in node.inputs.values():
            dependency = producer.get(signal)
            if dependency is None:
                continue
            dependencies[node.key].add(dependency)
            users[dependency].add(node.key)

    ready: List[Tuple[Tuple[Any, ...], str]] = []
    for key, deps in dependencies.items():
        if not deps:
            heapq.heappush(ready, (by_key[key].stable_key, key))

    ordered: List[_TempNode] = []
    while ready:
        _, key = heapq.heappop(ready)
        ordered.append(by_key[key])
        for user in sorted(users[key], key=lambda item: by_key[item].stable_key):
            dependencies[user].discard(key)
            if not dependencies[user]:
                heapq.heappush(ready, (by_key[user].stable_key, user))

    if len(ordered) != len(nodes):
        cyclic = sorted(key for key, deps in dependencies.items() if deps)
        raise SeqirImportError(
            "combinational cycle remains after cutting registers: " + ", ".join(cyclic)
        )
    for index, node in enumerate(ordered):
        node.node_id = f"comb.{index:04d}"
    return ordered


def _select_module(document: Mapping[str, Any], requested_top: Optional[str]) -> Tuple[str, Mapping[str, Any]]:
    modules = _as_mapping(document.get("modules"), "Yosys modules")
    if not modules:
        raise SeqirImportError("Yosys JSON contains no modules")
    normalized = {_clean_name(name): module for name, module in modules.items()}
    if len(normalized) != len(modules):
        raise SeqirImportError("module names collide after Yosys escape normalization")
    if requested_top is not None:
        top = _clean_name(requested_top)
        if top not in normalized:
            raise SeqirImportError(
                f"requested top module {top!r} is absent; available: {', '.join(sorted(normalized))}"
            )
        return top, _as_mapping(normalized[top], f"module {top}")

    attributed: List[str] = []
    for name, raw_module in normalized.items():
        module = _as_mapping(raw_module, f"module {name}")
        attributes = _as_mapping(module.get("attributes", {}), f"module {name}.attributes")
        if _truthy_encoded(attributes.get("top", 0)):
            attributed.append(name)
    if len(attributed) == 1:
        top = attributed[0]
        return top, _as_mapping(normalized[top], f"module {top}")
    if len(normalized) == 1:
        top = next(iter(normalized))
        return top, _as_mapping(normalized[top], f"module {top}")
    if not attributed:
        raise SeqirImportError(
            "Yosys JSON has multiple modules and no unique top attribute; pass --top"
        )
    raise SeqirImportError(
        "Yosys JSON marks multiple top modules: " + ", ".join(sorted(attributed))
    )


def _port_payload(
    module: Mapping[str, Any], signals: _SignalNames
) -> Tuple[List[Dict[str, Any]], Dict[str, Tuple[str, int]], Set[str]]:
    ports = _as_mapping(module.get("ports", {}), "module ports")
    payload: List[Dict[str, Any]] = []
    input_driver: Dict[str, Tuple[str, int]] = {}
    output_signals: Set[str] = set()
    for raw_name, raw_entry in sorted(ports.items(), key=lambda item: _clean_name(item[0])):
        name = _clean_name(raw_name)
        entry = _as_mapping(raw_entry, f"port {name}")
        direction = str(entry.get("direction", ""))
        if direction not in {"input", "output"}:
            raise SeqirImportError(f"port {name} has unsupported direction {direction!r}")
        raw_bits = _as_bit_list(entry.get("bits"), f"port {name}.bits")
        bits = [signals.resolve(bit, f"port {name}[{index}]") for index, bit in enumerate(raw_bits)]
        if not bits:
            raise SeqirImportError(f"port {name} has zero width")
        if direction == "input":
            for index, signal in enumerate(bits):
                previous = input_driver.setdefault(signal, (name, index))
                if previous != (name, index):
                    # Aliased input ports are legal electrically, but ambiguous as a stable source.
                    raise SeqirImportError(
                        f"signal {signal} is driven by aliased input ports {previous[0]} and {name}"
                    )
        else:
            output_signals.update(bits)
        payload.append({
            "id": name,
            "direction": direction,
            "width": len(bits),
            "bits": bits,
            "roles": [],
        })
    return payload, input_driver, output_signals


def _validate_drivers(
    nodes: Sequence[_TempNode],
    registers: Sequence[_TempRegister],
    input_driver: Mapping[str, Tuple[str, int]],
    output_signals: Set[str],
) -> Dict[str, Dict[str, Any]]:
    drivers: Dict[str, Dict[str, Any]] = {
        signal: {"kind": "port", "id": port, "bit": bit}
        for signal, (port, bit) in input_driver.items()
    }

    def add_driver(signal: str, driver: Dict[str, Any]) -> None:
        if signal.startswith("const."):
            raise SeqirImportError(f"attempted to drive literal {signal}")
        if signal in drivers:
            raise SeqirImportError(
                f"signal {signal} has multiple drivers: {drivers[signal]} and {driver}"
            )
        drivers[signal] = driver

    for register in registers:
        add_driver(register.q, {"kind": "register", "id": register.register_id, "port": "Q"})
    for node in nodes:
        add_driver(node.output, {"kind": "node", "id": node.node_id, "port": "Y"})

    known = set(drivers) | {"const.0", "const.1"}
    for node in nodes:
        for port, signal in sorted(node.inputs.items()):
            if signal not in known:
                raise SeqirImportError(
                    f"node {node.node_id} input {port} uses undriven signal {signal}"
                )
    for register in registers:
        if register.d not in known:
            raise SeqirImportError(
                f"register {register.register_id} D uses undriven signal {register.d}"
            )
        if register.clock not in input_driver:
            raise SeqirImportError(
                f"register {register.register_id} clock {register.clock} is not a module input"
            )
    for signal in sorted(output_signals):
        if signal not in known:
            raise SeqirImportError(f"output signal {signal} is undriven")
    return drivers


def _assign_clock_domains(registers: Sequence[_TempRegister]) -> List[Dict[str, Any]]:
    clocks = sorted({register.clock for register in registers})
    domain_by_clock = {clock: f"clock.{index:04d}" for index, clock in enumerate(clocks)}
    for register in registers:
        register.clock_domain = domain_by_clock[register.clock]
    return [
        {"id": domain_by_clock[clock], "signal": clock, "edge": "posedge"}
        for clock in clocks
    ]


def _mark_port_roles(
    ports: List[Dict[str, Any]],
    registers: Sequence[_TempRegister],
) -> None:
    roles_by_signal: Dict[str, Set[str]] = {}
    for register in registers:
        roles_by_signal.setdefault(register.clock, set()).add("logical_clock")
        reset = register.control_provenance.get("reset")
        if reset:
            roles_by_signal.setdefault(reset["signal"], set()).add("synchronous_reset")
        enable = register.control_provenance.get("enable")
        if enable:
            roles_by_signal.setdefault(enable["signal"], set()).add("register_enable")
    for port in ports:
        roles: Set[str] = set()
        for signal in port["bits"]:
            roles.update(roles_by_signal.get(signal, set()))
        port["roles"] = sorted(roles)


def _data_nets(
    nodes: Sequence[_TempNode],
    registers: Sequence[_TempRegister],
    ports: Sequence[Mapping[str, Any]],
    drivers: Mapping[str, Dict[str, Any]],
) -> List[Dict[str, Any]]:
    sinks: Dict[str, List[Dict[str, Any]]] = {}
    for node in nodes:
        for port, signal in sorted(node.inputs.items()):
            sinks.setdefault(signal, []).append({"kind": "node", "id": node.node_id, "port": port})
    for register in registers:
        sinks.setdefault(register.d, []).append({"kind": "register", "id": register.register_id, "port": "D"})
    for port in ports:
        if port["direction"] != "output":
            continue
        for bit_index, signal in enumerate(port["bits"]):
            sinks.setdefault(signal, []).append({"kind": "port", "id": port["id"], "bit": bit_index})

    payload: List[Dict[str, Any]] = []
    for index, signal in enumerate(sorted(sinks)):
        driver = drivers.get(signal)
        if driver is None:
            if signal.startswith("const."):
                driver = {"kind": "constant", "value": int(signal[-1])}
            else:  # validated earlier
                raise AssertionError(signal)
        ordered_sinks = sorted(
            sinks[signal],
            key=lambda item: (
                item["kind"], item["id"], str(item.get("port", "")), int(item.get("bit", -1))
            ),
        )
        payload.append({
            "id": f"net.{index:04d}",
            "signal": signal,
            "driver": dict(driver),
            "sinks": ordered_sinks,
        })
    return payload


def import_yosys_json(document: Mapping[str, Any], top: Optional[str] = None) -> Dict[str, Any]:
    """Return a deterministic ``ifcn.seqir.v0`` JSON-compatible object."""
    if not isinstance(document, Mapping):
        raise SeqirImportError("Yosys document must be a JSON object")
    module_name, module = _select_module(document, top)
    memories = _as_mapping(module.get("memories", {}), f"module {module_name}.memories")
    if memories:
        raise SeqirImportError(
            f"module {module_name} contains unsupported memories: {', '.join(sorted(memories))}"
        )

    signals = _SignalNames(module)
    ports, input_driver, output_signals = _port_payload(module, signals)
    cells = _as_mapping(module.get("cells", {}), f"module {module_name}.cells")
    nodes, register_cells = _build_comb_nodes(cells, signals)
    registers, lower_nodes = _parse_register_bits(register_cells, signals)
    if not registers:
        raise SeqirImportError(f"module {module_name} contains no supported positive-edge registers")
    nodes.extend(lower_nodes)
    ordered_nodes = _topological_nodes(nodes)
    drivers = _validate_drivers(ordered_nodes, registers, input_driver, output_signals)
    clock_domains = _assign_clock_domains(registers)
    _mark_port_roles(ports, registers)

    combinational_nodes = [
        {
            "id": node.node_id,
            "op": node.op,
            "inputs": dict(sorted(node.inputs.items())),
            "output": node.output,
            "source": node.source,
        }
        for node in ordered_nodes
    ]
    register_payload = [
        {
            "id": register.register_id,
            "kind": "plain_posedge_dff",
            "q": register.q,
            "d": register.d,
            "width": 1,
            "clock_domain": register.clock_domain,
            "init": "unknown",
            "controls_lowered_into_d": bool(register.control_provenance),
            "control_priority": register.priority,
            "control_provenance": register.control_provenance,
            "source": {
                "cell": register.source_cell,
                "type": register.source_type,
                "bit": register.source_bit,
            },
        }
        for register in registers
    ]
    state_edges = [
        {
            "id": f"state.{index:04d}",
            "data": register.d,
            "to": register.q,
            "register": register.register_id,
            "logical_latency_ticks": 1,
        }
        for index, register in enumerate(registers)
    ]

    clock_signals = {domain["signal"] for domain in clock_domains}
    pseudo_inputs = {
        signal
        for port in ports
        if port["direction"] == "input"
        for signal in port["bits"]
        if signal not in clock_signals
    }
    pseudo_inputs.update(register.q for register in registers)
    pseudo_outputs = set(output_signals)
    pseudo_outputs.update(register.d for register in registers)

    creator = document.get("creator")
    source: Dict[str, Any] = {"format": "yosys.write_json"}
    if isinstance(creator, str) and creator:
        source["creator"] = creator

    return {
        "schema": SCHEMA,
        "generator": {"name": GENERATOR_NAME, "version": GENERATOR_VERSION},
        "module": module_name,
        "source": source,
        "semantics": {
            "register_model": "plain_posedge_dff",
            "state_cut": "D(t)->Q(t+1)",
            "control_lowering": "synchronous_reset_enable_exactly_once_to_d_muxes",
            "mux_semantics": "Y = S ? B : A",
        },
        "ports": ports,
        "signals": signals.payload(),
        "clock_domains": clock_domains,
        "registers": register_payload,
        "combinational_nodes": combinational_nodes,
        "data_nets": _data_nets(ordered_nodes, registers, ports, drivers),
        "state_edges": state_edges,
        "comb_regions": [{
            "id": "comb_region.0000",
            "nodes": [node.node_id for node in ordered_nodes],
            "pseudo_inputs": sorted(pseudo_inputs),
            "pseudo_outputs": sorted(pseudo_outputs),
            "must_be_dag": True,
        }],
        "diagnostics": {
            "source_cells": len(cells),
            "register_bits": len(registers),
            "control_muxes_inserted": len(lower_nodes),
            "combinational_bits": len(ordered_nodes),
        },
    }


def _seqir_object_list(payload: Mapping[str, Any], key: str) -> List[Mapping[str, Any]]:
    value = payload.get(key)
    if not isinstance(value, list):
        raise SeqirImportError(f"SeqIR {key} must be a JSON array")
    return [
        _as_mapping(entry, f"SeqIR {key}[{index}]")
        for index, entry in enumerate(value)
    ]


def _seqir_string(value: Any, description: str) -> str:
    if not isinstance(value, str) or not value:
        raise SeqirImportError(f"{description} must be a non-empty string")
    return value


def _legacy_identifier_fragment(value: str) -> str:
    fragment = re.sub(r"[^A-Za-z0-9_]", "_", value)
    fragment = re.sub(r"_+", "_", fragment).strip("_")
    return fragment or "unnamed"


def build_legacy_cut_artifacts(
    payload: Mapping[str, Any], *, compact_names: bool = False
) -> LegacyCutArtifacts:
    """Build the strict AOI cut DAG consumed by the legacy C++ ``Parse``.

    Every register Q becomes a scalar pseudo input and every register D becomes
    a distinct scalar pseudo output.  Register clocks are excluded.  Only the
    transitive fan-in of the D events is emitted, so feedback is cut at state
    boundaries and the resulting network is a true DAG.

    Legacy ``Parse`` cannot represent constants.  A constant in a retained D
    cone therefore fails explicitly instead of silently turning it into a
    primary input.  XOR and mux nodes are decomposed into single-operation
    NOT/AND/OR assignments before rendering.
    """
    if not isinstance(payload, Mapping):
        raise SeqirImportError("SeqIR payload must be a JSON object")
    if payload.get("schema") != SCHEMA:
        raise SeqirImportError(
            f"legacy cut export requires schema {SCHEMA!r}, got {payload.get('schema')!r}"
        )

    module_name = _seqir_string(payload.get("module"), "SeqIR module")
    raw_nodes = _seqir_object_list(payload, "combinational_nodes")
    raw_registers = _seqir_object_list(payload, "registers")
    raw_ports = _seqir_object_list(payload, "ports")
    raw_domains = _seqir_object_list(payload, "clock_domains")
    if not raw_registers:
        raise SeqirImportError("legacy cut export requires at least one register")

    nodes: List[Dict[str, Any]] = []
    node_by_output: Dict[str, Dict[str, Any]] = {}
    node_ids: Set[str] = set()
    for index, raw_node in enumerate(raw_nodes):
        node_id = _seqir_string(raw_node.get("id"), f"SeqIR node[{index}].id")
        if node_id in node_ids:
            raise SeqirImportError(f"duplicate SeqIR combinational node id {node_id}")
        node_ids.add(node_id)
        op = _seqir_string(raw_node.get("op"), f"SeqIR node {node_id}.op")
        if op not in {"not", "and", "or", "xor", "mux", "buf"}:
            raise SeqirImportError(
                f"legacy cut export does not support SeqIR op {op!r} at {node_id}"
            )
        output = _seqir_string(raw_node.get("output"), f"SeqIR node {node_id}.output")
        if output in node_by_output:
            raise SeqirImportError(
                f"SeqIR signal {output} has multiple combinational producers"
            )
        raw_inputs = _as_mapping(raw_node.get("inputs"), f"SeqIR node {node_id}.inputs")
        inputs = {
            _seqir_string(port, f"SeqIR node {node_id} input port"): _seqir_string(
                signal, f"SeqIR node {node_id}.{port}"
            )
            for port, signal in raw_inputs.items()
        }
        expected_ports = {
            "not": {"A"},
            "buf": {"A"},
            "and": {"A", "B"},
            "or": {"A", "B"},
            "xor": {"A", "B"},
            "mux": {"A", "B", "S"},
        }[op]
        if set(inputs) != expected_ports:
            raise SeqirImportError(
                f"SeqIR node {node_id} ({op}) has ports {sorted(inputs)}, "
                f"expected {sorted(expected_ports)}"
            )
        node = {
            "id": node_id,
            "op": op,
            "inputs": inputs,
            "output": output,
            "source": dict(_as_mapping(raw_node.get("source", {}), f"SeqIR node {node_id}.source")),
            "ordinal": index,
        }
        nodes.append(node)
        node_by_output[output] = node

    registers: List[Dict[str, Any]] = []
    register_ids: Set[str] = set()
    q_signals: Set[str] = set()
    for index, raw_register in enumerate(raw_registers):
        register_id = _seqir_string(
            raw_register.get("id"), f"SeqIR register[{index}].id"
        )
        if register_id in register_ids:
            raise SeqirImportError(f"duplicate SeqIR register id {register_id}")
        register_ids.add(register_id)
        q_signal = _seqir_string(
            raw_register.get("q"), f"SeqIR register {register_id}.q"
        )
        if q_signal in q_signals:
            raise SeqirImportError(f"multiple SeqIR registers drive Q signal {q_signal}")
        q_signals.add(q_signal)
        registers.append({
            "id": register_id,
            "q": q_signal,
            "d": _seqir_string(
                raw_register.get("d"), f"SeqIR register {register_id}.d"
            ),
            "source": dict(_as_mapping(
                raw_register.get("source", {}), f"SeqIR register {register_id}.source"
            )),
        })
    registers.sort(key=lambda entry: entry["id"])

    clock_signals: Set[str] = set()
    for index, domain in enumerate(raw_domains):
        clock_signals.add(_seqir_string(
            domain.get("signal"), f"SeqIR clock_domains[{index}].signal"
        ))

    input_bits: Dict[str, Dict[str, Any]] = {}
    nonclock_input_order: List[str] = []
    for port_ordinal, port in enumerate(sorted(
        raw_ports, key=lambda entry: str(entry.get("id", ""))
    )):
        port_id = _seqir_string(port.get("id"), f"SeqIR port[{port_ordinal}].id")
        direction = _seqir_string(
            port.get("direction"), f"SeqIR port {port_id}.direction"
        )
        bits = port.get("bits")
        if not isinstance(bits, list):
            raise SeqirImportError(f"SeqIR port {port_id}.bits must be a JSON array")
        if direction != "input":
            continue
        for bit_index, raw_signal in enumerate(bits):
            signal = _seqir_string(raw_signal, f"SeqIR port {port_id}[{bit_index}]")
            if signal in clock_signals:
                continue
            if signal in input_bits:
                previous = input_bits[signal]
                raise SeqirImportError(
                    f"SeqIR input signal {signal} aliases ports "
                    f"{previous['port']} and {port_id}"
                )
            input_ordinal = len(nonclock_input_order)
            event = (
                f"i{input_ordinal}"
                if compact_names
                else (
                    f"ifcn_pi_{_legacy_identifier_fragment(port_id)}_"
                    f"{input_ordinal:04d}"
                )
            )
            input_bits[signal] = {
                "signal": signal,
                "event": event,
                "port": port_id,
                "bit": bit_index,
            }
            nonclock_input_order.append(signal)

    # Walk backwards from every register D to retain only the capture cones.
    needed_signals: Set[str] = set()
    required_node_ids: Set[str] = set()
    pending = [register["d"] for register in reversed(registers)]
    while pending:
        signal = pending.pop()
        if signal in needed_signals:
            continue
        needed_signals.add(signal)
        producer = node_by_output.get(signal)
        if producer is None:
            continue
        required_node_ids.add(producer["id"])
        pending.extend(reversed([
            producer["inputs"][port]
            for port in sorted(producer["inputs"])
        ]))

    for signal in sorted(needed_signals):
        if signal in node_by_output or signal in q_signals or signal in input_bits:
            continue
        if signal in {"const.0", "const.1"}:
            raise SeqirImportError(
                f"legacy Parse cannot represent constant {signal} in a retained D cone"
            )
        if signal in clock_signals:
            raise SeqirImportError(
                f"logical clock {signal} participates in a retained data cone"
            )
        raise SeqirImportError(
            f"retained D cone uses signal {signal} without a combinational, Q, or input source"
        )

    required_nodes = [node for node in nodes if node["id"] in required_node_ids]
    q_event_by_signal = {
        register["q"]: (f"q{index}" if compact_names else f"ifcn_q_{index:04d}")
        for index, register in enumerate(registers)
    }
    d_event_by_register = {
        register["id"]: (f"d{index}" if compact_names else f"ifcn_d_{index:04d}")
        for index, register in enumerate(registers)
    }
    registers_by_d_signal: Dict[str, List[Dict[str, Any]]] = {}
    for register in registers:
        registers_by_d_signal.setdefault(register["d"], []).append(register)
    direct_d_event_by_signal = {
        signal: d_event_by_register[users[0]["id"]]
        for signal, users in registers_by_d_signal.items()
        if len(users) == 1 and signal in node_by_output
    }
    node_event_by_signal = {
        node["output"]: direct_d_event_by_signal.get(
            node["output"],
            (
                f"n{node['ordinal']}"
                if compact_names
                else f"ifcn_n_{node['ordinal']:04d}"
            ),
        )
        for node in required_nodes
    }
    primary_input_events = [
        input_bits[signal]
        for signal in nonclock_input_order
        if signal in needed_signals
    ]
    signal_event: Dict[str, str] = dict(q_event_by_signal)
    signal_event.update({entry["signal"]: entry["event"] for entry in primary_input_events})
    signal_event.update(node_event_by_signal)

    q_events = [q_event_by_signal[register["q"]] for register in registers]
    d_events = [d_event_by_register[register["id"]] for register in registers]
    input_events = q_events + [entry["event"] for entry in primary_input_events]
    assignments: List[str] = []
    internal_wires = [
        node_event_by_signal[node["output"]]
        for node in required_nodes
        if node_event_by_signal[node["output"]] not in d_events
    ]
    temporary_wires: List[str] = []

    def mapped(signal: str, description: str) -> str:
        try:
            return signal_event[signal]
        except KeyError as exc:
            raise SeqirImportError(
                f"{description} cannot map retained signal {signal} to a legacy event"
            ) from exc

    def temporary() -> str:
        name = (
            f"t{len(temporary_wires)}"
            if compact_names
            else f"ifcn_tmp_{len(temporary_wires):04d}"
        )
        temporary_wires.append(name)
        return name

    for node in required_nodes:
        node_id = node["id"]
        inputs = {
            port: mapped(signal, f"SeqIR node {node_id}.{port}")
            for port, signal in node["inputs"].items()
        }
        output = mapped(node["output"], f"SeqIR node {node_id}.output")
        op = node["op"]
        if op == "buf":
            assignments.append(f"assign {output}={inputs['A']};")
        elif op == "not":
            assignments.append(f"assign {output}=~{inputs['A']};")
        elif op == "and":
            assignments.append(f"assign {output}={inputs['A']}&{inputs['B']};")
        elif op == "or":
            assignments.append(f"assign {output}={inputs['A']}|{inputs['B']};")
        elif op == "xor":
            not_a = temporary()
            not_b = temporary()
            a_and_not_b = temporary()
            not_a_and_b = temporary()
            assignments.extend([
                f"assign {not_a}=~{inputs['A']};",
                f"assign {not_b}=~{inputs['B']};",
                f"assign {a_and_not_b}={inputs['A']}&{not_b};",
                f"assign {not_a_and_b}={not_a}&{inputs['B']};",
                f"assign {output}={a_and_not_b}|{not_a_and_b};",
            ])
        elif op == "mux":
            not_select = temporary()
            select_a = temporary()
            select_b = temporary()
            assignments.extend([
                f"assign {not_select}=~{inputs['S']};",
                f"assign {select_a}={inputs['A']}&{not_select};",
                f"assign {select_b}={inputs['B']}&{inputs['S']};",
                f"assign {output}={select_a}|{select_b};",
            ])
        else:  # pragma: no cover - checked above
            raise AssertionError(op)

    state_boundaries: List[Dict[str, Any]] = []
    state_arguments: List[str] = []
    for register in registers:
        d_event = d_event_by_register[register["id"]]
        q_event = q_event_by_signal[register["q"]]
        d_source = mapped(
            register["d"], f"SeqIR register {register['id']}.d"
        )
        if d_source != d_event:
            assignments.append(f"assign {d_event}={d_source};")
        argument = f"{d_event}:{q_event}"
        state_arguments.append(argument)
        state_boundaries.append({
            "id": register["id"],
            "data_event": d_event,
            "q_event": q_event,
            "data_signal": register["d"],
            "q_signal": register["q"],
            "iteration_distance": 1,
            "latency_epochs": 0,
            "state_argument": argument,
            "source": register["source"],
        })

    cut_module = f"ifcn_cut_{_legacy_identifier_fragment(module_name)}"
    all_ports = input_events + d_events
    wires = d_events + internal_wires + temporary_wires
    verilog_lines = [
        f"module {cut_module}({','.join(all_ports)});",
        f"input {','.join(input_events)};",
        f"output {','.join(d_events)};",
        f"wire {','.join(wires)};",
        *assignments,
        "endmodule",
    ]
    verilog = "\n".join(verilog_lines) + "\n"

    manifest = {
        "schema": "ifcn.state_manifest.v0",
        "generator": {"name": GENERATOR_NAME, "version": GENERATOR_VERSION},
        "source_module": module_name,
        "seqir_schema": SCHEMA,
        "cut_dag": {
            "module": cut_module,
            "inputs": input_events,
            "outputs": d_events,
            "primary_inputs": primary_input_events,
            "clock_signals_excluded": sorted(clock_signals),
            "combinational_nodes_retained": len(required_nodes),
            "legacy_operations": ["buf", "not", "and", "or"],
        },
        "state_boundaries": state_boundaries,
        "ifcn_sequential_pnr": {
            "state_arguments": state_arguments,
            "argv": [item for argument in state_arguments for item in ("--state", argument)],
        },
    }
    return LegacyCutArtifacts(verilog=verilog, state_manifest=manifest)


def _read_json(path: str) -> Mapping[str, Any]:
    try:
        if path == "-":
            value = json.load(sys.stdin)
        else:
            with open(path, "r", encoding="utf-8") as handle:
                value = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        raise SeqirImportError(f"cannot read Yosys JSON {path!r}: {exc}") from exc
    return _as_mapping(value, "Yosys document")


def _render_json(payload: Mapping[str, Any]) -> str:
    return json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def _write_text(path: str, text: str) -> None:
    if path == "-":
        sys.stdout.write(text)
        return
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=f".{output.name}.", suffix=".tmp", dir=str(output.parent), text=True
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        os.replace(temporary, output)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def _write_requested_outputs(outputs: Sequence[Tuple[str, str, str]]) -> None:
    """Validate output destinations before committing any rendered artifact."""
    stdout_labels = [label for label, path, _ in outputs if path == "-"]
    if len(stdout_labels) > 1:
        raise SeqirImportError(
            "multiple artifacts target stdout: " + ", ".join(stdout_labels)
        )
    seen: Dict[str, str] = {}
    for label, path, _ in outputs:
        if path == "-":
            continue
        normalized = os.path.abspath(path)
        previous = seen.get(normalized)
        if previous is not None:
            raise SeqirImportError(
                f"{previous} and {label} target the same output path {path!r}"
            )
        seen[normalized] = label
    for _, path, rendered in outputs:
        _write_text(path, rendered)


def _argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert flattened Yosys write_json output to stable ifcn.seqir.v0 JSON."
    )
    parser.add_argument("input", help="Yosys JSON path, or - for stdin")
    parser.add_argument("-o", "--output", default="-", help="SeqIR JSON path, or - for stdout")
    parser.add_argument("--top", help="top module when the Yosys JSON has no unique top attribute")
    parser.add_argument(
        "--legacy-cut-verilog",
        metavar="PATH",
        help="also write a strict AOI cut DAG accepted by the legacy Parse front-end",
    )
    parser.add_argument(
        "--state-manifest",
        metavar="PATH",
        help="also write D-event/Q-event arguments for ifcn_sequential_pnr --state",
    )
    parser.add_argument(
        "--compact-legacy-names",
        action="store_true",
        help="use short i/q/d/n/t identifiers in legacy cut and TeX-facing artifacts",
    )
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = _argument_parser().parse_args(argv)
    try:
        payload = import_yosys_json(_read_json(args.input), args.top)
        outputs: List[Tuple[str, str, str]] = [
            ("SeqIR", args.output, _render_json(payload))
        ]
        if args.legacy_cut_verilog or args.state_manifest:
            legacy = build_legacy_cut_artifacts(
                payload, compact_names=args.compact_legacy_names
            )
            if args.legacy_cut_verilog:
                outputs.append(("legacy cut Verilog", args.legacy_cut_verilog, legacy.verilog))
            if args.state_manifest:
                outputs.append((
                    "state manifest",
                    args.state_manifest,
                    _render_json(legacy.state_manifest),
                ))
        _write_requested_outputs(outputs)
    except SeqirImportError as exc:
        print(f"yosys_json_to_seqir: error: {exc}", file=sys.stderr)
        return 2
    except OSError as exc:
        print(f"yosys_json_to_seqir: error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
