param([string]$Python = "$env:ProgramFiles\Python310\python.exe")
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build=Join-Path $root 'build'
$disk=Join-Path $build 'block-preview\driver-test-disk.img'
if (-not (Test-Path -LiteralPath $Python)) {throw "Python missing: $Python"}
if (-not (Test-Path -LiteralPath $disk)) {
    & $Python (Join-Path $root 'scripts\build\make_block_fixture.py')
    if($LASTEXITCODE -ne 0){throw "Failed to create isolated block fixture"}
}
$diskBefore=(Get-FileHash -LiteralPath $disk -Algorithm SHA256).Hash
$gold=@{
 'winux-0.38-x64.iso'='700E6A38C0B7DBE312B6012810728AF8ECD24A984CD8287E0F644A73F1C343B0'
 'winux-0.38-wrapper-fls-candidate.iso'='4D1E20074E35FB9B03BBCB3043180CAC39C08CD8C5536EFC784E48A7E9270C77'
}
foreach($name in $gold.Keys) {
    if((Get-FileHash -LiteralPath (Join-Path $build $name) -Algorithm SHA256).Hash -ne $gold[$name]) {
        throw "Stable image changed: $name"
    }
}
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $root 'tools\zig-cache'
& $Python (Join-Path $root 'scripts\build\generate_nt_abi.py') --check
if($LASTEXITCODE -ne 0){throw 'Existing NT ABI contract validation failed'}
& $Python (Join-Path $root 'scripts\build\make_proc_space_preview.py') --experiment integration
if($LASTEXITCODE -ne 0){throw 'Foundation ISO compilation failed'}
& $Python (Join-Path $root 'tests\runners\test_console_wrapper_fls_candidate.py') --iso 'build/foundation-preview/winux-0.38-wrapper-fls-candidate.iso' --result-tag console-foundations-p4 --block-image 'build/block-preview/driver-test-disk.img' --check-proc-space --check-scheduler --check-user-scheduler | Out-Null
if($LASTEXITCODE -ne 0){throw 'Foundation QEMU regression failed'}
$result=Get-Content (Join-Path $build 'console-foundations-p4-test.json') -Raw|ConvertFrom-Json
if(-not $result.passed -or $result.checks.Count -lt 41){throw 'Missing foundation test groups'}
if((Get-FileHash -LiteralPath $disk -Algorithm SHA256).Hash -ne $diskBefore){throw 'Fixture disk was changed'}
foreach($name in $gold.Keys) {
    if((Get-FileHash -LiteralPath (Join-Path $build $name) -Algorithm SHA256).Hash -ne $gold[$name]) {
        throw "Stable image changed during integration: $name"
    }
}
Write-Output "WINUX_FOUNDATIONS_OK: $($result.checks.Count) QEMU groups passed (PCI PnP/DMA, per-CR3 CPL3 yield/resume, x64 context)"
Write-Output "Disk remained read-only: SHA256 $diskBefore"
Write-Output "Integrated ISO: $($result.iso_sha256)"
