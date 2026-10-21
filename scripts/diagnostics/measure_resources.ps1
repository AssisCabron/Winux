param([string]$Python = "$env:ProgramFiles\Python310\python.exe")
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $root 'build'
$tag = 'console-memory-probe'
$stdout = Join-Path $build 'winux-memory-probe-stdout.log'
$stderr = Join-Path $build 'winux-memory-probe-stderr.log'
$arguments = @('tests/runners/test_console_wrapper_fls_candidate.py', '--iso', 'build/foundation-preview/winux-0.38-wrapper-fls-candidate.iso', '--result-tag', $tag, '--block-image', 'build/vfs-preview/fat16-test-disk.img', '--check-vfs', '--check-services')
$started=Get-Date
$proc=Start-Process -FilePath $Python -WorkingDirectory $root -ArgumentList $arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$samples = New-Object System.Collections.ArrayList
try {
    do {
        $qemu=Get-Process -Name 'qemu-system-x86_64' -ErrorAction SilentlyContinue | Where-Object {$_.StartTime -ge $started.AddSeconds(-3)} | Sort-Object StartTime -Descending | Select-Object -First 1
        if($qemu){
            [void]$samples.Add([ordered]@{
                working_set_bytes = [int64]$qemu.WorkingSet64
                private_bytes = [int64]$qemu.PrivateMemorySize64
                peak_working_set_bytes = [int64]$qemu.PeakWorkingSet64
                cpu_seconds = [double]$qemu.CPU
            })
        }
        Start-Sleep -Milliseconds 750
        $proc.Refresh()
    } while (-not $proc.HasExited)
} finally {
    $proc.Refresh()
}
$recordPath = Join-Path $build "$tag-test.json"
if(!(Test-Path -LiteralPath $recordPath)){
    throw "QEMU regression did not produce a test result; Python exit $($proc.ExitCode)"
}
$test=Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
if(!$test.passed -or $proc.ExitCode -ne 0){throw "QEMU regression failed; exit=$($proc.ExitCode) reason=$($test.failure)"}
$log=Get-Content -LiteralPath (Join-Path $build "$tag-serial.log") -Raw
$matches = [regex]::Matches($log, 'Reserved kernel pool: (\d+) / (\d+) free pages')
if($matches.Count -eq 0){throw 'Guest memory reports missing'}
$free = @($matches | ForEach-Object {[int]$_.Groups[1].Value})
$total = [int]$matches[0].Groups[2].Value
if($samples.Count -lt 4){throw 'Not enough live QEMU memory samples'}
$ws = @($samples | ForEach-Object {$_.working_set_bytes})
$pv = @($samples | ForEach-Object {$_.private_bytes})
$first = $samples[0]; $last=$samples[$samples.Count - 1]
$efi=Join-Path $build 'foundation-preview\BOOTX64-wrapper-fls.EFI'
$raw=[IO.File]::ReadAllBytes($efi)
$pe=[BitConverter]::ToInt32($raw,0x3c)
$sizeOfImage=[BitConverter]::ToUInt32($raw,$pe+24+56)
$result=[ordered]@{
    test_passed=[bool]$test.passed
    test_checks=$test.checks.Count
    vm_ram_configured_bytes=256MB
    efi_image_mapped_bytes=[int64]$sizeOfImage
    efi_file_size_bytes=(Get-Item -LiteralPath $efi).Length
    kernel_page_pool_reserved_bytes=[int64]$total * 4096
    kernel_page_pool_used_min_bytes=[int64]($total-($free | Measure-Object -Maximum).Maximum)*4096
    kernel_page_pool_used_max_bytes=[int64]($total-($free | Measure-Object -Minimum).Minimum)*4096
    kernel_page_pool_free_pages_sampled=$free
    qemu_host_working_set_min_bytes=[int64]($ws | Measure-Object -Minimum).Minimum
    qemu_host_working_set_max_bytes=[int64]($ws | Measure-Object -Maximum).Maximum
    qemu_host_working_set_average_bytes=[int64]($ws | Measure-Object -Average).Average
    qemu_host_private_bytes_min=[int64]($pv | Measure-Object -Minimum).Minimum
    qemu_host_private_bytes_max=[int64]($pv | Measure-Object -Maximum).Maximum
    qemu_host_private_bytes_average=[int64]($pv | Measure-Object -Average).Average
    qemu_host_cpu_seconds_delta=[double]($last.cpu_seconds-$first.cpu_seconds)
    sample_count=$samples.Count
    sample_interval_ms=750
    test_run_seconds=[Math]::Round(((Get-Date)-$started).TotalSeconds,2)
    caveat='QEMU host memory is NOT Winux guest RAM used; EFI mapped memory includes static 8 MiB page pool and bundled RAMFS; unused UEFI conventional pages are not reclaimed.'
}
$dest=Join-Path $build 'winux-resource-usage.json'
[IO.File]::WriteAllText($dest,($result|ConvertTo-Json -Depth 8), (New-Object System.Text.UTF8Encoding($false)))
Write-Output ($result|ConvertTo-Json -Depth 8)
Write-Output "RESOURCE_REPORT=$dest"
