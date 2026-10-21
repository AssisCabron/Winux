param([string]$Python = "$env:ProgramFiles\Python310\python.exe")
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$preview = Join-Path $root 'build\plugin-preview'
& (Join-Path $PSScriptRoot 'build_plugin_preview.ps1') -Python $Python | Out-Null
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw 'Custom DLL compilation failed' }
& $Python (Join-Path $root 'tests\runners\test_plugin_windows.py') | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Windows host DLL execution failed' }
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root 'tools\zig-cache'
& $Python (Join-Path $PSScriptRoot 'make_plugin_preview.py') | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Isolated plugin ISO packaging failed' }
& $Python (Join-Path $root 'tests\runners\test_console_wrapper_fls_candidate.py') --iso 'build/plugin-preview/winux-0.38-wrapper-fls-candidate.iso' --result-tag 'console-plugin-p4' --check-nt-missing --check-plugin | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Winux/QEMU DLL execution failed' }
$report = Get-Content -LiteralPath (Join-Path $root 'build\console-plugin-p4-test.json') -Raw | ConvertFrom-Json
if (-not $report.passed -or $report.checks.Count -lt 39) { throw 'Incomplete Winux plugin regression report' }
$windows = Get-Content -LiteralPath (Join-Path $preview 'plugin-windows-test.json') -Raw | ConvertFrom-Json
$guest = Get-Content -LiteralPath (Join-Path $preview 'plugin-build.json') -Raw | ConvertFrom-Json
if ($windows.plugin_dll_sha256 -ne $guest.dll_sha256 -or $windows.fixture_exe_sha256 -ne $guest.exe_sha256) {
    throw 'Windows/Winux plugin binary identities differ'
}
Write-Output "WINUX_PLUGIN_COMPAT_OK"
Write-Output "Windows DLL SHA256: $($guest.dll_sha256)"
Write-Output "Guest ISO SHA256: $($guest.iso_sha256)"
Write-Output "QEMU checklist: $($report.checks.Count) passed"
