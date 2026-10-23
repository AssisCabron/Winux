# ORBIS OS typographic system (Orbis Sans concept / locally rasterized Segoe UI)
# Build-only GDI+ rasterization. No font binaries redistributed.
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$out=Join-Path $root 'build\orbis-gui-preview\orbis_font.h'
New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null
$builder=[System.Text.StringBuilder]::new()
[void]$builder.AppendLine('/* Orbis Sans concept: antialiased glyph atlases rasterized from local Segoe UI. */')
[void]$builder.AppendLine('#ifndef ORBIS_PREVIEW_FONT_H')
[void]$builder.AppendLine('#define ORBIS_PREVIEW_FONT_H')
$faces=@(
  # The framebuffer UI is designed around a restrained 15 px reading face.
  # The old 17/31 px pair made every label compete with the page heading.
  @{id='body';width=22;height=25;point=15;bold=$false},
  @{id='display';width=40;height=42;point=28;bold=$false}
)
foreach($face in $faces){
  $W=[int]$face.width;$H=[int]$face.height
  $name=[string]$face.id
  # Local Windows GDI+ maps the conceptual SemiBold heading to Bold.
  $fontStyle=if($face.bold){[System.Drawing.FontStyle]::Bold}else{[System.Drawing.FontStyle]::Regular}
  $font=[System.Drawing.Font]::new('Segoe UI',[int]$face.point,$fontStyle,[System.Drawing.GraphicsUnit]::Pixel)
  $fmt=[System.Drawing.StringFormat]::GenericTypographic
  $advances=[System.Collections.Generic.List[int]]::new()
  [void]$builder.AppendLine(('#define ORBIS_{0}_W {1}' -f $name.ToUpperInvariant(),$W))
  [void]$builder.AppendLine(('#define ORBIS_{0}_H {1}' -f $name.ToUpperInvariant(),$H))
  [void]$builder.AppendLine(('static const unsigned char orbis_{0}_alpha[95][{1}]={{' -f $name,($W*$H)))
  for($c=32;$c -le 126;$c++){
    $bmp=[System.Drawing.Bitmap]::new($W,$H,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g=[System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.TextRenderingHint=[System.Drawing.Text.TextRenderingHint]::AntiAlias
    $g.DrawString([string][char]$c,$font,[System.Drawing.Brushes]::White,0,-2,$fmt)
    $left=$W;$right=-1
    for($y=0;$y -lt $H;$y++){for($x=0;$x -lt $W;$x++){
      if($bmp.GetPixel($x,$y).A -gt 6){
        if($x -lt $left){$left=$x};if($x -gt $right){$right=$x}
      }
    }}
    $advance=if($c -eq 32){[int]($face.point*0.34)}else{[Math]::Min($W-1,[Math]::Max(4,$right-$left+2))}
    $advances.Add([int]$advance)
    if($right -lt $left){$left=0}
    [void]$builder.Append('{')
    for($y=0;$y -lt $H;$y++){for($x=0;$x -lt $W;$x++){
      $sx=$x+$left
      $v=if($sx -ge 0 -and $sx -lt $W){$bmp.GetPixel($sx,$y).A}else{0}
      [void]$builder.Append([int]$v);[void]$builder.Append(',')
    }}
    [void]$builder.AppendLine('},')
    $g.Dispose();$bmp.Dispose()
  }
  $font.Dispose()
  [void]$builder.AppendLine('};')
  [void]$builder.Append(('static const unsigned char orbis_{0}_advance[95]={{' -f $name))
  foreach($w in $advances){[void]$builder.Append($w);[void]$builder.Append(',')}
  [void]$builder.AppendLine('};')
}
[void]$builder.AppendLine('/* Back-compatible names for smaller desktop labels */')
[void]$builder.AppendLine('#define orbis_font_8bpp orbis_body_alpha')
[void]$builder.AppendLine('#define orbis_font_advance orbis_body_advance')
[void]$builder.AppendLine('#endif')
[System.IO.File]::WriteAllText($out,$builder.ToString(),[System.Text.Encoding]::ASCII)
Write-Output ("ORBIS_TYPE_SYSTEM_OK: body + display, "+(Get-Item $out).Length+" bytes")
