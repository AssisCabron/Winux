param(
    [switch]$RunHostTests,
    [switch]$RunQemu,
    [switch]$HistoricalIdentity,
    [string]$Python = "$env:ProgramFiles\Python310\python.exe"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$zig = Join-Path $root 'tools\zig-x86_64-windows-0.14.1\zig.exe'
if (-not (Test-Path -LiteralPath $zig)) { throw "Zig toolchain not found" }
if (-not (Test-Path -LiteralPath $Python)) { throw "Python interpreter not found: $Python" }
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root 'tools\zig-cache'

# The current kernel uses one version-bound NT service contract.
& $Python (Join-Path $root 'scripts\build\generate_nt_abi.py') --check
if ($LASTEXITCODE -ne 0) { throw "Active NT dispatcher/profile drift" }

# Gold images must not be overwritten when validating source changes.
$originals = @(
    @{ Name='winux-0.38-x64.iso'; SHA='700E6A38C0B7DBE312B6012810728AF8ECD24A984CD8287E0F644A73F1C343B0' },
    @{ Name='winux-0.38-wrapper-fls-candidate.iso'; SHA='4D1E20074E35FB9B03BBCB3043180CAC39C08CD8C5536EFC784E48A7E9270C77' }
)
foreach ($item in $originals) {
    $file = Join-Path $root "build\$($item.Name)"
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $item.SHA) {
        throw "Original ISO changed: $($item.Name)"
    }
}

$verification = Join-Path $root 'build\layout-verification'
New-Item -ItemType Directory -Force $verification | Out-Null
$items = @(
    @{ Name='stable'; Source='src\kernel\boot.c'; Previous='build\BOOTX64.EFI' },
    @{ Name='p1'; Source='experiments\candidates\p1\boot.c'; Previous='build\BOOTX64-heap-policy.EFI' },
    @{ Name='p2'; Source='experiments\candidates\p2\boot.c'; Previous='build\BOOTX64-heap-policy-alloc.EFI' },
    @{ Name='p3'; Source='experiments\candidates\p3\boot.c'; Previous='build\BOOTX64-wrapper-realloc.EFI' },
    @{ Name='p4'; Source='experiments\candidates\p4\boot.c'; Previous='build\BOOTX64-wrapper-fls.EFI' }
)
foreach ($item in $items) {
    $source = Join-Path $root $item.Source
    $previous = Join-Path $root $item.Previous
    $output = Join-Path $verification ("BOOTX64-{0}.EFI" -f $item.Name)
    & $zig build-exe -target x86_64-uefi -O ReleaseSmall -fno-stack-check -fno-stack-protector -mno-red-zone "-femit-bin=$output" -fstrip $source
    if ($LASTEXITCODE -ne 0) { throw "Kernel compile failed: $($item.Name)" }
    $current = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
    $historical = (Get-FileHash -LiteralPath $previous -Algorithm SHA256).Hash
    if ($HistoricalIdentity -and $current -ne $historical) {
        throw "Byte identity changed since 0.38 baseline: $($item.Name)"
    }
    Write-Output "COMPILE_OK: $($item.Name) sha256=$current (legacy_identical=$($current -eq $historical))"
}
if ($RunHostTests) {
    $runners = @('test_nt_abi_contract.py','test_console_endpoint_host.py','test_nt_file_host.py',
                 'test_nt_file_api_host.py','test_process_parameters_host.py','test_nt_objects_host.py')
    foreach ($script in $runners) {
        & $Python (Join-Path $root "tests\runners\$script") | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Host test failed: $script" }
        Write-Output "HOST_OK: $script"
    }
}
if ($RunQemu) {
    # The rebuilt variant runs in a newly packaged preview ISO, not historical P4.
    Copy-Item -LiteralPath (Join-Path $verification 'BOOTX64-p4.EFI') -Destination (Join-Path $root 'build\BOOTX64-ntabi-p4.EFI') -Force
    & $Python (Join-Path $root 'scripts\build\build_ntabi_preview.py') | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Preview ISO packaging failed" }
    & $Python (Join-Path $root 'tests\runners\test_console_wrapper_fls_candidate.py') --iso 'build/abi-preview/winux-0.38-wrapper-fls-candidate.iso' --result-tag 'console-ntabi-p4-telemetry' --check-nt-missing | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "QEMU P4 + NT ABI regression failed" }
    & $Python (Join-Path $root 'scripts\compatibility\analyze_nt_missing.py') | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "REA NT missing-call cross-reference failed" }
    Write-Output 'QEMU_OK: P4 with generated NT ABI and missing-service ledger'
}
Write-Output 'WINUX_ABI_BUILD_VERIFIED: existing dispatcher, sources and original ISOs'
