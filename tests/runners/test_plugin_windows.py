"""Windows host differential test using the existing reliable PowerShell fixture runner."""
from pathlib import Path
import json
import subprocess
ROOT=Path(__file__).resolve().parents[2]
script=ROOT/"scripts"/"build"/"test_plugin_windows.ps1"
subprocess.run(["powershell.exe","-NoProfile","-ExecutionPolicy","Bypass","-File",str(script)],
               check=True,timeout=45)
result=json.loads((ROOT/"build"/"plugin-preview"/"plugin-windows-test.json").read_text(encoding="utf-8"))
assert result["passed"] and result["exit_code"]==0 and "WINUX_CUSTOM_DLL_OK:" in result["stdout"]
print("WINDOWS_PLUGIN_CONTRACT_OK")
