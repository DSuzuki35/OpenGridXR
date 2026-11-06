using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
using System.Runtime.InteropServices;

namespace LmuQuadViewLauncher {
    // Native controls retain keyboard/accessibility behavior; only their surfaces change.
    static class NativeChrome {
        [DllImport("dwmapi.dll")]static extern int DwmSetWindowAttribute(IntPtr window,int attribute,ref int value,int size);
        internal static void DarkTitleBar(Form form){
            try{int dark=1;DwmSetWindowAttribute(form.Handle,20,ref dark,sizeof(int));}
            catch(DllNotFoundException){}catch(EntryPointNotFoundException){}
        }
        internal static GraphicsPath Rounded(RectangleF bounds,float radius) {
            var path=new GraphicsPath();float d=Math.Min(radius*2,Math.Min(bounds.Width,bounds.Height));
            if(d<1){path.AddRectangle(bounds);return path;}
            path.AddArc(bounds.Left,bounds.Top,d,d,180,90);path.AddArc(bounds.Right-d,bounds.Top,d,d,270,90);
            path.AddArc(bounds.Right-d,bounds.Bottom-d,d,d,0,90);path.AddArc(bounds.Left,bounds.Bottom-d,d,d,90,90);path.CloseFigure();return path;
        }
    }
    sealed class RoundedButton : Button {
        bool hover,pressed;
        public RoundedButton(){SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);}
        protected override void OnMouseEnter(EventArgs e){hover=true;Invalidate();base.OnMouseEnter(e);}
        protected override void OnMouseLeave(EventArgs e){hover=pressed=false;Invalidate();base.OnMouseLeave(e);}
        protected override void OnMouseDown(MouseEventArgs e){pressed=true;Invalidate();base.OnMouseDown(e);}
        protected override void OnMouseUp(MouseEventArgs e){pressed=false;Invalidate();base.OnMouseUp(e);}
        protected override void OnPaint(PaintEventArgs e){
            var surface=Parent;while(surface!=null&&surface.BackColor.A<255)surface=surface.Parent;
            e.Graphics.Clear(surface==null?Theme.Background:surface.BackColor);e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;
            var bounds=new RectangleF(.5f,.5f,Width-1,Height-1);float scale=DeviceDpi/96f;
            var color=!Enabled?Theme.Card:pressed?FlatAppearance.MouseDownBackColor:hover?FlatAppearance.MouseOverBackColor:BackColor;
            using(var path=NativeChrome.Rounded(bounds,7*scale))using(var brush=new SolidBrush(color))using(var pen=new Pen(FlatAppearance.BorderColor)){
                e.Graphics.FillPath(brush,path);if(FlatAppearance.BorderSize>0)e.Graphics.DrawPath(pen,path);
            }
            var textBounds=Rectangle.Inflate(ClientRectangle,-(int)(8*scale),-2);
            var flags=TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis|TextFormatFlags.SingleLine;
            flags|=TextAlign==ContentAlignment.MiddleLeft?TextFormatFlags.Left:TextFormatFlags.HorizontalCenter;
            TextRenderer.DrawText(e.Graphics,Text,Font,textBounds,Enabled?ForeColor:Theme.Muted,flags);
            if(Focused&&ShowFocusCues)ControlPaint.DrawFocusRectangle(e.Graphics,Rectangle.Inflate(ClientRectangle,-4,-4),ForeColor,color);
        }
    }
}
