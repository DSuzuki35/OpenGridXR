// Reproducible vector-drawn multi-resolution Windows icon; no external images.
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
class BuildIcon
{
    static GraphicsPath Round(float x,float y,float w,float h,float r)
    {
        var p=new GraphicsPath();p.AddArc(x,y,r*2,r*2,180,90);p.AddArc(x+w-r*2,y,r*2,r*2,270,90);
        p.AddArc(x+w-r*2,y+h-r*2,r*2,r*2,0,90);p.AddArc(x,y+h-r*2,r*2,r*2,90,90);p.CloseFigure();return p;
    }
    static void Box(Graphics g,float x,float y,float w,float h,float r,Color fill,Color stroke,float width)
    {
        using(var p=Round(x,y,w,h,r))using(var b=new SolidBrush(fill)){g.FillPath(b,p);if(width>0)using(var pen=new Pen(stroke,width))g.DrawPath(pen,p);}
    }
    static byte[] Draw(int size,string preview)
    {
        using(var image=new Bitmap(size,size,PixelFormat.Format32bppArgb))
        {
            using(var g=Graphics.FromImage(image))
            {
                g.SmoothingMode=SmoothingMode.AntiAlias;g.PixelOffsetMode=PixelOffsetMode.HighQuality;g.ScaleTransform(size/256f,size/256f);
                Box(g,8,8,240,240,56,Color.FromArgb(20,21,27),Color.Transparent,0);
                Box(g,28,59,200,138,20,Color.FromArgb(41,35,57),Color.FromArgb(165,140,255),6);
                Box(g,74,98,108,60,8,Color.FromArgb(137,111,246),Color.FromArgb(238,233,255),4);
                using(var divider=new Pen(Color.FromArgb(238,233,255),4))g.DrawLine(divider,128,100,128,156);
            }
            if(preview!=null)image.Save(preview,ImageFormat.Png);
            using(var memory=new MemoryStream()){image.Save(memory,ImageFormat.Png);return memory.ToArray();}
        }
    }
    static void Main(string[] args)
    {
        string folder=args[0];Directory.CreateDirectory(folder);int[] sizes={16,20,24,32,40,48,64,96,128,256};
        var images=new byte[sizes.Length][];for(int i=0;i<sizes.Length;i++)images[i]=Draw(sizes[i],sizes[i]==256?Path.Combine(folder,"quadview.png"):null);
        using(var file=File.Create(Path.Combine(folder,"quadview.ico")))using(var writer=new BinaryWriter(file))
        {
            writer.Write((ushort)0);writer.Write((ushort)1);writer.Write((ushort)sizes.Length);uint offset=(uint)(6+16*sizes.Length);
            for(int i=0;i<sizes.Length;i++){writer.Write((byte)(sizes[i]==256?0:sizes[i]));writer.Write((byte)(sizes[i]==256?0:sizes[i]));writer.Write((byte)0);writer.Write((byte)0);writer.Write((ushort)1);writer.Write((ushort)32);writer.Write((uint)images[i].Length);writer.Write(offset);offset+=(uint)images[i].Length;}
            foreach(var image in images)writer.Write(image);
        }
    }
}
