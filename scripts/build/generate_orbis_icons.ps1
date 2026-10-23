$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$assets = Join-Path $root 'src\ui\assets\generated'
$out = Join-Path $root 'build\orbis-gui-preview'
New-Item -ItemType Directory -Force -Path $out | Out-Null
Add-Type -AssemblyName System.Drawing

$source = @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;

public static class OrbisAtlasBuilder {
    const int Cell=64, Columns=8, Rows=4;

    static void DrawSheet(Bitmap source, Bitmap target, int columnOffset) {
        using (Graphics g=Graphics.FromImage(target)) {
            g.CompositingMode=CompositingMode.SourceCopy;
            g.CompositingQuality=CompositingQuality.HighQuality;
            g.InterpolationMode=InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode=PixelOffsetMode.HighQuality;
            g.SmoothingMode=SmoothingMode.HighQuality;
            for(int row=0;row<4;row++) for(int col=0;col<4;col++) {
                Rectangle dst=new Rectangle((columnOffset+col)*Cell,row*Cell,Cell,Cell);
                float sw=source.Width/4f,sh=source.Height/4f;
                RectangleF src=new RectangleF(col*sw,row*sh,sw,sh);
                g.DrawImage(source,dst,src,GraphicsUnit.Pixel);
            }
        }
    }

    // Image generation can leave tiny, disconnected semitransparent specks.
    // Keep meaningful components, then restore their antialiased fringe.
    static void CleanCell(Bitmap image,int ox,int oy,bool allowSeparateParts) {
        bool[,] solid=new bool[Cell,Cell],keep=new bool[Cell,Cell];
        for(int y=0;y<Cell;y++) for(int x=0;x<Cell;x++)
            solid[x,y]=image.GetPixel(ox+x,oy+y).A>=96;
        bool[,] seen=new bool[Cell,Cell];
        int[] qx=new int[Cell*Cell],qy=new int[Cell*Cell];
        var components=new List<List<Point>>();
        for(int sy=0;sy<Cell;sy++) for(int sx=0;sx<Cell;sx++) {
            if(!solid[sx,sy]||seen[sx,sy])continue;
            int head=0,tail=0; qx[tail]=sx;qy[tail++]=sy;seen[sx,sy]=true;
            var points=new List<Point>();
            while(head<tail){
                int x=qx[head],y=qy[head++];points.Add(new Point(x,y));
                for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
                    int nx=x+dx,ny=y+dy;
                    if(nx<0||ny<0||nx>=Cell||ny>=Cell||seen[nx,ny]||!solid[nx,ny])continue;
                    seen[nx,ny]=true;qx[tail]=nx;qy[tail++]=ny;
                }
            }
            if(points.Count>=10)components.Add(points);
        }
        int largest=-1;
        for(int i=0;i<components.Count;i++)if(largest<0||components[i].Count>components[largest].Count)largest=i;
        for(int i=0;i<components.Count;i++)if(allowSeparateParts||i==largest)
            foreach(Point p in components[i])keep[p.X,p.Y]=true;
        bool[,] fringe=(bool[,])keep.Clone();
        for(int pass=0;pass<3;pass++){
            bool[,] next=(bool[,])fringe.Clone();
            for(int y=0;y<Cell;y++)for(int x=0;x<Cell;x++)if(fringe[x,y])
                for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
                    int nx=x+dx,ny=y+dy;
                    if(nx>=0&&ny>=0&&nx<Cell&&ny<Cell&&image.GetPixel(ox+nx,oy+ny).A>=8)next[nx,ny]=true;
                }
            fringe=next;
        }
        for(int y=0;y<Cell;y++)for(int x=0;x<Cell;x++)
            if(!fringe[x,y]||(!allowSeparateParts&&(x<5||x>=59||y<4||y>=60)))
                image.SetPixel(ox+x,oy+y,Color.Transparent);
    }

    public static void Build(string appPath,string systemPath,string pngPath,string rawPath) {
        using(Bitmap app=new Bitmap(appPath),system=new Bitmap(systemPath),
              atlas=new Bitmap(Columns*Cell,Rows*Cell,PixelFormat.Format32bppArgb)) {
            DrawSheet(app,atlas,0); DrawSheet(system,atlas,4);
            for(int row=0;row<4;row++)for(int col=0;col<8;col++)
                CleanCell(atlas,col*Cell,row*Cell,col>=4);
            atlas.Save(pngPath,ImageFormat.Png);
            using(FileStream stream=File.Create(rawPath))
                for(int y=0;y<atlas.Height;y++)for(int x=0;x<atlas.Width;x++){
                    Color p=atlas.GetPixel(x,y);
                    stream.WriteByte(p.B);stream.WriteByte(p.G);stream.WriteByte(p.R);stream.WriteByte(p.A);
                }
        }
    }
}
'@
Add-Type -TypeDefinition $source -ReferencedAssemblies System.Drawing

$app = Join-Path $assets 'orbis-app-icons-atlas-v1.png'
$system = Join-Path $assets 'orbis-system-icons-atlas-v2.png'
$preview = Join-Path $out 'orbis-icons-atlas.png'
$raw = Join-Path $out 'orbis-icons-atlas.bgra'
[OrbisAtlasBuilder]::Build($app,$system,$preview,$raw)
if ((Get-Item $raw).Length -ne (512*256*4)) { throw 'Invalid ORBIS icon atlas size' }
Write-Host 'ORBIS_ICON_ATLAS_OK: 32 generated icons, 512x256 BGRA'
