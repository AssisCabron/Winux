param(
    [string]$Python="C:\Program Files\Python310\python.exe",
    [switch]$SkipFullRegression
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build=Join-Path $root 'build'
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $root 'tools\zig-cache'
$disk=Join-Path $build 'user-pe-preview\user-pe-fat16-v2.img'
$tampered=Join-Path $build 'user-pe-preview\user-pe-tampered-fat16-v2.img'
$manifest=Join-Path $build 'user-pe-preview\user-pe-fixture-v2.json'
$tmanifest=Join-Path $build 'user-pe-preview\user-pe-tampered-fixture-v2.json'
$prior=Join-Path $build 'vfs-preview\fat16-tree-disk.img'
$parentHash='F88540F059EAB658AB51D02E313A3847DD14D55F3C9184A7AA6ED190BA6748DA'
if((Get-FileHash -LiteralPath $prior -Algorithm SHA256).Hash -ne $parentHash){throw 'Prior FAT16 image modified'}
if(-not(Test-Path $disk) -or -not(Test-Path $tampered)){throw 'Generate the USER-PE64 v2 fixture before verifying'}
$m=Get-Content $manifest -Raw|ConvertFrom-Json
$tm=Get-Content $tmanifest -Raw|ConvertFrom-Json
if((Get-FileHash -LiteralPath $disk -Algorithm SHA256).Hash -ne $m.disk_sha256){throw 'USER-PE64 fixture SHA mismatch'}
if((Get-FileHash -LiteralPath $tampered -Algorithm SHA256).Hash -ne $tm.sha256){throw 'Tamper fixture SHA mismatch'}
$headers=Get-Content (Join-Path $build 'user-pe-preview\user-pe-allowlist.h') -Raw
if(-not $headers.Contains($m.files.'USER64.EXE'.fnv64)){throw 'Build allowlist does not match PE fixture v2'}
$stable=@{
 'winux-0.38-x64.iso'='700E6A38C0B7DBE312B6012810728AF8ECD24A984CD8287E0F644A73F1C343B0'
 'winux-0.38-wrapper-fls-candidate.iso'='4D1E20074E35FB9B03BBCB3043180CAC39C08CD8C5536EFC784E48A7E9270C77'
}
foreach($f in $stable.Keys){
 if((Get-FileHash (Join-Path $build $f) -Algorithm SHA256).Hash -ne $stable[$f]){throw "Stable ISO changed: $f"}
}
& $Python (Join-Path $root 'scripts\build\make_proc_space_preview.py') --experiment integration --user-pe
if($LASTEXITCODE -ne 0){throw 'USER-PE64 build failed'}
$iso=Join-Path $build 'user-pe-preview\winux-0.38-wrapper-fls-candidate.iso'
$isoHash=(Get-FileHash -LiteralPath $iso -Algorithm SHA256).Hash.ToLowerInvariant()
foreach($item in @(@('tests\runners\test_user_pe_qemu.py','user-pe64-test.json',5),
                   @('tests\runners\test_user_pe_tamper_qemu.py','user-pe64-tamper-test.json',2))){
 & $Python (Join-Path $root $item[0])
 if($LASTEXITCODE -ne 0){throw "USER-PE64 QEMU test failed: $($item[0])"}
 $j=Get-Content (Join-Path $build $item[1]) -Raw|ConvertFrom-Json
 if(-not $j.passed -or $j.checks.Count -lt $item[2] -or $j.iso_sha256 -ne $isoHash){
   throw "USER-PE64 evidence mismatch: $($item[1])"
 }
}
if(-not $SkipFullRegression){
 $args=@('--iso','build/user-pe-preview/winux-0.38-wrapper-fls-candidate.iso',
 '--result-tag','console-user-pe-foundation-v2',
 '--block-image','build/user-pe-preview/user-pe-fat16-v2.img',
 '--check-proc-space','--check-scheduler','--check-user-scheduler','--check-services',
 '--check-vfs','--check-process-manager','--check-preemption','--check-permanent',
 '--check-sleep','--check-multithread','--check-ipc-wait','--check-fat16-tree',
 '--check-isolation-dma','--check-user-pe')
 & $Python (Join-Path $root 'tests\runners\test_console_wrapper_fls_candidate.py') @args | Out-Null
 if($LASTEXITCODE -ne 0){throw 'USER-PE64 Foundation regression failed'}
 $j=Get-Content (Join-Path $build 'console-user-pe-foundation-v2-test.json') -Raw|ConvertFrom-Json
 if(-not $j.passed -or $j.checks.Count -lt 67 -or $j.iso_sha256 -ne $isoHash){
   throw 'USER-PE64 integrated result incomplete'
 }
}
if((Get-FileHash -LiteralPath $disk -Algorithm SHA256).Hash -ne $m.disk_sha256){throw 'USER-PE64 disk mutated'}
if((Get-FileHash -LiteralPath $prior -Algorithm SHA256).Hash -ne $parentHash){throw 'Original FAT16 changed'}
foreach($f in $stable.Keys){
 if((Get-FileHash (Join-Path $build $f) -Algorithm SHA256).Hash -ne $stable[$f]){throw "Stable ISO changed: $f"}
}
Write-Output 'WINUX_USER_PE64_OK: external PE32+ from FAT16 executed in isolated CPL3 with lifecycle and fault recovery'
Write-Output "USER_PE_ISO_SHA256=$isoHash"
Write-Output 'Parent/stable FAT16 and ISO hashes unchanged'
