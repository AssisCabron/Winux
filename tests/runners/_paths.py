"""Paths shared by the Winux test harness after moving the Git repository.

The REA evidence is intentionally kept in the sibling windows-analysis tree;
no duplicated manifests, binaries or copies are required inside Winux.
"""
from pathlib import Path

def analysis_cmd_dir(project_root: Path) -> Path:
    root=project_root.resolve()
    for ancestor in (root,*root.parents):
        candidate=ancestor/"windows-analysis"/"cmd"
        if (candidate/"dependency-startup.json").is_file():
            return candidate
    raise FileNotFoundError(
        "REA reference missing: expected windows-analysis/cmd/dependency-startup.json "
        "in the workspace ancestors of " + str(root)
    )
