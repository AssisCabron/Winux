param([string]$Python='python', [string]$Zig="$PSScriptRoot/tools/zig-x86_64-windows-0.14.1/zig.exe", [switch]$IncludeWindowsCmd, [switch]$IncludeWindowsDependencies)
$ErrorActionPreference='Stop'
# An existing, version-pinned NT dispatcher consumes the generated service IDs.
# Fail before writing build outputs if the contract or generated file drifts.
& $Python "$PSScriptRoot/scripts/build/generate_nt_abi.py" --check
if($LASTEXITCODE -ne 0){throw 'NT syscall ABI contract drift'}
New-Item -ItemType Directory -Force "$PSScriptRoot/build" | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR="$PSScriptRoot/tools/zig-cache"
& $Zig build-lib -dynamic -target x86_64-windows -O ReleaseSmall -fno-stack-check -fno-stack-protector -fentry=DllMain -fstrip -lkernel32 "-femit-bin=$PSScriptRoot/build/winuxrt.dll" "-femit-implib=$PSScriptRoot/build/winuxrt.lib" -cflags -fno-emulated-tls -Werror=unknown-attributes -- "$PSScriptRoot/apps/winuxrt.c"
if($LASTEXITCODE -ne 0){throw 'Original DLL compilation failed'}
& $Python "$PSScriptRoot/scripts/build/finalize_dlls.py" winuxrt
if($LASTEXITCODE -ne 0){throw 'Original DLL export packaging failed'}
& $Zig dlltool -m i386:x86-64 -d "$PSScriptRoot/apps/winuxrt.def" -l "$PSScriptRoot/build/winuxrt.lib" -D winuxrt.dll
if($LASTEXITCODE -ne 0){throw 'Original DLL import library failed'}
& $Zig build-lib -dynamic -target x86_64-windows -O ReleaseSmall -fno-stack-check -fno-stack-protector -fentry=DllMain -fstrip "-femit-bin=$PSScriptRoot/build/winuxbridge.dll" "$PSScriptRoot/apps/winuxbridge.c" "$PSScriptRoot/build/winuxrt.lib"
if($LASTEXITCODE -ne 0){throw 'Dependent DLL compilation failed'}
& $Python "$PSScriptRoot/scripts/build/finalize_dlls.py" winuxbridge
if($LASTEXITCODE -ne 0){throw 'Dependent DLL export packaging failed'}
& $Zig dlltool -m i386:x86-64 -d "$PSScriptRoot/apps/winuxbridge.def" -l "$PSScriptRoot/build/winuxbridge.lib" -D winuxbridge.dll
if($LASTEXITCODE -ne 0){throw 'Dependent DLL import library failed'}
& $Zig build-exe -target x86_64-windows -O ReleaseSmall -fno-stack-check -fno-stack-protector -fentry=mainCRTStartup -fstrip -lkernel32 "-femit-bin=$PSScriptRoot/build/dlltest.exe" "$PSScriptRoot/apps/dlltest_win32.c" "$PSScriptRoot/build/winuxrt.lib" "$PSScriptRoot/build/winuxbridge.lib"
if($LASTEXITCODE -ne 0){throw 'DLL graph test compilation failed'}
& $Zig build-exe -target x86_64-windows -O ReleaseSmall -fno-stack-check -fno-stack-protector -fentry=mainCRTStartup -fstrip -DTEST_EXIT_PROCESS -lkernel32 "-femit-bin=$PSScriptRoot/build/dllexit.exe" "$PSScriptRoot/apps/dlltest_win32.c" "$PSScriptRoot/build/winuxrt.lib" "$PSScriptRoot/build/winuxbridge.lib"
if($LASTEXITCODE -ne 0){throw 'DLL exit test compilation failed'}
& $Zig build-exe -target x86_64-windows -O ReleaseSmall -fno-stack-check -fno-stack-protector -fentry=mainCRTStartup -fstrip -lkernel32 "-femit-bin=$PSScriptRoot/build/hello.exe" "$PSScriptRoot/apps/hello_win32.c"
if($LASTEXITCODE -ne 0){throw 'Win32 demo compilation failed'}
& $Zig build-exe -target x86_64-windows -O ReleaseSmall -fno-stack-check -fno-stack-protector -fentry=mainCRTStartup -fstrip -lkernel32 "-femit-bin=$PSScriptRoot/build/runtime.exe" "$PSScriptRoot/apps/runtime_win32.c"
if($LASTEXITCODE -ne 0){throw 'Win32 runtime contract compilation failed'}
& $Zig build-exe -target x86_64-windows -O ReleaseSmall -fno-stack-check -fno-stack-protector -fentry=mainCRTStartup -fstrip -lkernel32 "-femit-bin=$PSScriptRoot/build/tlstest.exe" -cflags -fno-emulated-tls -Werror=unknown-attributes -- "$PSScriptRoot/apps/tls_win32.c"
if($LASTEXITCODE -ne 0){throw 'Static TLS test compilation failed'}
& $Zig build-exe -target x86_64-windows -O ReleaseSmall -fno-stack-check -fno-stack-protector -fentry=mainCRTStartup -fstrip -lkernel32 -lntdll "-femit-bin=$PSScriptRoot/build/ntmemory.exe" "$PSScriptRoot/apps/ntmemory_win32.c"
if($LASTEXITCODE -ne 0){throw 'NT memory contract compilation failed'}
if($IncludeWindowsDependencies){ & $Python "$PSScriptRoot/scripts/build/prepare_assets.py" --cmd "$env:SystemRoot/System32/cmd.exe" --windows-dependencies }
elseif($IncludeWindowsCmd){ & $Python "$PSScriptRoot/scripts/build/prepare_assets.py" --cmd "$env:SystemRoot/System32/cmd.exe" }
else { & $Python "$PSScriptRoot/scripts/build/prepare_assets.py" }
if($LASTEXITCODE -ne 0){throw 'Asset packaging failed'}
& $Zig build-exe -target x86_64-uefi -O ReleaseSmall -fno-stack-check -fno-stack-protector -mno-red-zone "-femit-bin=$PSScriptRoot/build/BOOTX64.EFI" -fstrip "$PSScriptRoot/src/kernel/boot.c"
if($LASTEXITCODE -ne 0){throw 'Kernel compilation failed'}
& $Python "$PSScriptRoot/scripts/build/make_iso.py"
if($LASTEXITCODE -ne 0){throw 'ISO creation failed'}

