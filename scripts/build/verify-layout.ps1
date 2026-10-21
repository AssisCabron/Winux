param(
    [switch]$RunHostTests,
    [switch]$RunQemu,
    [string]$Python = "$env:ProgramFiles\Python310\python.exe"
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$zig = Join-Path $projectRoot 'tools\zig-x86_64-windows-0.14.1\zig.exe'
if (-not (Test-Path -LiteralPath $zig)) { throw 'Zig toolchain not found' }
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $projectRoot 'tools\zig-cache'
$verification = Join-Path $projectRoot 'build\layout-verification'
New-Item -ItemType Directory -Path $verification -Force | Out-Null
$items = @(
    @{ Name='stable'; Source='src\kernel\boot.c'; Reference='build\BOOTX64.EFI' },
    @{ Name='p1'; Source='experiments\candidates\p1\boot.c'; Reference='build\BOOTX64-heap-policy.EFI' },
    @{ Name='p2'; Source='experiments\candidates\p2\boot.c'; Reference='build\BOOTX64-heap-policy-alloc.EFI' },
    @{ Name='p3'; Source='experiments\candidates\p3\boot.c'; Reference='build\BOOTX64-wrapper-realloc.EFI' },
    @{ Name='p4'; Source='experiments\candidates\p4\boot.c'; Reference='build\BOOTX64-wrapper-fls.EFI' }
)
foreach ($item in $items) {
    $source = Join-Path $projectRoot $item.Source
    $reference = Join-Path $projectRoot $item.Reference
    $output = Join-Path $verification ("BOOTX64-{0}.EFI" -f $item.Name)
    & $zig build-exe -target x86_64-uefi -O ReleaseSmall -fno-stack-check -fno-stack-protector -mno-red-zone "-femit-bin=$output" -fstrip $source
    if ($LASTEXITCODE -ne 0) { throw "Zig compilation failed: $($item.Name)" }
    $current = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
    $expected = (Get-FileHash -LiteralPath $reference -Algorithm SHA256).Hash
    if ($current -ne $expected) { throw "Binary identity mismatch ($($item.Name)): rebuilt=$current previous=$expected" }
    Write-Output "PASS: $($item.Name) rebuild SHA256=$current"
}
if ($RunHostTests -or $RunQemu) {
    if (-not (Test-Path -LiteralPath $Python)) { throw "Python interpreter not found: $Python" }
}
if ($RunHostTests) {
    foreach ($script in @('test_console_endpoint_host.py','test_nt_file_host.py','test_nt_file_api_host.py','test_process_parameters_host.py','test_nt_objects_host.py')) {
        & $Python (Join-Path $projectRoot "tests\runners\$script") | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Host test failed: $script" }
        Write-Output "PASS: host $script"
    }
}
if ($RunQemu) {
    & $Python (Join-Path $projectRoot "tests\runners\test_console_wrapper_fls_candidate.py") | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'QEMU P4 regression failed' }
    Write-Output 'PASS: P4 QEMU regression'
}
Write-Output 'WINUX_LAYOUT_VERIFIED: stable and P1-P4 binaries match previous versions'
