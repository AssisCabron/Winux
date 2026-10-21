param([string]$Python = "$env:ProgramFiles\Python310\python.exe")
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build=Join-Path $root 'build'
if(!(Test-Path $Python)){throw "Python missing: $Python"}
$known=@{
 'winux-0.38-x64.iso'='700E6A38C0B7DBE312B6012810728AF8ECD24A984CD8287E0F644A73F1C343B0'
 'winux-0.38-wrapper-fls-candidate.iso'='4D1E20074E35FB9B03BBCB3043180CAC39C08CD8C5536EFC784E48A7E9270C77'
}
foreach($name in $known.Keys){
    $sha=(Get-FileHash -LiteralPath (Join-Path $build $name) -Algorithm SHA256).Hash
    if($sha -ne $known[$name]){throw "Historical ISO modified: $name"}
}
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $root 'tools\zig-cache'
& $Python (Join-Path $root 'scripts\build\generate_nt_abi.py') --check
if($LASTEXITCODE -ne 0){throw 'NT ABI drift'}
& $Python (Join-Path $root 'scripts\build\make_proc_space_preview.py') --experiment preemption
if($LASTEXITCODE -ne 0){throw 'Preemption candidate build failed'}
& $Python (Join-Path $root 'tests\runners\test_console_wrapper_fls_candidate.py') --iso 'build/preemption-preview/winux-0.38-wrapper-fls-candidate.iso' --result-tag 'console-managed-preemption-p4' --check-process-manager --check-preemption | Out-Null
if($LASTEXITCODE -ne 0){throw 'IRQ0/ProcessManager QEMU regression failed'}
$j=Get-Content (Join-Path $build 'console-managed-preemption-p4-test.json') -Raw|ConvertFrom-Json
if(!$j.passed -or $j.checks.Count -lt 39){throw 'Incomplete managed preemption regression'}
$trace=Get-Content (Join-Path $build 'console-managed-preemption-p4-serial.log') -Raw
foreach($marker in @('PIT_PREEMPT_OK:', 'PIT_THREAD_STACKS_OK:', 'PIT_THREAD_FPU_OK:', 'PIT_PROC_MANAGER_OK:', 'PROC_MANAGER_OK:')){
    if(([regex]::Matches($trace,[regex]::Escape($marker))).Count -lt 2){throw "Expected two complete cycles of $marker"}
}
foreach($name in $known.Keys){
    $sha=(Get-FileHash -LiteralPath (Join-Path $build $name) -Algorithm SHA256).Hash
    if($sha -ne $known[$name]){throw "Historical ISO modified during preemption validation: $name"}
}
Write-Output "WINUX_IRQ_PREEMPT_OK: $($j.checks.Count) QEMU checks; 2 cycles; no YIELD in CPL3 spin loops; managed process reaping"
Write-Output "ISO_SHA256=$($j.iso_sha256)"
