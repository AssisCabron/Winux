param([string]$Python = "$env:ProgramFiles\Python310\python.exe")
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$zig = Join-Path $root 'tools\zig-x86_64-windows-0.14.1\zig.exe'
$preview = Join-Path $root 'build\plugin-preview'
New-Item -Force -ItemType Directory $preview | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root 'tools\zig-cache'
& $Python (Join-Path $root 'scripts\build\generate_nt_abi.py') --check
if ($LASTEXITCODE -ne 0) { throw "Existing NT ABI contract is invalid" }
& $zig build-lib -dynamic -target x86_64-windows -O ReleaseSmall -fno-stack-check -fno-stack-protector -fentry=DllMain -fstrip -lkernel32 "-femit-bin=$preview\winuxplugin.dll" (Join-Path $root 'apps\winuxplugin.c')
if ($LASTEXITCODE -ne 0) { throw "Windows plugin DLL build failed" }
& $zig dlltool -m i386:x86-64 -d (Join-Path $root 'apps\winuxplugin.def') -l (Join-Path $preview 'winuxplugin.lib') -D winuxplugin.dll
if ($LASTEXITCODE -ne 0) { throw "Windows plugin import library build failed" }
& $zig build-exe -target x86_64-windows -O ReleaseSmall -fno-stack-check -fno-stack-protector -fentry=mainCRTStartup -fstrip -lkernel32 "-femit-bin=$preview\plugintest.exe" (Join-Path $root 'apps\plugin_fixture.c') (Join-Path $preview 'winuxplugin.lib')
if ($LASTEXITCODE -ne 0) { throw "Windows plugin host EXE build failed" }
Write-Output "PLUGIN_BUILD_OK"
Get-FileHash (Join-Path $preview 'winuxplugin.dll') -Algorithm SHA256 | Format-List
Get-FileHash (Join-Path $preview 'plugintest.exe') -Algorithm SHA256 | Format-List
