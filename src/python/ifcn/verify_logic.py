#!/usr/bin/env python3
"""Exhaustive source-Verilog versus exported combinational-DAG logic audit.

This independent, read-only audit compares one source file with one exported
DAG. It neither routes circuits nor changes their success flags. Inputs are matched by exact
name; outputs require exact names or an explicit pure-signal alias chain in the
source. No output permutation or truth-table matching is used. Missing metadata,
unsupported gates/constants, or ambiguous aliases return unsupported, never pass.
The result says nothing about device waveforms, clock latency, or physical power.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import graphlib
import hashlib
import itertools
import json
from pathlib import Path
import re

import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ifcn.logic import analyze, expression_tree

SCOPE='Exhaustive Boolean equivalence of source assignments and exported routed DAG; excludes device behavior, clock timing/alignment, and power.'
SEMANTICS={
    'input':'primary input; no fanins',
    'and':'AND of exactly two fanins', 'or':'OR of exactly two fanins',
    'not':'complement of exactly one fanin',
    'wire':'identity of exactly one fanin', 'buf':'identity of exactly one fanin',
    'buffer':'identity of exactly one fanin', 'output':'identity of exactly one fanin',
    'fanout':'identity of exactly one fanin', 'redundancy':'identity of exactly one fanin',
    'maj':'majority of exactly three fanins', 'majority':'majority of exactly three fanins',
}

class Unsupported(ValueError):
    pass


def sha(data):
    return hashlib.sha256(data).hexdigest()


def strip_comments(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)


def source_analysis(data, max_inputs):
    text=strip_comments(data.decode('utf-8-sig'))
    match=re.fullmatch(r'\s*module\s+([A-Za-z_]\w*)\s*\(([^)]*)\)\s*;(.*?)\bendmodule\s*',text,re.S)
    if not match:
        raise Unsupported('Expected exactly one old-style scalar assign module')
    body=match[3]
    source_style='old_style_scalar'
    if re.search(r'\b(input|output)\b',match[2]):
        declarations={'input':[],'output':[]};direction=None;ports=[]
        for field in match[2].split(','):
            port=re.fullmatch(r'\s*(?:(input|output)\s+(?:wire\s+)?)?([A-Za-z_]\w*)\s*',field)
            if not port or not (port[1] or direction):
                raise Unsupported('Unsupported ANSI port declaration; only scalar input/output names are accepted')
            direction=port[1] or direction;declarations[direction].append(port[2]);ports.append(port[2])
        if not declarations['input'] or not declarations['output'] or len(ports)!=len(set(ports)):
            raise Unsupported('ANSI ports must include unique scalar inputs and outputs')
        body='input '+','.join(declarations['input'])+';\noutput '+','.join(declarations['output'])+';\n'+body
        text='module '+match[1]+'('+','.join(ports)+');\n'+body+'\nendmodule\n'
        data=text.encode()
        source_style='scalar_ansi'
    known=re.sub(r'\b(?:input|output|wire)\s+[A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*\s*;','',body)
    known=re.sub(r'\bassign\s+[A-Za-z_]\w*\s*=\s*[^;]+;','',known)
    if known.strip():
        raise Unsupported('Source contains unsupported declarations or non-assign statements')
    names=[]
    for decl in re.findall(r'\binput\s+([^;]+);',body):
        names.extend(name.strip() for name in decl.split(','))
    if not 0<len(names)<=max_inputs:
        raise Unsupported(f'Source input count {len(names)} is outside exhaustive limit 1..{max_inputs}')
    result=analyze(data)
    result['source_style']=source_style
    # Declaration names define the interface; retain header mismatches as
    # diagnostics rather than silently reordering legacy circuit ports.
    assignments={name:expression_tree(expr) for name,expr in re.findall(r'\bassign\s+(\w+)\s*=\s*([^;]+);',body)}
    result['pure_aliases']={name:tree[1] for name,tree in assignments.items()
                            if tree[0]=='value' and re.fullmatch(r'[A-Za-z_]\w*',tree[1])}
    return result


def compile_graph(dag, source):
    if dag.get('schema')!='ifcn.dag.v1':
        raise Unsupported('Unrecognized DAG schema')
    nodes={}
    for node in dag.get('nodes',[]):
        if type(node.get('id')) is not int or node['id'] in nodes:
            raise Unsupported('Missing or duplicate integer node identity')
        if not isinstance(node.get('name'),str) or not isinstance(node.get('type'),str):
            raise Unsupported('Node names and explicit types are required')
        if type(node.get('is_input')) is not bool or type(node.get('is_output')) is not bool:
            raise Unsupported('Explicit primary-input/output flags are required')
        nodes[node['id']]=node
    if not nodes:
        raise Unsupported('Empty DAG')
    incoming={node:[] for node in nodes}; edges=set()
    for edge in dag.get('edges',[]):
        if not isinstance(edge,list) or len(edge)!=2 or any(type(n) is not int for n in edge):
            raise Unsupported('Only explicit non-inverting source/sink edge pairs are supported')
        key=tuple(edge)
        if key in edges or any(n not in nodes for n in edge):
            raise Unsupported('Duplicate edge or unknown edge endpoint')
        edges.add(key);incoming[edge[1]].append(edge[0])
    try:
        order=list(graphlib.TopologicalSorter({n:set(v) for n,v in incoming.items()}).static_order())
    except graphlib.CycleError as exc:
        raise Unsupported('Exported combinational DAG contains a cycle') from exc
    for node in nodes.values():
        kind=node['type'].lower();degree=len(incoming[node['id']])
        if kind not in SEMANTICS:
            raise Unsupported(f'No explicit audited semantics for node type {kind!r}; constants are not inferred from names')
        arity=0 if kind=='input' else (2 if kind in ('and','or') else (3 if kind in ('maj','majority') else 1))
        if degree!=arity:
            raise Unsupported(f'Node {node["id"]} ({kind}) has {degree} fanins; supported arity is {arity}')
        if (kind=='input')!=node['is_input']:
            raise Unsupported(f'Node {node["id"]} input type/flag disagree')
    input_nodes={n['name']:n['id'] for n in nodes.values() if n['is_input']}
    output_nodes={n['name']:n['id'] for n in nodes.values() if n['is_output']}
    if len(input_nodes)!=sum(n['is_input'] for n in nodes.values()) or len(output_nodes)!=sum(n['is_output'] for n in nodes.values()):
        raise Unsupported('Ambiguous repeated primary-port names')
    if set(input_nodes)!=set(source['input_ports']):
        raise Unsupported(f'Input name mismatch: source={source["input_ports"]}, DAG={sorted(input_nodes)}')
    output_mapping=[]
    for port in source['output_ports']:
        chain=[port]
        while chain[-1] not in output_nodes and chain[-1] in source['pure_aliases']:
            nxt=source['pure_aliases'][chain[-1]]
            if nxt in chain:
                raise Unsupported('Cyclic source output alias chain')
            chain.append(nxt)
        if chain[-1] not in output_nodes:
            raise Unsupported(f'No explicit exported output node for source port {port!r}; attempted source alias chain {chain}')
        output_mapping.append({'source_port':port,'dag_node_id':output_nodes[chain[-1]],'dag_node_name':chain[-1],
                               'basis':'exact_name' if len(chain)==1 else 'explicit_source_signal_alias', 'alias_chain':chain})
    mapped={m['dag_node_id'] for m in output_mapping}
    if len(mapped)!=len(output_mapping) or mapped!=set(output_nodes.values()):
        raise Unsupported('Output mapping is not a bijection; ports cannot be inferred or permuted')
    if dag.get('input_count')!=len(input_nodes) or dag.get('output_count')!=len(output_nodes):
        raise Unsupported('DAG primary-port counts disagree with flagged nodes')
    return nodes,incoming,order,input_nodes,output_mapping


def evaluate_graph(nodes,incoming,order,input_nodes,bits,source_inputs):
    values={input_nodes[name]:bit for name,bit in zip(source_inputs,bits)}
    for node_id in order:
        node=nodes[node_id];kind=node['type'].lower()
        if kind=='input':
            continue
        operands=[values[parent] for parent in incoming[node_id]]
        if kind=='and': value=operands[0]&operands[1]
        elif kind=='or': value=operands[0]|operands[1]
        elif kind=='not': value=1^operands[0]
        elif kind in ('maj','majority'): value=int(sum(operands)>=2)
        else: value=operands[0]
        values[node_id]=value
    return values


def verify_bytes(source_bytes, dag_bytes, *, max_inputs=11):
    result={'schema':'ifcn.routed_logic_audit.v1','scope':SCOPE,'source_sha256':sha(source_bytes),
            'routed_dag_sha256':sha(dag_bytes),'status':'unsupported','equivalent':None,
            'max_inputs':max_inputs,'vectors_checked':0,'mismatch_count':0,'counterexamples':[]}
    try:
        source=source_analysis(source_bytes,max_inputs)
        dag=json.loads(dag_bytes)
        result['port_diagnostics']={
            'source_input_ports':source['input_ports'], 'source_output_ports':source['output_ports'],
            'dag_declared_input_count':dag.get('input_count'), 'dag_declared_output_count':dag.get('output_count'),
            'dag_flagged_inputs':[{'id':n.get('id'),'name':n.get('name'),'type':n.get('type')} for n in dag.get('nodes',[]) if n.get('is_input') is True],
            'dag_flagged_outputs':[{'id':n.get('id'),'name':n.get('name'),'type':n.get('type')} for n in dag.get('nodes',[]) if n.get('is_output') is True],
        }
        nodes,incoming,order,input_nodes,output_mapping=compile_graph(dag,source)
        result.update(input_ports=source['input_ports'],output_ports=source['output_ports'],
                      source_header_matches_declarations=source['header_matches_declarations'],
                      input_mapping=[{'source_port':p,'dag_node_id':input_nodes[p],'basis':'exact_name'} for p in source['input_ports']],
                      output_mapping=output_mapping,gate_semantics={k:SEMANTICS[k] for k in sorted({n['type'].lower() for n in nodes.values()})})
        rows=[]
        for index,bits in enumerate(itertools.product((0,1),repeat=len(source['input_ports']))):
            values=evaluate_graph(nodes,incoming,order,input_nodes,bits,source['input_ports'])
            observed=[values[m['dag_node_id']] for m in output_mapping]
            expected=source['truth_table'][index];rows.append(observed)
            result['vectors_checked']+=1
            if observed!=expected:
                result['mismatch_count']+=1
                if len(result['counterexamples'])<8:
                    result['counterexamples'].append({'inputs':dict(zip(source['input_ports'],bits)),
                                                     'source_outputs':dict(zip(source['output_ports'],expected)),
                                                     'dag_outputs':dict(zip(source['output_ports'],observed))})
        result.update(status='equivalent' if not result['mismatch_count'] else 'not_equivalent',equivalent=not result['mismatch_count'],
                      expected_vector_count=1<<len(source['input_ports']),
                      source_truth_sha256=sha(json.dumps(source['truth_table'],separators=(',',':')).encode()),
                      dag_truth_sha256=sha(json.dumps(rows,separators=(',',':')).encode()))
    except (ValueError,KeyError,TypeError,UnicodeError) as exc:
        result['reason']=str(exc)
        result['reason_code']='input_limit_exceeded' if 'outside exhaustive limit' in str(exc) else 'unsupported_logic_or_port_mapping'
    return result


def audit_case(source: Path, dag: Path, max_inputs: int = 11) -> dict:
    """Production API; only status == 'equivalent' grants bounded logic approval.

    source is the snapshotted ORIGINAL Verilog, dag is the selected candidate's
    actual routed_dag.json. For unsupported/oversize cases equivalent remains
    None. I/O errors propagate so a missing artifact cannot become approval.
    """
    if not 1 <= max_inputs <= 16:
        raise ValueError('max_inputs must be between 1 and 16')
    source,dag=Path(source),Path(dag)
    result=verify_bytes(source.read_bytes(),dag.read_bytes(),max_inputs=max_inputs)
    result.update(source_path=str(source.resolve()),routed_dag_path=str(dag.resolve()))
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Scalar source Verilog")
    parser.add_argument("--dag", type=Path, required=True, help="Exported ifcn.dag.v1 JSON")
    parser.add_argument("--output", type=Path, required=True, help="New JSON audit report")
    parser.add_argument("--max-inputs", type=int, default=11)
    args = parser.parse_args(argv)
    if not 1 <= args.max_inputs <= 16:
        parser.error("--max-inputs must be in 1..16")
    if args.output.exists():
        parser.error("Output exists; use a new report path to preserve earlier evidence")
    report = audit_case(args.source, args.dag, max_inputs=args.max_inputs)
    report.update(
        created_at=datetime.now(timezone.utc).isoformat(),
        auditor_sha256=sha(Path(__file__).read_bytes()),
        source_oracle_sha256=sha(Path(__file__).with_name("logic.py").read_bytes()),
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in
                     ("status", "equivalent", "vectors_checked", "mismatch_count", "scope")},
                    ensure_ascii=False))
    return {"equivalent": 0, "not_equivalent": 1}.get(report["status"], 2)


if __name__ == "__main__":
    raise SystemExit(main())
