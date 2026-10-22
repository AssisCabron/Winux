# Convert high-resolution ORBIS photo to uncompressed RGB888.
# Embedded with assembler .incbin, not as huge C hex literals.
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$src=Join-Path $root 'src\ui\assets\orbis-wallpaper-golden.jpg'
$out=Join-Path $root 'build\orbis-gui-preview\wallpaper.rgb'
if(!(Test-Path $src)){throw "Missing wallpaper source"}
$source=[System.Drawing.Bitmap]::new($src)
$width=1280;$height=800
$dest=[System.Drawing.Bitmap]::new($width,$height,[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$g=[System.Drawing.Graphics]::FromImage($dest)
try{
    $g.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode=[System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality=[System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.Clear([System.Drawing.Color]::Black)
    $srcRatio=$source.Width/[double]$source.Height
    $targetRatio=$width/[double]$height
    if($srcRatio -gt $targetRatio){
       $sw=[int]($source.Height*$targetRatio);$sx=[int](($source.Width-$sw)/2)
       $rect=[System.Drawing.Rectangle]::new($sx,0,$sw,$source.Height)
    }else{
       $sh=[int]($source.Width/$targetRatio);$sy=[int](($source.Height-$sh)/2)
       $rect=[System.Drawing.Rectangle]::new(0,$sy,$source.Width,$sh)
    }
    $g.DrawImage($source,[System.Drawing.Rectangle]::new(0,0,$width,$height),$rect,[System.Drawing.GraphicsUnit]::Pixel)
    $bounds=[System.Drawing.Rectangle]::new(0,0,$width,$height)
    $bits=$dest.LockBits($bounds,[System.Drawing.Imaging.ImageLockMode]::ReadOnly,[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try{
        $row=[byte[]]::new($width*3)
        $buffer=[byte[]]::new($width*$height*3)
        for($y=0;$y -lt $height;$y++){
           $ptr=[IntPtr]::Add($bits.Scan0,$y*$bits.Stride)
           [System.Runtime.InteropServices.Marshal]::Copy($ptr,$row,0,$row.Length)
           for($x=0;$x -lt $width;$x++){
             $i=$x*3;$j=($y*$width+$x)*3
             $buffer[$j]=$row[$i+2];$buffer[$j+1]=$row[$i+1];$buffer[$j+2]=$row[$i]
           }
        }
        [System.IO.File]::WriteAllBytes($out,$buffer)
    }finally{$dest.UnlockBits($bits)}
    Write-Output ("ORBIS_RGB888_WALLPAPER_OK: "+$width+"x"+$height+" "+(Get-Item $out).Length+" bytes")
}finally{$g.Dispose();$dest.Dispose();$source.Dispose()}
