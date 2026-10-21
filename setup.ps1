param([string]$Python='python', [switch]$WithQemu)
$ErrorActionPreference='Stop'
$toolDir=Join-Path $PSScriptRoot 'tools'
New-Item -ItemType Directory -Force $toolDir | Out-Null
function Fetch-Verified($Url,$File,$Algorithm,$Expected){
 if(!(Test-Path -LiteralPath $File)){
  & curl.exe -fL --connect-timeout 20 --max-time 600 $Url -o $File
  if($LASTEXITCODE -ne 0){throw "Download failed: $Url"}
 }
 if((Get-FileHash -LiteralPath $File -Algorithm $Algorithm).Hash.ToLower() -ne $Expected){throw "Hash mismatch: $File"}
}
Fetch-Verified 'https://ziglang.org/download/0.14.1/zig-x86_64-windows-0.14.1.zip' "$toolDir/zig-curl.zip" SHA256 '554f5378228923ffd558eac35e21af020c73789d87afeabf4bfd16f2e6feed2c'
if(!(Test-Path "$toolDir/zig-x86_64-windows-0.14.1/zig.exe")){
 Expand-Archive -LiteralPath "$toolDir/zig-curl.zip" -DestinationPath $toolDir
}
& $Python -m pip install --target "$toolDir/python" pycdlib==1.14.0
if($LASTEXITCODE -ne 0){throw 'pycdlib installation failed'}
if($WithQemu){
 Fetch-Verified 'https://qemu.weilnetz.de/w64/qemu-w64-setup-20260811.exe' "$toolDir/qemu-curl.exe" SHA512 '5bcf9eed634e8575a37b74f445af41a2fe4106da512d0c30c368301d4c105037fdfab40a5287367a28a957624cddebbc8c07e16c88ab6634f554cdf3d16bf543'
 & 7z x "$toolDir/qemu-curl.exe" "-o$toolDir/qemu" -y
 if($LASTEXITCODE -ne 0){throw 'QEMU extraction failed; 7z must be available'}
}
