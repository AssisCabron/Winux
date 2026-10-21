$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$dir = Join-Path $root 'build\plugin-preview'
$exe = Join-Path $dir 'plugintest.exe'
$dll = Join-Path $dir 'winuxplugin.dll'
$stdout = Join-Path $dir 'windows-plugin-stdout.txt'
$stderr = Join-Path $dir 'windows-plugin-stderr.txt'
if (!(Test-Path $exe) -or !(Test-Path $dll)) { throw 'Plugin fixture not compiled' }
$process = Start-Process -FilePath $exe -WorkingDirectory $dir -NoNewWindow -PassThru -Wait -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$printed = [System.IO.File]::ReadAllText($stdout)
$errors = [System.IO.File]::ReadAllText($stderr)
$passed = $process.ExitCode -eq 0 -and $printed.Contains('WINUX_CUSTOM_DLL_OK: DllMain, exports, data, GetProcAddress, GetTickCount') -and [string]::IsNullOrWhiteSpace($errors)
$result = [ordered]@{
    passed = [bool]$passed
    exit_code = [int]$process.ExitCode
    stdout = $printed
    stderr = $errors
    plugin_dll_sha256 = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
    fixture_exe_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    description = 'Self-authored Windows PE DLL and EXE, executed locally by the Windows loader'
}
$json = $result | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText((Join-Path $dir 'plugin-windows-test.json'), $json, (New-Object System.Text.UTF8Encoding($false)))
Write-Output ($result | ConvertTo-Json -Depth 5)
if (!$passed) { throw "Windows plugin contract failed: $($process.ExitCode)" }
