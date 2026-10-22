# Generate a proportional 8-bit antialiased glyph atlas from the system font.
# Only raster pixels are included in the EFI image; no .ttf files are copied.
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$out=Join-Path $root 'build\orbis-gui-preview\orbis_font.h'
New-Item -ItemType Directory -Force -Path (Split-Path $out)|Out-Null
$W=24;$H=28
$font=[System.Drawing.Font]::new('Segoe UI',18,[System.Drawing.FontStyle]::Regular,[System.Drawing.GraphicsUnit]::Pixel)
$format=[System.Drawing.StringFormat]::GenericTypographic
$builder=[System.Text.StringBuilder]::new()
[void]$builder.AppendLine('#ifndef ORBIS_PREVIEW_FONT_H')
[void]$builder.AppendLine('#define ORBIS_PREVIEW_FONT_H')
[void]$builder.AppendLine('/* GDI+ Segoe UI rasterization. 8-bit coverage, cropped and proportional. */')
[void]$builder.AppendLine('static const unsigned char orbis_font_8bpp[95][672]={')
$widths=[System.Collections.Generic.List[byte]]::new()
for($c=32;$c -le 126;$c++){
  $bmp=[System.Drawing.Bitmap]::new($W,$H,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g=[System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::Transparent)
  $g.TextRenderingHint=[System.Drawing.Text.TextRenderingHint]::AntiAlias
  $g.DrawString([string][char]$c,$font,[System.Drawing.Brushes]::White,0,-1,$format)
  $left=$W;$right=-1
  for($y=0;$y -lt $H;$y++){for($x=0;$x -lt $W;$x++){
    if($bmp.GetPixel($x,$y).A -gt 6) {
      if($x -lt $left){$left=$x};if($x -gt $right){$right=$x}
    }
  }}
  $charsize=if($c -eq 32){6}else{[Math]::Max(3,$right-$left+2)}
  $widths.Add([byte]$charsize)
  if($right -lt $left){$left=0}
  [void]$builder.Append('{')
  for($y=0;$y -lt $H;$y++){
    for($x=0;$x -lt $W;$x++){
      $sx=$x+$left
      $v=if($sx -ge 0 -and $sx -lt $W){$bmp.GetPixel($sx,$y).A}else{0}
      [void]$builder.Append([int]$v);[void]$builder.Append(',')
    }
  }
  [void]$builder.AppendLine('},')
  $g.Dispose();$bmp.Dispose()
}
$font.Dispose()
[void]$builder.AppendLine('};')
[void]$builder.Append('static const unsigned char orbis_font_advance[95]={')
foreach($w in $widths){[void]$builder.Append([int]$w);[void]$builder.Append(',')}
[void]$builder.AppendLine('};')
[void]$builder.AppendLine('#endif')
[System.IO.File]::WriteAllText($out,$builder.ToString(),[System.Text.Encoding]::ASCII)
Write-Output "ORBIS_FONT_PROPORTIONAL_OK: $out ($((Get-Item $out).Length) bytes)"
