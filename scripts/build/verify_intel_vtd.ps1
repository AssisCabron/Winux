param([string]$Python = "C:\Program Files\Python310\python.exe")
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $root 'tools\zig-cache'
$disk=Join-Path $root 'build\vfs-preview\fat16-tree-disk.img'
$diskBefore=(Get-FileHash -Algorithm SHA256 -LiteralPath $disk).Hash
if($diskBefore -ne 'F88540F059EAB658AB51D02E313A3847DD14D55F3C9184A7AA6ED190BA6748DA'){throw 'Unexpected FAT16 fixture content'}
$stable=@{
 'winux-0.38-x64.iso'='700E6A38C0B7DBE312B6012810728AF8ECD24A984CD8287E0F644A73F1C343B0'
 'winux-0.38-wrapper-fls-candidate.iso'='4D1E20074E35FB9B03BBCB3043180CAC39C08CD8C5536EFC784E48A7E9270C77'
}
foreach($key in $stable.Keys){
 if((Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $root "build\$key")).Hash -ne $stable[$key]){throw "Stable ISO changed: $key"}
}
& $Python (Join-Path $root 'scripts\build\make_proc_space_preview.py') --experiment integration --vtd
if($LASTEXITCODE -ne 0){throw 'Experimental Intel VT-d build failed'}
$iso=Join-Path $root 'build\vtd-preview\winux-0.38-wrapper-fls-candidate.iso'
$hash=(Get-FileHash -Algorithm SHA256 -LiteralPath $iso).Hash.ToLowerInvariant()
$tags=@{positive='vtd-modern-positive';fault='vtd-hardware-fault';invalidate='vtd-iotlb-invalidation'}
foreach($mode in @('positive','fault','invalidate')){
 & $Python (Join-Path $root 'tests\runners\test_vtd_hardware_fault.py') $mode
 if($LASTEXITCODE -ne 0){throw "Hardware VT-d regression failed: $mode"}
 $j=Get-Content (Join-Path $root "build\$($tags[$mode])-test.json") -Raw | ConvertFrom-Json
 if(-not $j.passed -or $j.checks.Count -lt 5 -or $j.iso_sha256 -ne $hash){throw "Invalid VT-d evidence: $mode"}
}
if((Get-FileHash -Algorithm SHA256 -LiteralPath $disk).Hash -ne $diskBefore){throw 'FAT16 disk changed during DMA regressions'}
foreach($key in $stable.Keys){
 if((Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $root "build\$key")).Hash -ne $stable[$key]){throw "Stable ISO changed after VT-d regression: $key"}
}
Write-Output "WINUX_VTD_HARDWARE_OK: 3 QEMU modes, modern VirtIO ACCESS_PLATFORM, BDF-specific domain, forbidden DMA fault and IOTLB invalidation"
Write-Output "VT-d experimental ISO SHA256: $hash"
Write-Output "FAT16 fixture unchanged: $diskBefore"
