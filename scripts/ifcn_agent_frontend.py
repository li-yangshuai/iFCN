"""Strict scalar combinational Verilog front end for the Pi adapter.

Lower supported Boolean expressions to the existing iFCN parser's AOI syntax.
Reject unsupported RTL before the permissive native parser can drop statements.
"""
import ast
import graphlib
import re


IDENT = r"[A-Za-z_][A-Za-z0-9_]*"


def normalize_verilog(source):
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    if re.search(r"\b(always\w*|initial|reg|posedge|negedge|latch)\b", text):
        raise ValueError("Sequential/behavioral RTL is reserved for the sequential adapter; provide a scalar combinational assign netlist.")
    match = re.fullmatch(rf"\s*module\s+({IDENT})\s*\((.*?)\)\s*;(.*?)\bendmodule\s*", text, re.S)
    if not match:
        raise ValueError("Expected exactly one Verilog module with a port list.")
    module, header, body = match.groups()
    declarations = {"input": [], "output": [], "wire": []}
    statements = body.split(";")
    if statements[-1].strip():
        raise ValueError("Missing statement semicolon.")
    # Support scalar ANSI ports as well as the original benchmark declaration style.
    if re.search(r"\b(input|output)\b", header):
        direction = None
        for field in header.split(","):
            port = re.fullmatch(rf"\s*(?:(input|output)\s+(?:wire\s+)?)?({IDENT})\s*", field)
            if not port or not (port[1] or direction):
                raise ValueError("Only scalar input/output ports are supported; synthesize buses/hierarchy first.")
            direction = port[1] or direction
            declarations[direction].append(port[2])
    assignments = {}
    for statement in statements[:-1]:
        statement = statement.strip()
        decl = re.fullmatch(r"(input|output|wire)\s+(.*)", statement, re.S)
        assign = re.fullmatch(rf"assign\s+({IDENT})\s*=\s*(.+)", statement, re.S)
        if decl:
            names = [part.strip() for part in decl[2].split(",")]
            if any(not re.fullmatch(IDENT, name) for name in names):
                raise ValueError("Only scalar declarations are supported; synthesize buses first.")
            declarations[decl[1]].extend(names)
        elif assign:
            if assign[1] in assignments:
                raise ValueError(f"Multiple drivers for {assign[1]}.")
            # Python and Verilog have the same precedence for this limited grammar.
            expr = assign[2]
            if not re.fullmatch(r"[A-Za-z0-9_\s~&|^()]+", expr):
                raise ValueError("Supported expressions use scalar names and ~, &, |, ^, parentheses.")
            tree = ast.parse(expr.strip(), mode="eval").body
            assignments[assign[1]] = tree
        else:
            raise ValueError(f"Unsupported Verilog statement: {statement[:100]}")
    names = sum(declarations.values(), [])
    if len(names) != len(set(names)):
        raise ValueError("Duplicate signal declaration.")
    if not declarations["input"] or not declarations["output"]:
        raise ValueError("At least one input and one output are required.")
    declared = set(names)
    inputs = set(declarations["input"])
    dependencies = {}
    for target, tree in assignments.items():
        if target not in declared or target in inputs:
            raise ValueError(f"Invalid assignment target: {target}")
        refs = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Name):
                refs.add(node.id)
            elif not isinstance(node, (ast.BinOp, ast.UnaryOp, ast.BitAnd, ast.BitOr, ast.BitXor, ast.Invert, ast.Load)):
                raise ValueError("Only scalar Boolean expressions are supported (constants require synthesis).")
        if not refs <= declared:
            raise ValueError(f"Undeclared signals: {sorted(refs - declared)}")
        if not refs <= inputs | assignments.keys():
            raise ValueError(f"Undriven signals: {sorted(refs - inputs - assignments.keys())}")
        dependencies[target] = refs - inputs
    if not set(declarations["output"]) <= assignments.keys():
        raise ValueError("Every output must have an assign driver.")
    try:
        order = list(graphlib.TopologicalSorter(dependencies).static_order())
    except graphlib.CycleError as exc:
        raise ValueError("Combinational cycle detected; feedback requires the sequential adapter.") from exc
    emitted = []
    temporaries = []

    def gate(expr, target=None):
        if target is not None:
            emitted.append((target, expr))
            return target
        candidate = f"ifcn_tmp_{len(temporaries)}"
        while candidate in declared:
            candidate += "_"
        declared.add(candidate)
        temporaries.append(candidate)
        emitted.append((candidate, expr))
        return candidate

    def lower(node, target=None):
        if isinstance(node, ast.Name):
            return gate(node.id, target) if target is not None else node.id
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Invert):
            return gate("~" + lower(node.operand), target)
        left, right = lower(node.left), lower(node.right)
        if isinstance(node.op, ast.BitXor):
            either = gate(f"{left} | {right}")
            both = gate(f"{left} & {right}")
            inverted = gate("~" + both)
            return gate(f"{either} & {inverted}", target)
        return gate(f"{left} {'&' if isinstance(node.op, ast.BitAnd) else '|'} {right}", target)

    for target in order:
        lower(assignments[target], target)
    wires = declarations["wire"] + temporaries
    if not wires:
        dummy = "ifcn_unused"
        while dummy in declared:
            dummy += "_"
        wires = [dummy]
    ports = declarations["input"] + declarations["output"]
    normalized = "\n".join([
        f"module {module}({', '.join(ports)});",
        f"input {', '.join(declarations['input'])};",
        f"output {', '.join(declarations['output'])};",
        f"wire {', '.join(wires)};",
        *[f"assign {target} = {expr};" for target, expr in emitted],
        "endmodule", "",
    ])
    warnings = []
    if not re.search(r"\b(input|output)\b", header):
        header_ports = [item.strip() for item in header.split(",")]
        if any(not re.fullmatch(IDENT, item) for item in header_ports):
            raise ValueError("Unsupported module port list.")
        if set(header_ports) != set(ports):
            warnings.append("Legacy module header differs from input/output declarations; normalized ports follow the declarations. Original source preserved.")
    return normalized, {"module": module, "inputs": declarations["input"], "warnings": warnings,
                        "outputs": declarations["output"], "assignment_count": len(assignments),
                        "frontend": "scalar-boolean-v1", "functional_signoff": "not_performed"}
