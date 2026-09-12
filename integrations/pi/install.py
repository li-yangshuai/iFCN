#!/usr/bin/env python3
"""Install the iFCN agent and extension into one or more Pi project directories."""
import argparse
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def install(project):
    project = project.resolve()
    if not project.is_dir():
        raise ValueError(f"Project directory does not exist: {project}")
    files = {
        project / ".pi/agents/ifcn.md": (HERE / "agent.md").read_text(),
        project / ".pi/extensions/ifcn/index.ts": (HERE / "extension.ts").read_text(),
        project / ".pi/extensions/ifcn/ifcn-config.json": json.dumps({"root": str(ROOT)}, indent=2) + "\n",
    }
    receipt_path = project / ".pi/ifcn-install.json"
    prior = json.loads(receipt_path.read_text()) if receipt_path.exists() else {}
    import hashlib
    for target, content in files.items():
        if target.exists() and target.read_text() != content:
            expected = prior.get(str(target.relative_to(project)))
            actual = hashlib.sha256(target.read_bytes()).hexdigest()
            if expected != actual:
                raise ValueError(f"Preserving user-modified file: {target}")
    receipt = {}
    for target, content in files.items():
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content)
        receipt[str(target.relative_to(project))] = hashlib.sha256(target.read_bytes()).hexdigest()
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
    return {"project": str(project), "files": [str(p) for p in files]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("projects", nargs="+", type=Path)
    args = parser.parse_args()
    print(json.dumps([install(project) for project in args.projects], indent=2))
