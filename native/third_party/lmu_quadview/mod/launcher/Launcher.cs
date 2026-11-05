using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace LmuQuadViewLauncher
{
    static class Theme
    {
        public static readonly Color Background = Color.FromArgb(18, 19, 24);
        public static readonly Color Sidebar = Color.FromArgb(22, 23, 29);
        public static readonly Color Card = Color.FromArgb(29, 30, 38);
        public static readonly Color Field = Color.FromArgb(22, 23, 30);
        public static readonly Color Line = Color.FromArgb(49, 50, 63);
        public static readonly Color Text = Color.FromArgb(237, 236, 245);
        public static readonly Color Muted = Color.FromArgb(153, 154, 174);
        public static readonly Color Accent = Color.FromArgb(137, 111, 246);
        public static readonly Color AccentDark = Color.FromArgb(49, 40, 79);
        public static Label Label(string text, float size, Color color)
        {
            return new Label { Text = text, ForeColor = color, BackColor = Color.Transparent,
                Font = new Font("Segoe UI", size), AutoSize = false, Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleLeft, Margin = new Padding(0) };
        }
        public static Button Button(string text, bool primary)
        {
            var b = new Button { Text = text, FlatStyle = FlatStyle.Flat, Cursor = Cursors.Hand,
                BackColor = primary ? Accent : Card, ForeColor = Text, Height = 42,
                Font = new Font("Segoe UI", 10F, FontStyle.Bold), UseVisualStyleBackColor = false };
            b.FlatAppearance.BorderColor = primary ? Accent : Line;
            b.FlatAppearance.MouseOverBackColor = primary ? Color.FromArgb(155, 131, 255) : AccentDark;
            b.FlatAppearance.MouseDownBackColor = AccentDark;
            return b;
        }
    }

    // Suppress GDI's font-dependent text inset so the three brand lines share a visual edge.
    sealed class BrandLabel : Label
    {
        public BrandLabel(string text, float size, Color color) {
            Text=text;Font=new Font("Segoe UI",size);ForeColor=color;BackColor=Color.Transparent;
            TextAlign=ContentAlignment.MiddleLeft;Padding=new Padding(0);
        }
        protected override void OnPaint(PaintEventArgs e) {
            TextRenderer.DrawText(e.Graphics,Text,Font,ClientRectangle,ForeColor,
                TextFormatFlags.NoPadding|TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine);
        }
    }

    // Keep the standard native editor and spinner, with breathing room inside the edit text.
    sealed class SpacedNumericUpDown : NumericUpDown
    {
        [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr window,int message,IntPtr wParam,IntPtr lParam);
        public SpacedNumericUpDown() {
            foreach(Control child in Controls)if(child is TextBox)child.HandleCreated+=delegate { ApplyTextMargin(); };
        }
        void ApplyTextMargin() {
            int pixels=Math.Max(1,(int)Math.Round(6*DeviceDpi/96.0*Font.SizeInPoints/10.0));
            foreach(Control child in Controls)if(child is TextBox&&child.IsHandleCreated)
                SendMessage(child.Handle,0xD3,new IntPtr(2),new IntPtr(pixels<<16)); // EM_SETMARGINS / EC_RIGHTMARGIN
        }
        protected override void OnLayout(LayoutEventArgs e) { base.OnLayout(e);ApplyTextMargin(); }
        protected override void OnFontChanged(EventArgs e) { base.OnFontChanged(e);ApplyTextMargin(); }
        protected override void OnDpiChangedAfterParent(EventArgs e) { base.OnDpiChangedAfterParent(e);ApplyTextMargin(); }
    }

    sealed class Surface : Panel
    {
        public Surface() { BackColor = Theme.Card; Padding = new Padding(18); DoubleBuffered = true; }
        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            using (var p = new Pen(Theme.Line)) e.Graphics.DrawRectangle(p, 0, 0, Width - 1, Height - 1);
        }
    }

    sealed class BenchmarkSurface : Panel
    {
        public BenchmarkSurface() {BackColor=Color.FromArgb(26,27,31);DoubleBuffered=true;}
        protected override void OnPaint(PaintEventArgs e) {
            base.OnPaint(e);e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;
            float r=12F*DeviceDpi/96F,w=Width-1,h=Height-1;
            if(w<r||h<r)return;
            using(var path=new GraphicsPath())using(var pen=new Pen(Color.FromArgb(50,52,60))) {
                path.AddArc(0,0,r,r,180,90);path.AddArc(w-r,0,r,r,270,90);path.AddArc(w-r,h-r,r,r,0,90);path.AddArc(0,h-r,r,r,90,90);path.CloseFigure();e.Graphics.DrawPath(pen,path);
            }
        }
    }

    // A real scrollable viewport without the OS's bright non-client scroll bars.
    sealed class DarkScrollBar : Control
    {
        int position, maximum, viewport;
        int dragY, dragValue;
        bool dragging;
        public event EventHandler ValueChanged;
        public int Maximum { get { return maximum; } set { maximum = Math.Max(0, value); Value = position; Invalidate(); } }
        public int Viewport { get { return viewport; } set { viewport = Math.Max(1, value); Invalidate(); } }
        public int Value { get { return position; } set { int next = Math.Max(0, Math.Min(maximum, value)); if (next == position) return;
            position = next; Invalidate(); if (ValueChanged != null) ValueChanged(this, EventArgs.Empty); } }
        int ThumbHeight { get { return Math.Min(Height, Math.Max(Font.Height * 2, Height * viewport / Math.Max(1, maximum + viewport))); } }
        int ThumbTop { get { return maximum == 0 ? 0 : (int)((long)(Height - ThumbHeight) * position / maximum); } }
        public DarkScrollBar()
        {
            Name = "PageScrollBar"; Width = 12; Dock = DockStyle.Right; TabStop = true; BackColor = Theme.Background;
            AccessibleRole = AccessibleRole.ScrollBar; AccessibleName = "Page scroll"; Cursor = Cursors.Hand;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
        }
        public void ScrollWheel(int delta) { Value -= delta * Math.Max(1, SystemInformation.MouseWheelScrollLines) * Font.Height / 120; }
        protected override bool IsInputKey(Keys keyData)
        {
            return keyData == Keys.Up || keyData == Keys.Down || keyData == Keys.PageUp || keyData == Keys.PageDown ||
                keyData == Keys.Home || keyData == Keys.End || base.IsInputKey(keyData);
        }
        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Up) Value -= Font.Height; else if (e.KeyCode == Keys.Down) Value += Font.Height;
            else if (e.KeyCode == Keys.PageUp) Value -= viewport; else if (e.KeyCode == Keys.PageDown) Value += viewport;
            else if (e.KeyCode == Keys.Home) Value = 0; else if (e.KeyCode == Keys.End) Value = maximum;
        }
        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e); if (e.Button != MouseButtons.Left) return; Focus();
            if (e.Y >= ThumbTop && e.Y < ThumbTop + ThumbHeight) { dragging = true; dragY = e.Y; dragValue = position; Capture = true; }
            else Value += e.Y < ThumbTop ? -viewport : viewport;
        }
        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e); if (dragging) Value = dragValue + (int)((long)(e.Y - dragY) * maximum / Math.Max(1, Height - ThumbHeight));
        }
        protected override void OnMouseUp(MouseEventArgs e) { dragging = false; Capture = false; base.OnMouseUp(e); }
        protected override void OnMouseCaptureChanged(EventArgs e) { if (!Capture) dragging = false; base.OnMouseCaptureChanged(e); }
        protected override void OnMouseWheel(MouseEventArgs e) { ScrollWheel(e.Delta); base.OnMouseWheel(e); }
        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e); if (maximum == 0) return;
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            float thickness = Math.Max(3, Width * 0.38F);
            using (var track = new Pen(Theme.Line, Math.Max(1, thickness / 3))) e.Graphics.DrawLine(track, Width / 2F, 0, Width / 2F, Height);
            using (var thumb = new Pen(Focused || dragging ? Theme.Accent : Theme.Muted, thickness)) {
                thumb.StartCap = thumb.EndCap = LineCap.Round;
                e.Graphics.DrawLine(thumb, Width / 2F, ThumbTop + thickness / 2, Width / 2F, ThumbTop + Math.Max(thickness / 2, ThumbHeight - thickness / 2));
            }
        }
    }

    sealed class DarkScrollPage : Panel
    {
        readonly Panel viewport = new Panel { Dock = DockStyle.Fill, AutoScroll = false, Margin = new Padding(0), BackColor = Theme.Background };
        readonly DarkScrollBar bar = new DarkScrollBar();
        Control content;
        bool arranging;
        public DarkScrollPage()
        {
            Dock = DockStyle.Fill; Margin = new Padding(0); BackColor = Theme.Background; AutoScroll = false;
            Controls.Add(viewport); Controls.Add(bar); bar.Visible = false;
            viewport.SizeChanged += delegate { ArrangeContent(); };
            bar.ValueChanged += delegate { if (content != null) content.Top = -bar.Value; };
            MouseWheel += Wheel; viewport.MouseWheel += Wheel;
        }
        public void SetContent(Control value)
        {
            content = value; value.AutoSize = false; value.Dock = DockStyle.None; value.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            viewport.Controls.Add(value); Watch(value); value.SizeChanged += delegate { ArrangeContent(); }; ArrangeContent();
        }
        void Watch(Control control)
        {
            control.MouseWheel += Wheel;
            control.Enter += delegate { Reveal(control); };
            control.ControlAdded += delegate(object sender, ControlEventArgs e) { Watch(e.Control); ArrangeContent(); };
            foreach (Control child in control.Controls) Watch(child);
        }
        void Wheel(object sender, MouseEventArgs e)
        {
            if (bar.Maximum == 0) return; bar.ScrollWheel(e.Delta);
            var handled = e as HandledMouseEventArgs; if (handled != null) handled.Handled = true;
        }
        void Reveal(Control control)
        {
            if (!Visible || content == null || !control.IsHandleCreated || bar.Maximum == 0) return;
            var point = viewport.PointToClient(control.PointToScreen(Point.Empty));
            if (point.Y < 0) bar.Value += point.Y - Font.Height;
            else if (point.Y + control.Height > viewport.ClientSize.Height) bar.Value += point.Y + control.Height - viewport.ClientSize.Height + Font.Height;
        }
        protected override void OnLayout(LayoutEventArgs e) { base.OnLayout(e); ArrangeContent(); }
        void ArrangeContent()
        {
            if (arranging || content == null || ClientSize.Width <= 0 || ClientSize.Height <= 0) return;
            arranging = true;
            try {
                int width = ClientSize.Width;
                int height = content.GetPreferredSize(new Size(width, 0)).Height;
                bar.Visible = height > ClientSize.Height;
                width = Math.Max(1, ClientSize.Width - (bar.Visible ? bar.Width : 0));
                height = content.GetPreferredSize(new Size(width, 0)).Height;
                content.SetBounds(0, -bar.Value, width, height);
                bar.Viewport = ClientSize.Height; bar.Maximum = height - ClientSize.Height; content.Top = -bar.Value;
            } finally { arranging = false; }
        }
    }

    sealed class PurpleSlider : Control
    {
        int current;
        public int Minimum { get; set; }
        public int Maximum { get; set; }
        public int Value
        {
            get { return current; }
            set { int next = Math.Max(Minimum, Math.Min(Maximum, value)); if (current == next) return;
                current = next; Invalidate(); if (ValueChanged != null) ValueChanged(this, EventArgs.Empty); }
        }
        public event EventHandler ValueChanged;
        public PurpleSlider()
        {
            Minimum = 1; Maximum = 400; Height = 32; TabStop = true; Cursor = Cursors.Hand;
            AccessibleRole = AccessibleRole.Slider;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                ControlStyles.UserPaint | ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
        }
        float DpiScale { get { return DeviceDpi / 96F; } }
        void SetFromMouse(int x) { Value = Minimum + (int)Math.Round((Maximum - Minimum) * Math.Max(0, Math.Min(1, (x - 9.0 * DpiScale) / Math.Max(1, Width - 18 * DpiScale)))); }
        protected override void OnMouseDown(MouseEventArgs e) { base.OnMouseDown(e); if (e.Button != MouseButtons.Left) return; Focus(); Capture = true; SetFromMouse(e.X); }
        protected override void OnMouseMove(MouseEventArgs e) { base.OnMouseMove(e); if (Capture && e.Button == MouseButtons.Left) SetFromMouse(e.X); }
        protected override void OnMouseUp(MouseEventArgs e) { Capture = false; base.OnMouseUp(e); }
        protected override bool IsInputKey(Keys keyData) { return keyData == Keys.Left || keyData == Keys.Right || keyData == Keys.Home || keyData == Keys.End || base.IsInputKey(keyData); }
        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Left) Value--; else if (e.KeyCode == Keys.Right) Value++;
            else if (e.KeyCode == Keys.Home) Value = Minimum; else if (e.KeyCode == Keys.End) Value = Maximum;
        }
        protected override void OnEnabledChanged(EventArgs e) { base.OnEnabledChanged(e); Invalidate(); }
        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics; g.SmoothingMode = SmoothingMode.AntiAlias;
            float scale = DpiScale, inset = 9 * scale, radius = 6 * scale;
            int y = Height / 2; float end = inset + (Width - 2 * inset) * (Value - Minimum) / (float)Math.Max(1, Maximum - Minimum);
            using (var track = new Pen(Theme.Line, 4 * scale)) g.DrawLine(track, inset, y, Width - inset, y);
            using (var active = new Pen(Enabled ? Theme.Accent : Theme.Muted, 4 * scale)) g.DrawLine(active, inset, y, end, y);
            using (var dot = new SolidBrush(Enabled ? Theme.Accent : Theme.Muted)) g.FillEllipse(dot, end - radius, y - radius, radius * 2, radius * 2);
            if (Focused) using (var ring = new Pen(Theme.Text)) g.DrawEllipse(ring, end - inset, y - inset, inset * 2, inset * 2);
        }
    }

    sealed class PercentSetting : UserControl
    {
        readonly NumericUpDown number = new SpacedNumericUpDown();
        readonly PurpleSlider slider = new PurpleSlider();
        public decimal Value { get { return number.Value; } set { number.Value = Math.Max(number.Minimum, Math.Min(number.Maximum, value)); } }
        public void SetRange(decimal minimum,decimal maximum) {
            number.Maximum=Math.Max(number.Maximum,maximum);number.Minimum=minimum;number.Maximum=maximum;
            slider.Maximum=(int)Math.Ceiling(maximum);slider.Minimum=(int)Math.Floor(minimum);
        }
        public PercentSetting(string title, int min, int max)
        {
            AutoScaleMode = AutoScaleMode.Inherit;
            Height = 78; Dock = DockStyle.Fill; BackColor = Theme.Card; Margin = new Padding(0, 3, 0, 0);
            var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, RowCount = 2, Margin = new Padding(0) };
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 86));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 22));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 32)); grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            grid.Controls.Add(Theme.Label(title, 10F, Theme.Text), 0, 0);
            number.Minimum = min; number.Maximum = max; number.Value = min; number.DecimalPlaces = 1;
            number.Font = new Font("Segoe UI", 10F); number.BackColor = Theme.Field; number.ForeColor = Theme.Text;
            number.BorderStyle = BorderStyle.FixedSingle; number.Dock = DockStyle.Fill; number.TextAlign = HorizontalAlignment.Right;
            number.AccessibleName = title + " percent"; number.Margin = new Padding(0, 3, 0, 0);
            grid.Controls.Add(number, 1, 0); grid.Controls.Add(Theme.Label(" %", 10F, Theme.Muted), 2, 0);
            slider.Minimum = min; slider.Maximum = max; slider.Value = min; slider.AccessibleName = title;
            slider.Dock = DockStyle.Fill; grid.Controls.Add(slider, 0, 1); grid.SetColumnSpan(slider, 3);
            bool synchronizing = false;
            number.ValueChanged += delegate { if (synchronizing) return; synchronizing = true; slider.Value = (int)Math.Round(number.Value); synchronizing = false; };
            slider.ValueChanged += delegate { if (synchronizing) return; synchronizing = true; Value = slider.Value; synchronizing = false; };
            Controls.Add(grid);
        }
    }

    sealed class Launcher : Form
    {
        readonly string root = AppDomain.CurrentDomain.BaseDirectory;
        readonly ComboBox backend = Choice(new[] { "Native OpenXR (Pimax, etc.)", "Quad-Views-Foveated (mbucchia)", "VectorXR (experimental)", "Stereo VRS (NVIDIA, experimental)" });
        readonly ComboBox vrsRate = Choice(new[] { "1x1 (full shading)", "2x2 peripheral shading", "4x4 peripheral shading" });
        readonly ToolTip help = new ToolTip { AutoPopDelay=30000, InitialDelay=350, ReshowDelay=100, ShowAlways=true };
        bool filling, focusDisplayedAsVrs;
        readonly ComboBox mode = Choice(new[] { "Auto / eye tracking", "Fixed foveation" });
        readonly TextBox game = Field();
        readonly TextBox manifest = Field();
        readonly TextBox vectorManifest = Field();
        readonly CheckBox outline = Toggle("Show green focus outline");
        readonly CheckBox nativeOuter = Toggle("Experimental smaller outer render targets");
        readonly CheckBox renderProfiling = Toggle("Enable render profiling (Ctrl+Alt+F9)");
        readonly CheckBox diagnosticsEnabled=Toggle("Enable diagnostics and logging");
        readonly CheckBox stereoDrawAudit=Toggle("Experimental stereo draw audit (Ctrl+Alt+F8)");
        readonly CheckBox stereoShaderCapture=Toggle("Experimental shader program capture");
        readonly CheckBox stereoPairProof=Toggle("Experimental paired rendering proof");
        readonly CheckBox cpuUploadReuse=Toggle("Experimental CPU upload reuse");
        readonly CheckBox cpuUploadBatch=Toggle("Experimental CPU upload fast path");
        readonly CheckBox cpuOuterReuse=Toggle("Experimental CPU outer upload reuse");
        readonly CheckBox outerSsr=Toggle("Experimental smaller outer SSR resources");
        readonly NumericUpDown profileDuration=new SpacedNumericUpDown {Minimum=30,Maximum=300,Increment=30,Value=120,
            BackColor=Theme.Field,ForeColor=Theme.Text,BorderStyle=BorderStyle.FixedSingle,Dock=DockStyle.Fill,
            TextAlign=HorizontalAlignment.Right,Name="RenderProfileSeconds",AccessibleName="RenderProfileSeconds",Enabled=false};
        readonly PercentSetting outer = new PercentSetting("Outer resolution", 10, 400);
        readonly PercentSetting inner = new PercentSetting("Inner resolution", 25, 400);
        readonly CheckBox dynamicFocus=Toggle("Enable dynamic center FOV (experimental)");
        readonly PercentSetting dynamicMin=new PercentSetting("Minimum (% of baseline)",25,200) {Name="DynamicFocusMinPercent",Value=70};
        readonly PercentSetting dynamicMax=new PercentSetting("Maximum (% of baseline)",25,200) {Name="DynamicFocusMaxPercent",Value=100};
        readonly PercentSetting dynamicReserve=new PercentSetting("GPU reserve (%)",1,30) {Name="DynamicFocusReservePercent",Value=2};
        readonly PercentSetting cropWidth = new PercentSetting("Width", 25, 100) { Name="CropWidthPercent", Value=100 };
        readonly PercentSetting cropHeight = new PercentSetting("Height", 25, 100) { Name="CropHeightPercent", Value=100 };
        readonly PercentSetting focusWidth = new PercentSetting("Width", 10, 90);
        readonly PercentSetting focusHeight = new PercentSetting("Height", 10, 90);
        readonly PercentSetting focusOffsetX = new PercentSetting("Horizontal", -50, 50) { Name="FocusOffsetXPercent" };
        readonly PercentSetting focusOffsetY = new PercentSetting("Vertical", -50, 50) { Name="FocusOffsetYPercent" };
        readonly Button resetOffsets = Theme.Button("Reset offsets", false);
        readonly NumericUpDown outerBlur = new SpacedNumericUpDown { Minimum = 0, Maximum = 3, DecimalPlaces = 2,
            Increment = 0.25M, BackColor = Theme.Field, ForeColor = Theme.Text, BorderStyle = BorderStyle.FixedSingle,
            Dock = DockStyle.Fill, TextAlign = HorizontalAlignment.Right, AccessibleName = "Outer smoothing in pixels" };
        readonly Label status = Theme.Label("Ready. Close LMU before starting a new session.", 9F, Theme.Muted);
        readonly Label header = Theme.Label("Rendering", 25F, Theme.Text);
        readonly Label subheader = Theme.Label("Four views. One sharper racing experience.", 10F, Theme.Muted);
        readonly Label providerHint = Theme.Label("", 9F, Theme.Muted);
        readonly Label resolutionHint = Theme.Label("", 9F, Theme.Muted);
        readonly Label focusHint = Theme.Label("", 9F, Theme.Muted);
        readonly Button refreshFov = Theme.Button("Read latest headset FOV", false);
        FocusGeometry focusGeometry;
        readonly Button launch = Theme.Button("Launch LMU", true);
        readonly Button save = Theme.Button("Save settings", false);
        readonly Button renderingNav = Theme.Button("  Rendering", false);
        readonly Button setupNav = Theme.Button("  Setup", false);
        readonly Button benchmarkNav = Theme.Button("  Benchmark", false);
        readonly DarkScrollPage benchmarkPage = new DarkScrollPage { Name = "BenchmarkPage" };
        readonly CheckBox benchmarkEnabled = Toggle("Enable benchmark on next launch");
        readonly ComboBox benchmarkTemplate = Choice(new[] { "Minimal", "Detailed" });
        readonly Button benchmarkVisible = Theme.Button("Hide overlay", false);
        readonly Button benchmarkRecord = Theme.Button("Start recording", true);
        readonly Button benchmarkReport = Theme.Button("Open last report", false);
        readonly Label benchmarkStatus = Caption("");
        readonly Label benchmarkNotice = Caption("");
        readonly Label visibilityBindingText = Caption("Unassigned");
        readonly Label recordingBindingText = Caption("Unassigned");
        readonly Label bindingStatus = Caption("Assign separate joystick buttons for visibility and manual recording.");
        readonly PictureBox benchmarkPreview = new PictureBox { Name="BenchmarkPreview", Dock=DockStyle.Fill, SizeMode=PictureBoxSizeMode.Zoom, BackColor=Color.FromArgb(15,15,18) };
        readonly NumericUpDown benchmarkX = BenchmarkNumber("BenchmarkX", -2, 2, 0);
        readonly NumericUpDown benchmarkY = BenchmarkNumber("BenchmarkY", -2, 2, -0.25m);
        readonly NumericUpDown benchmarkDistance = BenchmarkNumber("BenchmarkDistance", 0.3m, 5, 1.2m);
        readonly NumericUpDown benchmarkWidth = BenchmarkNumber("BenchmarkWidth", 0.2m, 2, 0.65m);
        readonly Timer benchmarkTimer = new Timer { Interval=50 };
        BenchmarkController benchmark;
        JoystickInput joystick;
        JoystickBinding visibilityBinding, recordingBinding;
        int previewTicks;
        readonly Button browseManifest = Theme.Button("Browse", false);
        readonly Button browseVectorManifest = Theme.Button("Browse", false);
        readonly DarkScrollPage renderingPage = new DarkScrollPage { Name = "RenderingPage" };
        readonly DarkScrollPage setupPage = new DarkScrollPage { Name = "SetupPage" };
        bool busy;
        string SettingsPath { get { return Path.Combine(root, "launcher-settings.xml"); } }

        static ComboBox Choice(string[] choices)
        {
            var c = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, FlatStyle = FlatStyle.Flat,
                BackColor = Theme.Field, ForeColor = Theme.Text, Dock = DockStyle.Fill, Font = new Font("Segoe UI", 10F) };
            c.Items.AddRange(choices); c.SelectedIndex = 0; return c;
        }
        static TextBox Field() { return new TextBox { Dock = DockStyle.Fill, BackColor = Theme.Field, ForeColor = Theme.Text,
            BorderStyle = BorderStyle.FixedSingle, Font = new Font("Segoe UI", 10F), Margin = new Padding(0, 5, 10, 0) }; }
        static CheckBox Toggle(string text) { return new CheckBox { Text = text, AutoSize = false, Dock = DockStyle.Fill,
            ForeColor = Theme.Text, Font = new Font("Segoe UI", 10F), Cursor = Cursors.Hand, Margin = new Padding(0) }; }
        static TableLayoutPanel Stack(int rows)
        {
            var stack = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = rows,
                Margin = new Padding(0), Padding = new Padding(0), BackColor = Color.Transparent };
            stack.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); return stack;
        }
        static Label Caption(string text) { return Theme.Label(text, 9F, Theme.Muted); }
        void Help(Control control,string text) {
            help.SetToolTip(control,text);control.AccessibleDescription=text;
            foreach(Control child in control.Controls)Help(child,text);
        }
        void DefaultHelp(Control parent) {
            foreach(Control control in parent.Controls) {
                if(control is Button||control is LinkLabel)Help(control,control.Text.Trim()+". This launcher action has no ongoing game CPU/GPU effect; launching applies the saved rendering options.");
                DefaultHelp(control);
            }
        }
        void ConfigureHelp() {
            DefaultHelp(this);
            Help(backend,"Choose one rendering path. Quad views render peripheral and focus views; Stereo VRS keeps native stereo and coarsens peripheral pixel shading on supported NVIDIA hardware. GPU effect depends on the workload; VRS does not merge CPU draw submissions. Experimental paths have no guaranteed performance gain.");
            Help(mode,"Auto uses available eye tracking; unavailable or invalid gaze falls back to fixed focus. Fixed keeps the region at its configured offset. Tracking adds gaze update work on the CPU; focus placement can change GPU shading cost. Restart LMU to apply.");
            Help(vrsRate,"Stereo VRS only. 1x1 shades every pixel; 2x2 or 4x4 shares shading over larger peripheral blocks. Focus stays 1x1. Larger blocks can reduce GPU pixel shading but reduce detail; geometry and CPU draw work remain. No measured gain is promised.");
            Help(Controls.Find("VrsRateCaption",true)[0],help.GetToolTip(vrsRate));
            Help(game,"Folder containing Le Mans Ultimate.exe. The supported executable is checked before launch. Path selection has no CPU/GPU rendering effect.");
            Help(manifest,"Optional QVFR manifest path, used only with QVFR. Blank auto-detects its installation. No direct CPU/GPU effect; the selected provider determines rendering behavior.");
            Help(vectorManifest,"Optional VectorXR manifest, used only with VectorXR. Configure its LMU Quadviews profile separately. No direct CPU/GPU effect from this path setting.");
            Help(outer,"QVFR only: linear peripheral resolution scale. 200% doubles both dimensions, producing four times the pixels. Higher values increase GPU pixel and memory work; CPU draw count is not reduced. In Stereo VRS use runtime/game resolution instead.");
            Help(inner,"QVFR only: linear focus resolution scale. Higher values improve central detail and increase GPU pixel/memory work; CPU draw count is not reduced. Native and VectorXR use their own settings; Stereo VRS uses runtime/game resolution.");
            Help(focusWidth,"Focus width. Stereo VRS uses 10-90% of each eye's projected horizontal span; quad display may use measured FOV calibration. A larger focus covers more detail but can increase GPU shading work. CPU draw work is largely unchanged.");
            Help(focusHeight,"Focus height. Stereo VRS uses 10-90% of each eye's projected vertical span; quad display may use measured FOV calibration. A larger focus increases full-quality coverage and can increase GPU work. CPU draw work is largely unchanged.");
            Help(focusOffsetX,"Horizontal shift as a percent of each eye's full projected view: positive is right. Auto adds this offset to gaze; fixed uses the configured center. CPU update cost is small; GPU cost can vary with covered content.");
            Help(focusOffsetY,"Vertical shift as a percent of each eye's full projected view: positive is up. Auto adds this offset to gaze; fixed uses the configured center. CPU update cost is small; GPU cost can vary with covered content.");
            string cropHelp="Highly experimental: please test and report issues on GitHub with your headset, runtime/provider and crop values. Reset to 100/100 to disable. Quad views only. Removes edge coverage without changing image scale. Smaller native outer targets can reduce GPU pixel work; reliable FPS/CPU gains have not been demonstrated. A final copy adds black borders to full-size output textures, which stay allocated. Disabled for Stereo VRS; saved quad values are preserved.";
            Help(cropWidth,cropHelp);Help(cropHeight,cropHelp);Help(Controls.Find("CropHint",true)[0],cropHelp);
            Help(nativeOuter,"Experimental, quad views only. Smaller native outer render targets may reduce GPU pixel and memory work; CPU draw work remains. Off uses full-size outer rendering plus resampling. Disabled in Stereo VRS; compare visual output and timings.");
            Help(outerBlur,"Quad views only. Smooth peripheral shimmer with an extra GPU pass; 0 disables it. CPU dispatch and GPU filtering add work. Focus stays sharp. Disabled for Stereo VRS.");
            Help(outline,"Quad views only: draw a green focus border to inspect size, offset and tracking. Disabled for Stereo VRS; the saved preference is retained. Adds visualization work on the GPU and a small CPU setup cost; turn off for representative performance comparisons.");
            Help(diagnosticsEnabled,"Enable local mod logs and timing support. Logging adds CPU/file I/O overhead; GPU timing queries can affect measurements when captures are enabled. Off disables profiling, benchmark timing and draw audits for the next launch. Provider logging is separate.");
            Help(renderProfiling,"Record a timed render profile after Ctrl+Alt+F9. Requires diagnostics. CPU bookkeeping and GPU timestamp queries add measurement overhead. Available with quad views and Stereo VRS; compare equivalent runs.");
            Help(profileDuration,"Profile capture duration in seconds (30-300). Longer captures collect more samples and files, increasing total CPU/I/O work; they do not change render quality. Profiling queries add overhead while active.");
            Help(stereoDrawAudit,"Experimental quad-only diagnostic. Ctrl+Alt+F8 captures sampled scene draw state, adding CPU overhead and possible hitches; not a speedup. Disabled for Stereo VRS and the upload fast path.");
            Help(stereoShaderCapture,"Experimental quad-only diagnostic requiring draw audit. Exports captured shader programs locally for development. Adds CPU copying and disk I/O; no GPU speedup. Keep local shader files out of public reports.");
            Help(stereoPairProof,"Experimental quad-only proof, not single-pass stereo. Samples one object in private targets while original game draws remain enabled. Adds CPU and GPU work and may hitch; use with profiling off. Disabled for Stereo VRS.");
            Help(cpuUploadReuse,"Experimental quad-only upload reuse for focus opaque geometry. May avoid redundant CPU constant-buffer uploads; GPU geometry draw work remains. Visual correctness and performance depend on the scene. Disabled in Stereo VRS and when upload fast path is selected.");
            Help(cpuUploadBatch,"Experimental quad-only unchanged-upload filtering and small-buffer batching for outer/focus geometry. May reduce CPU upload work; does not merge stereo draws or guarantee GPU savings. Disables draw audit and upload reuse. Compare equivalent on/off runs; disabled in Stereo VRS.");
            Help(cpuOuterReuse,"Experimental, quad views only. Extends unchanged constant-upload filtering to outer and focus opaque group 31 and caches buffer aliases while identities remain unchanged. Implies CPU upload reuse; preparation, bindings, changed uploads and draws still execute. May reduce CPU upload work, with no guaranteed GPU gain. Excludes audit/proof and upload fast path; if saved together, fast path wins. Works with diagnostics off. Compare images and timings.");
            Help(outerSsr,"Experimental, quad views only; requires smaller native outer render targets. Requests smaller outer screen-space reflection resources, never below 40% of either original axis. Unsupported layouts keep native resources. Reflections may look softer; focus resolution is unchanged. May reduce GPU work; no CPU savings or measured gain is promised. Works with diagnostics off. Disabled states preserve the saved preference.");
            Help(benchmarkEnabled,"Opt in to benchmark timing and headset overlay for the next launch. Requires diagnostics. CPU sampling/overlay updates and GPU timing/compositing add overhead. Works with quad views and Stereo VRS; compare equivalent settings.");
            Help(benchmarkTemplate,"Choose Minimal or Detailed overlay presentation. More displayed metrics can add CPU text/bitmap update work; overlay compositing adds GPU work. Timing data is unchanged.");
            Help(benchmarkVisible,"Show or hide the headset overlay during the connected session. Hidden overlay reduces overlay drawing/compositing work; benchmark sampling can remain active. This does not change the next-launch benchmark preference.");
            Help(benchmarkRecord,"Manually start or stop recording benchmark samples. Adds CPU sample processing and report I/O; timing queries have GPU measurement overhead. Use matching track, weather and camera conditions for comparisons.");
            foreach(var number in new[]{benchmarkX,benchmarkY,benchmarkDistance,benchmarkWidth})Help(number,"Headset overlay placement in metres: horizontal/vertical offset, viewing distance or width. Updates the connected overlay. CPU update cost is small; changing visible overlay coverage can affect GPU compositing. Does not change game rendering resolution.");
            Help(refreshFov,"Read the latest measured headset FOV to calibrate equal-axis quad focus controls. CPU file read only; no direct GPU effect. Stereo VRS always uses raw projected per-eye percentages, so this control is disabled there.");
            Help(save,"Save settings for the next launch. QVFR also updates only LMU's provider profile with backups. Stereo VRS does not edit provider profiles. Saving has CPU/file I/O cost but no immediate GPU rendering change.");
            Help(launch,"Validate the supported game build and launch LMU with the selected settings. Quad providers/Toolkit conflicts are isolated only in the child process. Rendering CPU/GPU effects depend on your chosen options.");
            foreach(var name in new[]{"AssignVisibility","AssignRecording","ClearVisibility","ClearRecording"})Help(Controls.Find(name,true)[0],"Assign or clear a joystick button for overlay visibility or manual recording. Use separate buttons; Escape cancels assignment. CPU button polling adds a small amount of work; the binding itself has no GPU effect. Save to keep the assignment.");
            Help(((TableLayoutPanel)outer.Parent).GetControlFromPosition(0,0),help.GetToolTip(outer)+" "+help.GetToolTip(inner));
            foreach(var option in new[]{nativeOuter,cpuUploadReuse,diagnosticsEnabled,stereoDrawAudit,stereoShaderCapture,stereoPairProof,cpuUploadBatch,cpuOuterReuse,outerSsr}) {
                var table=option.Parent as TableLayoutPanel;
                if(table!=null) {var caption=table.GetControlFromPosition(0,table.GetRow(option)+1);if(caption!=null)Help(caption,help.GetToolTip(option));}
            }
        }
        static void Link(Control parent, string text, string address, int row)
        {
            var link = new LinkLabel { Text = text, Dock = DockStyle.Fill, LinkColor = Theme.Muted,
                ActiveLinkColor = Theme.Text, VisitedLinkColor = Theme.Muted, Font = new Font("Segoe UI", 9F),
                TextAlign = ContentAlignment.MiddleLeft, Margin = new Padding(0), LinkBehavior = LinkBehavior.HoverUnderline };
            link.LinkClicked += delegate { try { Process.Start(new ProcessStartInfo(address) { UseShellExecute = true }); }
                catch (Exception ex) { MessageBox.Show(parent.FindForm(), ex.Message, "Could not open link"); } };
            ((TableLayoutPanel)parent).Controls.Add(link, 0, row);
        }
        public Launcher()
        {
            SuspendLayout();
            benchmark=new BenchmarkController(root);
            Text = "LMU Quad View - " + Application.ProductVersion; BackColor = Theme.Background; ForeColor = Theme.Text;
            Font = new Font("Segoe UI", 10F); AutoScaleDimensions = new SizeF(96F, 96F); AutoScaleMode = AutoScaleMode.Dpi;
            try {
                using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("LmuQuadView.Icon")) {
                    if (stream != null) using (var icon = new Icon(stream)) Icon = (Icon)icon.Clone();
                    else Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
                }
            } catch (ArgumentException) { }
            renderingNav.Name = "RenderingNav"; setupNav.Name = "SetupNav"; benchmarkNav.Name="BenchmarkNav";
            save.Name = "SaveButton"; launch.Name = "LaunchButton";
            backend.Name = "ProviderSelector";
            vrsRate.Name="VrsPeripheralRate";vrsRate.SelectedIndex=1;
            manifest.Name="QvfrManifest";vectorManifest.Name="VectorXrManifest";
            outer.Name="OuterResolution";inner.Name="InnerResolution";
            ClientSize = new Size(1050, 780); MinimumSize = new Size(900, 700); StartPosition = FormStartPosition.CenterScreen;
            var frame = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Margin = new Padding(0) };
            frame.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            frame.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 204)); frame.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            Controls.Add(frame); frame.Controls.Add(BuildSidebar(), 0, 0);
            var main = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3,
                Padding = new Padding(28, 14, 28, 14), Margin = new Padding(0) };
            main.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            main.RowStyles.Add(new RowStyle(SizeType.Absolute, 91)); main.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); main.RowStyles.Add(new RowStyle(SizeType.Absolute, 106));
            frame.Controls.Add(main, 1, 0);
            var heading = Stack(2); heading.RowStyles.Add(new RowStyle(SizeType.Absolute, 50)); heading.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
            heading.Controls.Add(header, 0, 0); heading.Controls.Add(subheader, 0, 1); main.Controls.Add(heading, 0, 0);
            var pages = new Panel { Dock = DockStyle.Fill, Margin = new Padding(0) };
            pages.Controls.Add(benchmarkPage); pages.Controls.Add(setupPage); pages.Controls.Add(renderingPage); main.Controls.Add(pages, 0, 1);
            BuildRendering(); BuildSetup(); BuildBenchmark(); main.Controls.Add(BuildFooter(), 0, 2);ConfigureHelp();
            backend.SelectedIndexChanged += delegate { UpdateProvider(); };
            diagnosticsEnabled.Name="DiagnosticsEnabled";
            stereoDrawAudit.Name="StereoDrawAudit";
            stereoShaderCapture.Name="StereoShaderCapture";
            stereoPairProof.Name="StereoPairProof";cpuUploadReuse.Name="CpuUploadReuse";cpuUploadBatch.Name="CpuUploadBatch";
            cpuOuterReuse.Name="CpuOuterReuse";outerSsr.Name="OuterSsr";
            cpuUploadBatch.CheckedChanged+=delegate {if(cpuUploadBatch.Checked&&!filling){cpuUploadReuse.Checked=false;stereoDrawAudit.Checked=false;}UpdateAdvanced();};
            cpuOuterReuse.CheckedChanged+=delegate {UpdateAdvanced();};
            nativeOuter.CheckedChanged+=delegate {UpdateAdvanced();};
            diagnosticsEnabled.CheckedChanged+=delegate {UpdateBenchmarkNotice();UpdateAdvanced();};
            stereoDrawAudit.CheckedChanged+=delegate {UpdateAdvanced();};
            renderProfiling.CheckedChanged+=delegate {UpdateAdvanced();};
            renderingNav.Click += delegate { ShowPage(false); }; setupNav.Click += delegate { ShowPage(true); }; benchmarkNav.Click += delegate { ShowBenchmarkPage(); };
            save.Click += delegate { SaveSettings(); }; launch.Click += delegate { StartGame(); };
            AcceptButton = launch;
            try {
                var settings=QvfrSettings.Load(SettingsPath);
                var measurement=FocusGeometry.LoadCalibration(root,settings.FocusAspectRatio);
                settings.FocusAspectRatio=measurement==null?0:measurement.Aspect;
                Fill(settings);
            }
            catch (Exception ex) { status.Text = "Settings could not be loaded: " + ex.Message; }
            UpdateProvider(); ShowPage(false);
            ResumeLayout(true);
            KeyPreview=true;
            KeyDown+=delegate(object sender,KeyEventArgs e) { if(e.KeyCode==Keys.Escape&&joystick!=null&&joystick.Listening) {joystick.CancelListen();bindingStatus.Text="Button assignment cancelled.";e.Handled=true;} };
            Shown+=delegate { if(joystick==null) {joystick=new JoystickInput(new DirectInputButtonSource(Handle));UpdateBindings();joystick.VisibilityPressed+=delegate {benchmark.ToggleOverlay();};joystick.RecordingPressed+=delegate {benchmark.ToggleRecording();};benchmarkTimer.Start();} };
            benchmarkTimer.Tick+=delegate { PollBenchmark(); };
        }
        Control BuildSidebar()
        {
            var side = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 8,
                BackColor = Theme.Sidebar, Padding = new Padding(16, 26, 16, 14), Margin = new Padding(0) };
            side.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            side.RowStyles.Add(new RowStyle(SizeType.AutoSize)); side.RowStyles.Add(new RowStyle(SizeType.Absolute, 49));
            side.RowStyles.Add(new RowStyle(SizeType.Absolute, 49)); side.RowStyles.Add(new RowStyle(SizeType.Absolute,49)); side.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            side.RowStyles.Add(new RowStyle(SizeType.Absolute, 26)); side.RowStyles.Add(new RowStyle(SizeType.Absolute, 89)); side.RowStyles.Add(new RowStyle(SizeType.Absolute, 65));
            var brand = Stack(3); brand.AutoSize = true; brand.Dock = DockStyle.Top; brand.Margin = new Padding(0, 0, 0, 24);
            brand.RowStyles.Add(new RowStyle(SizeType.AutoSize)); brand.RowStyles.Add(new RowStyle(SizeType.AutoSize)); brand.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            var lmu = new BrandLabel("LMU", 25F, Theme.Text); lmu.Name = "BrandTitle"; lmu.Font = new Font("Segoe UI", 25F, FontStyle.Bold);
            brand.Controls.Add(lmu, 0, 0); brand.Controls.Add(new BrandLabel("QUAD VIEW", 12F, Theme.Accent) {Name="BrandSubtitle"}, 0, 1); brand.Controls.Add(new BrandLabel("OPENXR RENDERING", 9F, Theme.Muted) {Name="BrandRenderer"}, 0, 2); side.Controls.Add(brand, 0, 0);
            foreach (Control label in brand.Controls) { ((Label)label).AutoSize = true; label.Dock = DockStyle.Top; label.Margin = new Padding(0, 0, 0, 5); }
            foreach (var nav in new[] { renderingNav, setupNav, benchmarkNav }) { nav.Dock = DockStyle.Fill; nav.TextAlign = ContentAlignment.MiddleLeft; nav.Margin = new Padding(0, 3, 0, 5); nav.FlatAppearance.BorderSize = 0; }
            side.Controls.Add(renderingNav, 0, 1); side.Controls.Add(setupNav, 0, 2); side.Controls.Add(benchmarkNav,0,3);
            side.Controls.Add(Caption("OPTIONAL SUPPORT"), 0, 5);
            var support = Stack(2); support.RowStyles.Add(new RowStyle(SizeType.Absolute, 41)); support.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));
            var coffee = Theme.Button("Buy me a coffee", false); coffee.Name = "CoffeeButton"; coffee.Dock = DockStyle.Fill; coffee.Font = new Font("Segoe UI", 9F); coffee.Margin = new Padding(0, 0, 0, 4);
            coffee.Click += delegate { OpenLink("https://buymeacoffee.com/dansuzuki"); }; support.Controls.Add(coffee, 0, 0);
            Link(support, "Shop via affiliate links", "https://dansuzuki.com/discounts/", 1); side.Controls.Add(support, 0, 6);
            var tag = Theme.Label("Always free.\nDonations are completely\noptional.", 8F, Theme.Muted); tag.Name="FreeNotice"; side.Controls.Add(tag, 0, 7); return side;
        }
        void BuildRendering()
        {
            var grid = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 2, RowCount = 7, Margin = new Padding(0) };
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50)); grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 260)); grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 280));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 250)); grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 270)); grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 128)); grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 54)); grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 330)); renderingPage.SetContent(grid);
            var provider = new Surface { Dock = DockStyle.Fill, Margin = new Padding(0, 0, 0, 14) };
            var p = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 7, Margin = new Padding(0) };
            p.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            p.RowStyles.Add(new RowStyle(SizeType.Absolute, 25)); p.RowStyles.Add(new RowStyle(SizeType.Absolute, 32));
            p.RowStyles.Add(new RowStyle(SizeType.Absolute, 25)); p.RowStyles.Add(new RowStyle(SizeType.Absolute, 32));
            p.RowStyles.Add(new RowStyle(SizeType.Absolute,25));p.RowStyles.Add(new RowStyle(SizeType.Absolute,32));p.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            p.Controls.Add(Caption("RENDERING PROVIDER"), 0, 0); p.Controls.Add(Caption("FOVEATION MODE"), 0, 2);
            backend.Margin = mode.Margin = new Padding(0); p.Controls.Add(backend, 0, 1); p.Controls.Add(mode, 0, 3);
            var rateCaption=Caption("VRS PERIPHERAL SHADING  (hover for help)");rateCaption.Name="VrsRateCaption";p.Controls.Add(rateCaption,0,4);p.Controls.Add(vrsRate,0,5);
            p.Controls.Add(providerHint, 0, 6); provider.Controls.Add(p); grid.Controls.Add(provider, 0, 0); grid.SetColumnSpan(provider, 2);
            grid.Controls.Add(SettingCard("Resolution", outer, inner, "Linear scale per dimension.", new Padding(0, 0, 7, 14)), 0, 1);
            var focusCard=SettingCard("Focus region", focusWidth, focusHeight, "", new Padding(7, 0, 0, 14));
            var focusContent=(TableLayoutPanel)focusCard.Controls[0];
            focusContent.Controls.Remove(focusContent.GetControlFromPosition(0,3));
            var focusFooter=Stack(2);focusFooter.RowStyles.Add(new RowStyle(SizeType.Percent,100));focusFooter.RowStyles.Add(new RowStyle(SizeType.Absolute,30));
            focusFooter.Controls.Add(focusHint,0,0);refreshFov.Dock=DockStyle.Fill;refreshFov.Margin=new Padding(0);refreshFov.Font=new Font("Segoe UI",9F);
            refreshFov.Click+=delegate { RefreshFocusGeometry(); };focusFooter.Controls.Add(refreshFov,0,1);focusContent.Controls.Add(focusFooter,0,3);
            grid.Controls.Add(focusCard,1,1);
            var position = new Surface { Name="FocusPositionCard", Dock=DockStyle.Fill, Margin=new Padding(0,0,0,14), Padding=new Padding(17,12,17,10) };
            var positionContent = new TableLayoutPanel { Dock=DockStyle.Fill, ColumnCount=2, RowCount=3, Margin=new Padding(0) };
            positionContent.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));positionContent.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));
            positionContent.RowStyles.Add(new RowStyle(SizeType.Absolute,34));positionContent.RowStyles.Add(new RowStyle(SizeType.Absolute,84));positionContent.RowStyles.Add(new RowStyle(SizeType.Percent,100));
            var positionTitle=Theme.Label("Focus position",13F,Theme.Text);positionTitle.Font=new Font("Segoe UI",13F,FontStyle.Bold);
            var positionHeader=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=2,RowCount=1,Margin=new Padding(0)};
            positionHeader.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));positionHeader.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,150));
            positionHeader.Controls.Add(positionTitle,0,0);positionContent.Controls.Add(positionHeader,0,0);positionContent.SetColumnSpan(positionHeader,2);
            resetOffsets.Name="ResetFocusOffsets";resetOffsets.Dock=DockStyle.Fill;resetOffsets.Height=30;resetOffsets.Font=new Font("Segoe UI",9F);resetOffsets.Margin=new Padding(0);
            resetOffsets.Click+=delegate { focusOffsetX.Value=focusOffsetY.Value=0; };positionHeader.Controls.Add(resetOffsets,1,0);
            focusOffsetX.Margin=new Padding(0,3,12,0);focusOffsetY.Margin=new Padding(12,3,0,0);
            positionContent.Controls.Add(focusOffsetX,0,1);positionContent.Controls.Add(focusOffsetY,1,1);
            var positionHint=Caption("Percent of each eye's full view. Positive: right / up. In Auto, offsets are relative to gaze. 0 = no shift. Applies next game launch.");
            positionContent.Controls.Add(positionHint,0,2);positionContent.SetColumnSpan(positionHint,2);position.Controls.Add(positionContent);grid.Controls.Add(position,0,2);grid.SetColumnSpan(position,2);
            var crop = new Surface { Name="CropCard", Dock=DockStyle.Fill, Margin=new Padding(0,0,0,14), Padding=new Padding(17,12,17,10) };
            var cropContent = new TableLayoutPanel { Dock=DockStyle.Fill, ColumnCount=2, RowCount=3, Margin=new Padding(0) };
            cropContent.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));cropContent.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));
            cropContent.RowStyles.Add(new RowStyle(SizeType.Absolute,38));cropContent.RowStyles.Add(new RowStyle(SizeType.Absolute,84));cropContent.RowStyles.Add(new RowStyle(SizeType.Percent,100));
            var cropHeader=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=2,RowCount=1,Margin=new Padding(0)};
            cropHeader.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));cropHeader.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,150));
            var cropTitle=Theme.Label("Rendered FOV / crop",13F,Theme.Text);cropTitle.Name="CropTitle";cropTitle.Font=new Font("Segoe UI",13F,FontStyle.Bold);
            cropHeader.Controls.Add(cropTitle,0,0);
            var resetCrop=Theme.Button("Reset to full",false);resetCrop.Name="ResetCrop";resetCrop.Height=30;resetCrop.Dock=DockStyle.Fill;resetCrop.Font=new Font("Segoe UI",9F);resetCrop.Margin=new Padding(0);
            resetCrop.Click+=delegate {cropWidth.Value=cropHeight.Value=100;};cropHeader.Controls.Add(resetCrop,1,0);
            cropContent.Controls.Add(cropHeader,0,0);cropContent.SetColumnSpan(cropHeader,2);
            cropWidth.Margin=new Padding(0,3,12,0);cropHeight.Margin=new Padding(12,3,0,0);
            cropContent.Controls.Add(cropWidth,0,1);cropContent.Controls.Add(cropHeight,1,1);
            var cropHint=Caption("Highly experimental - please test and report issues on GitHub. Quad views only: 100/100 disables crop. Lower values trim edges without zoom; focus may be constrained. Performance gains are not guaranteed. Disabled for Stereo VRS. Restart LMU to apply.");cropHint.Name="CropHint";
            cropContent.Controls.Add(cropHint,0,2);cropContent.SetColumnSpan(cropHint,2);crop.Controls.Add(cropContent);grid.Controls.Add(crop,0,3);grid.SetColumnSpan(crop,2);
            var preview = new Surface { Dock = DockStyle.Fill, Margin = new Padding(0, 0, 0, 12), Padding = new Padding(18, 10, 18, 10) };
            var info = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 2, Margin = new Padding(0) };
            info.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50)); info.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            info.RowStyles.Add(new RowStyle(SizeType.Absolute, 35)); info.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            outline.Margin = new Padding(0, 0, 12, 0); info.Controls.Add(outline, 0, 0);
            var outlineHint = Caption("Quad views only: highlights the focus region. Disabled for Stereo VRS."); outlineHint.Margin = new Padding(0, 0, 16, 0); info.Controls.Add(outlineHint, 0, 1);
            var blurRow = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Margin = new Padding(0) };
            blurRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); blurRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 86));
            blurRow.Controls.Add(Theme.Label("Outer smoothing (pixels)", 10F, Theme.Text), 0, 0);
            outerBlur.Margin = new Padding(2, 4, 0, 0); blurRow.Controls.Add(outerBlur, 1, 0); info.Controls.Add(blurRow, 1, 0);
            info.Controls.Add(Caption("Softens peripheral shimmer; adds a small GPU pass. Focus stays sharp. 0 = off."), 1, 1);
            preview.Controls.Add(info); grid.Controls.Add(preview, 0, 4); grid.SetColumnSpan(preview, 2);
            grid.Controls.Add(resolutionHint, 0, 5); grid.SetColumnSpan(resolutionHint, 2);
            var dynamicCard=new Surface {Name="DynamicFocusCard",Dock=DockStyle.Fill,Margin=new Padding(0,0,0,14),Padding=new Padding(17,12,17,10)};
            var dynamicContent=Stack(5);
            dynamicContent.RowStyles.Add(new RowStyle(SizeType.Absolute,34));
            dynamicContent.RowStyles.Add(new RowStyle(SizeType.Absolute,65));dynamicContent.RowStyles.Add(new RowStyle(SizeType.Absolute,65));
            dynamicContent.RowStyles.Add(new RowStyle(SizeType.Absolute,65));dynamicContent.RowStyles.Add(new RowStyle(SizeType.Percent,100));
            dynamicFocus.Name="DynamicFocusEnabled";
            dynamicContent.Controls.Add(dynamicFocus,0,0);dynamicContent.Controls.Add(dynamicMin,0,1);
            dynamicContent.Controls.Add(dynamicMax,0,2);dynamicContent.Controls.Add(dynamicReserve,0,3);
            dynamicContent.Controls.Add(Caption("Scales both center FOV axes relative to your Focus region, preserving aspect ratio. Resolution stays fixed. GPU app-time estimate excludes compositor work; savings are not guaranteed. Pauses while render profiling is enabled. Enable on next launch; adjusts while driving."),0,4);
            dynamicCard.Controls.Add(dynamicContent);grid.Controls.Add(dynamicCard,0,6);grid.SetColumnSpan(dynamicCard,2);
        }
        Control SettingCard(string title, PercentSetting first, PercentSetting second, string hint, Padding margin)
        {
            var card = new Surface { Dock = DockStyle.Fill, Margin = margin, Padding = new Padding(17, 12, 17, 10) };
            var content = Stack(4); content.RowStyles.Add(new RowStyle(SizeType.Absolute, 29)); content.RowStyles.Add(new RowStyle(SizeType.Absolute, 65));
            content.RowStyles.Add(new RowStyle(SizeType.Absolute, 65)); content.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            var caption = Theme.Label(title, 13F, Theme.Text); caption.Font = new Font("Segoe UI", 13F, FontStyle.Bold);
            content.Controls.Add(caption, 0, 0); content.Controls.Add(first, 0, 1); content.Controls.Add(second, 0, 2); content.Controls.Add(Caption(hint), 0, 3);
            card.Controls.Add(content); return card;
        }
        void BuildSetup()
        {
            var grid = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 1, RowCount = 5, Margin = new Padding(0) };
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 153)); grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 165));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 165));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 900)); grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 50)); setupPage.SetContent(grid);
            var browseGame = Theme.Button("Browse", false); browseGame.Click += delegate {
                using (var d = new FolderBrowserDialog { Description = "Select the folder containing Le Mans Ultimate.exe", ShowNewFolderButton = false }) {
                    if (Directory.Exists(game.Text)) d.SelectedPath = game.Text;
                    if (d.ShowDialog(this) == DialogResult.OK) game.Text = d.SelectedPath;
                } };
            grid.Controls.Add(PathCard("Game installation", game, browseGame, "Choose your LMU folder. The launcher checks the supported game build before starting.", false), 0, 0);
            browseManifest.Click += delegate { using (var d = new OpenFileDialog { Filter = "OpenXR layer manifest (*.json)|*.json", CheckFileExists = true }) { if (d.ShowDialog(this) == DialogResult.OK) manifest.Text = d.FileName; } };
            grid.Controls.Add(PathCard("Quad-Views-Foveated", manifest, browseManifest, "Manifest JSON: leave blank to auto-detect an installed copy.", true), 0, 1);
            browseVectorManifest.Click += delegate { using(var d=new OpenFileDialog {Filter="OpenXR layer manifest (*.json)|*.json",CheckFileExists=true}) {if(d.ShowDialog(this)==DialogResult.OK)vectorManifest.Text=d.FileName;} };
            grid.Controls.Add(PathCard("VectorXR",vectorManifest,browseVectorManifest,
                "Manifest JSON: leave blank to detect VectorXR. Enable an LMU Quadviews profile in its app.",true,
                "https://github.com/DienerTech/vectorxr#installation"),0,2);
            var options = new Surface { Dock = DockStyle.Fill, Margin = new Padding(0, 0, 0, 14) };
            var stack = Stack(22);
            foreach(int height in new[]{23,30,40,30,45,30,40,30,35,46,30,45,30,45,30,45,30,45,30,60,30,60})stack.RowStyles.Add(new RowStyle(SizeType.Absolute,height));
            stack.Controls.Add(Caption("ADVANCED"), 0, 0); stack.Controls.Add(nativeOuter, 0, 1);
            stack.Controls.Add(Caption("Experimental, quad views only. Smaller outer targets may reduce GPU work. Compare with full-size rendering and resampling."), 0, 2);
            stack.Controls.Add(cpuUploadReuse,0,3);
            stack.Controls.Add(Caption("Focus opaque geometry only. Skips uploads of unchanged constants; needs visual and performance testing."),0,4);
            stack.Controls.Add(diagnosticsEnabled,0,5);
            stack.Controls.Add(Caption("Untick for normal play. Stops mod logs and profiling. Applies next launch; VectorXR logging is separate."),0,6);
            stack.Controls.Add(renderProfiling,0,7);
            var durationRow=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=2,RowCount=1,Margin=new Padding(0)};
            durationRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));durationRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,86));
            durationRow.Controls.Add(Theme.Label("Capture length (seconds)",10F,Theme.Text),0,0);profileDuration.Margin=new Padding(2,4,0,0);durationRow.Controls.Add(profileDuration,1,0);stack.Controls.Add(durationRow,0,8);
            stack.Controls.Add(Caption("Press Ctrl+Alt+F9 on track. 30-300 seconds; closing LMU saves a partial capture."),0,9);
            stack.Controls.Add(stereoDrawAudit,0,10);
            stack.Controls.Add(Caption("Diagnostic only: captures two scene frames with overhead. Stereo optimization is still under development."),0,11);
            stack.Controls.Add(stereoShaderCapture,0,12);
            stack.Controls.Add(Caption("Saves programs used by the sampled focus views under logs/stereo-shaders. Local development files; keep out of GitHub."),0,13);
            stack.Controls.Add(stereoPairProof,0,14);
            stack.Controls.Add(Caption("One sampled object in private targets. Leave render profiling off; diagnostic hitch expected. Game draws remain enabled."),0,15);
            stack.Controls.Add(cpuUploadBatch,0,16);
            stack.Controls.Add(Caption("Combines unchanged-upload filtering and small-buffer batching for outer and focus geometry. Compare on/off runs; draw audit is disabled."),0,17);
            stack.Controls.Add(cpuOuterReuse,0,18);
            stack.Controls.Add(Caption("Extends upload filtering to outer and focus geometry; preparation and draws still run. Implies upload reuse. Excludes audit/proof and upload fast path; no measured gain yet."),0,19);
            stack.Controls.Add(outerSsr,0,20);
            stack.Controls.Add(Caption("Requires smaller native outer targets. Reduces outer reflection resources with a 40% per-axis floor; reflections may soften, focus stays unchanged. Unsupported layouts keep native resources."),0,21);
            options.Controls.Add(stack); grid.Controls.Add(options, 0, 3);
            grid.Controls.Add(Caption("Keep your headset's OpenXR runtime active. Configure VectorXR's LMU Quadviews profile when using it; select one quad-view provider."), 0, 4);
        }
        static NumericUpDown BenchmarkNumber(string name,decimal min,decimal max,decimal value) {
            return new SpacedNumericUpDown {Name=name,AccessibleName=name,Minimum=min,Maximum=max,Value=value,DecimalPlaces=2,Increment=0.05m,Dock=DockStyle.Fill,BackColor=Theme.Field,ForeColor=Theme.Text,BorderStyle=BorderStyle.FixedSingle,TextAlign=HorizontalAlignment.Right};
        }
        static JoystickBinding ReadBinding(string device,string name,int button) {
            return String.IsNullOrEmpty(device)?null:new JoystickBinding {Device=new Guid(device),Name=name,Button=button};
        }
        TableLayoutPanel BenchmarkCard(TableLayoutPanel parent,int row,string title,int[] heights) {
            var card=new BenchmarkSurface {Dock=DockStyle.Fill,Margin=new Padding(0,0,0,14),Padding=new Padding(18,12,18,12)};
            var stack=Stack(heights.Length+1);stack.RowStyles.Add(new RowStyle(SizeType.Absolute,30));
            foreach(int height in heights)stack.RowStyles.Add(new RowStyle(SizeType.Absolute,height));
            stack.Controls.Add(Theme.Label(title,13F,Theme.Text),0,0);card.Controls.Add(stack);parent.Controls.Add(card,0,row);return stack;
        }
        static TableLayoutPanel BenchmarkButtons(params Button[] buttons) {
            var row=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=buttons.Length,RowCount=1,Margin=new Padding(0)};
            foreach(var button in buttons) {row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100F/buttons.Length));button.Dock=DockStyle.Fill;button.Margin=new Padding(0,2,8,2);row.Controls.Add(button);}
            return row;
        }
        void BuildBenchmark() {
            var grid=Stack(5);grid.Dock=DockStyle.Top;grid.AutoSize=true;
            foreach(int height in new[]{304,294,174,330,180})grid.RowStyles.Add(new RowStyle(SizeType.Absolute,height));
            benchmarkPage.SetContent(grid);
            var capture=BenchmarkCard(grid,0,"Manual lap benchmark",new[]{32,46,36,46,62});
            benchmarkEnabled.Name="BenchmarkEnabled";benchmarkTemplate.Name="BenchmarkTemplate";
            benchmarkTemplate.DrawMode=DrawMode.OwnerDrawFixed;benchmarkTemplate.ItemHeight=Math.Max(24,benchmarkTemplate.Font.Height+6);
            benchmarkTemplate.FontChanged+=delegate {benchmarkTemplate.ItemHeight=Math.Max(24,benchmarkTemplate.Font.Height+6);};
            benchmarkTemplate.DrawItem+=delegate(object sender,DrawItemEventArgs e) {
                if(e.Index<0)return;
                using(var background=new SolidBrush((e.State&DrawItemState.Selected)!=0?Theme.AccentDark:Theme.Field))e.Graphics.FillRectangle(background,e.Bounds);
                TextRenderer.DrawText(e.Graphics,benchmarkTemplate.Items[e.Index].ToString(),e.Font,e.Bounds,Theme.Text,TextFormatFlags.Left|TextFormatFlags.VerticalCenter);
            };
            benchmarkVisible.Name="BenchmarkVisibilityButton";benchmarkRecord.Name="BenchmarkRecordButton";
            capture.Controls.Add(benchmarkEnabled,0,1);capture.Controls.Add(benchmarkNotice,0,2);capture.Controls.Add(benchmarkTemplate,0,3);
            capture.Controls.Add(BenchmarkButtons(benchmarkVisible,benchmarkRecord),0,4);
            capture.Controls.Add(Caption("Start before your lap, stop after the finish. Hiding the overlay keeps recording. Keep this launcher open while benchmarking."),0,5);
            benchmarkEnabled.CheckedChanged+=delegate {UpdateBenchmarkNotice();};
            benchmarkTemplate.SelectedIndexChanged+=delegate {ApplyBenchmarkPlacement();RefreshBenchmarkPreview();};
            benchmarkVisible.Click+=delegate {benchmark.ToggleOverlay();RefreshBenchmarkPreview();};
            benchmarkRecord.Click+=delegate {benchmark.ToggleRecording();};
            var bindings=BenchmarkCard(grid,1,"Joystick buttons",new[]{42,40,42,40,62});
            visibilityBindingText.Name="BenchmarkVisibilityBinding";recordingBindingText.Name="BenchmarkRecordingBinding";
            visibilityBindingText.AutoEllipsis=recordingBindingText.AutoEllipsis=true;
            bindings.Controls.Add(visibilityBindingText,0,1);bindings.Controls.Add(BindingButtons(true),0,2);
            bindings.Controls.Add(recordingBindingText,0,3);bindings.Controls.Add(BindingButtons(false),0,4);
            bindingStatus.Name="BenchmarkBindingStatus";bindings.Controls.Add(bindingStatus,0,5);
            var placement=BenchmarkCard(grid,2,"Headset placement (metres)",new[]{84});
            var positions=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=4,RowCount=2,Margin=new Padding(0)};
            positions.RowStyles.Add(new RowStyle(SizeType.Absolute,30));positions.RowStyles.Add(new RowStyle(SizeType.Absolute,38));
            var numbers=new[]{benchmarkX,benchmarkY,benchmarkDistance,benchmarkWidth};var names=new[]{"Horizontal","Vertical","Distance","Width"};
            for(int i=0;i<numbers.Length;++i) {positions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,25));positions.Controls.Add(Caption(names[i]),i,0);numbers[i].Margin=new Padding(0,3,10,0);positions.Controls.Add(numbers[i],i,1);numbers[i].ValueChanged+=delegate {ApplyBenchmarkPlacement();};}
            placement.Controls.Add(positions,0,1);
            var preview=BenchmarkCard(grid,3,"Live overlay preview",new[]{254});preview.Controls.Add(benchmarkPreview,0,1);
            var diagnostics=BenchmarkCard(grid,4,"Connection and reports",new[]{66,42});
            benchmarkStatus.Name="BenchmarkStatus";diagnostics.Controls.Add(benchmarkStatus,0,1);
            var diagnose=Theme.Button("Diagnose latest launch",false);diagnose.Name="BenchmarkDiagnoseButton";
            benchmarkReport.Name="BenchmarkReportButton";diagnostics.Controls.Add(BenchmarkButtons(benchmarkReport,diagnose),0,2);
            benchmarkReport.Click+=delegate {benchmark.OpenLastReport();};
            diagnose.Click+=delegate {MessageBox.Show(this,LaunchDiagnostics.ExplainLatest(root),"Latest launch diagnostics",MessageBoxButtons.OK,MessageBoxIcon.Information);};
            UpdateBenchmarkNotice();RefreshBenchmarkPreview();
        }
        Control BindingButtons(bool visibility) {
            var assign=Theme.Button(visibility?"Assign visibility":"Assign recording",false);
            var clear=Theme.Button("Clear",false);
            assign.Name=visibility?"AssignVisibility":"AssignRecording";clear.Name=visibility?"ClearVisibility":"ClearRecording";
            assign.Click+=delegate {
                if(joystick==null)return;
                joystick.Listen(delegate(JoystickBinding binding) {
                    var other=visibility?recordingBinding:visibilityBinding;
                    if(other!=null&&other.Device==binding.Device&&other.Button==binding.Button) {bindingStatus.Text="That button already controls the other action. Assign a different button.";return;}
                    if(visibility)visibilityBinding=binding;else recordingBinding=binding;
                    UpdateBindings();bindingStatus.Text="Assigned. Save settings to keep this binding.";
                });
                bindingStatus.Text=joystick.ListenStatus;
            };
            clear.Click+=delegate {if(joystick!=null)joystick.CancelListen();if(visibility)visibilityBinding=null;else recordingBinding=null;UpdateBindings();bindingStatus.Text="Binding cleared. Save settings to keep this change.";};
            return BenchmarkButtons(assign,clear);
        }
        void UpdateBindings() {
            visibilityBindingText.Text="Visibility: "+(visibilityBinding==null?"Unassigned":visibilityBinding.ToString());
            recordingBindingText.Text="Recording: "+(recordingBinding==null?"Unassigned":recordingBinding.ToString());
            if(joystick!=null) {joystick.Visibility=visibilityBinding;joystick.Recording=recordingBinding;}
        }
        void UpdateBenchmarkNotice() {
            benchmarkNotice.Text=!diagnosticsEnabled.Checked?"Diagnostics are off: benchmark timing stays disabled. Enable diagnostics in Setup, then relaunch LMU.":benchmarkEnabled.Checked?"Enabled for the next launch. Current sessions keep their launch settings.":"Disabled for the next launch. Enable here, save, and launch LMU to connect.";
        }
        void ApplyBenchmarkPlacement() {
            if(benchmark==null)return;
            benchmark.Template=benchmarkTemplate.SelectedIndex;benchmark.OverlayX=(float)benchmarkX.Value;benchmark.OverlayY=(float)benchmarkY.Value;
            benchmark.OverlayDistance=(float)benchmarkDistance.Value;benchmark.OverlayWidth=(float)benchmarkWidth.Value;
        }
        void RefreshBenchmarkPreview() {
            if(benchmark==null)return;
            var old=benchmarkPreview.Image;benchmarkPreview.Image=benchmark.RenderPreview();if(old!=null)old.Dispose();
        }
        void PollBenchmark() {
            benchmark.Tick();if(joystick!=null) {joystick.Tick();if(joystick.Listening)bindingStatus.Text=joystick.ListenStatus;}
            benchmarkStatus.Text=benchmark.Status;benchmarkVisible.Text=benchmark.OverlayVisible?"Hide overlay":"Show overlay";
            benchmarkRecord.Text=benchmark.Recording?"Stop recording":"Start recording";benchmarkRecord.Enabled=benchmark.Connected||benchmark.Recording;
            benchmarkReport.Enabled=!String.IsNullOrEmpty(benchmark.LastReportPath);
            if(++previewTicks>=4) {previewTicks=0;if(benchmarkPage.Visible)RefreshBenchmarkPreview();}
        }
        void ShowBenchmarkPage() {
            renderingPage.Visible=setupPage.Visible=false;benchmarkPage.Visible=true;
            header.Text="Benchmark";subheader.Text="Measure a lap. Compare the result. See what changed.";
            renderingNav.BackColor=setupNav.BackColor=Theme.Sidebar;renderingNav.ForeColor=setupNav.ForeColor=Theme.Muted;
            benchmarkNav.BackColor=Theme.AccentDark;benchmarkNav.ForeColor=Theme.Text;RefreshBenchmarkPreview();
        }
        protected override void Dispose(bool disposing) {
            if(disposing) {help.Dispose();benchmarkTimer.Stop();benchmarkTimer.Dispose();if(joystick!=null)joystick.Dispose();if(benchmark!=null)benchmark.Dispose();if(benchmarkPreview.Image!=null) {benchmarkPreview.Image.Dispose();benchmarkPreview.Image=null;}}
            base.Dispose(disposing);
        }
        Control PathCard(string title, TextBox field, Button browse, string hint, bool link,string address="https://github.com/mbucchia/Quad-Views-Foveated/wiki")
        {
            var card = new Surface { Dock = DockStyle.Fill, Margin = new Padding(0, 0, 0, 14), Padding = new Padding(17, 12, 17, 12) };
            var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = link ? 4 : 3, Margin = new Padding(0) };
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 87));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 29)); grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 35));
            grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); if (link) grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 25));
            grid.Controls.Add(Theme.Label(title, 13F, Theme.Text), 0, 0); grid.SetColumnSpan(grid.GetControlFromPosition(0, 0), 2);
            browse.Dock = DockStyle.Fill; browse.Margin = new Padding(0); browse.Font = new Font("Segoe UI", 9F);
            grid.Controls.Add(field, 0, 1); grid.Controls.Add(browse, 1, 1); grid.Controls.Add(Caption(hint), 0, 2); grid.SetColumnSpan(grid.GetControlFromPosition(0, 2), 2);
            if (link) { Link(grid, "Installation guide and download", address, 3); grid.SetColumnSpan(grid.GetControlFromPosition(0, 3), 2); }
            card.Controls.Add(grid); return card;
        }
        Control BuildFooter()
        {
            var footer = Stack(2); footer.RowStyles.Add(new RowStyle(SizeType.Absolute, 49)); footer.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            footer.Controls.Add(status, 0, 0);
            var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft, WrapContents = false, Margin = new Padding(0) };
            launch.Width = 180; save.Width = 146; launch.Margin = new Padding(10, 0, 0, 0); save.Margin = new Padding(0);
            buttons.Controls.Add(launch); buttons.Controls.Add(save); footer.Controls.Add(buttons, 0, 1); return footer;
        }
        void ShowPage(bool setup)
        {
            benchmarkPage.Visible=false; benchmarkNav.BackColor=Theme.Sidebar;benchmarkNav.ForeColor=Theme.Muted;
            setupPage.Visible = setup; renderingPage.Visible = !setup;
            header.Text = setup ? "Setup" : "Rendering";
            subheader.Text = setup ? "Connect the game, the provider, and your headset." : "Fine-tune image quality where it matters.";
            renderingNav.BackColor = setup ? Theme.Sidebar : Theme.AccentDark; setupNav.BackColor = setup ? Theme.AccentDark : Theme.Sidebar;
            renderingNav.ForeColor = setup ? Theme.Muted : Theme.Text; setupNav.ForeColor = setup ? Theme.Text : Theme.Muted;
        }
        void UpdateProvider()
        {
            if(filling)return;
            bool qvfr = backend.SelectedIndex == 1;
            bool vector = backend.SelectedIndex == 2;
            bool vrs=backend.SelectedIndex==3;
            if(vrs!=focusDisplayedAsVrs) {
                decimal w=focusGeometry==null||focusDisplayedAsVrs?focusWidth.Value:focusGeometry.ProviderWidth(focusWidth.Value);
                decimal h=focusGeometry==null||focusDisplayedAsVrs?focusHeight.Value:focusGeometry.ProviderHeight(focusHeight.Value);
                SetFocusDisplay(w,h,vrs);
            }
            outer.Enabled = inner.Enabled = qvfr;
            ((TableLayoutPanel)outer.Parent).GetControlFromPosition(0,3).Text=vrs?"Stereo VRS uses full runtime/game resolution. Adjust resolution there.":"Linear scale per dimension.";
            focusWidth.Enabled = focusHeight.Enabled = qvfr||vrs;
            dynamicFocus.Enabled=dynamicMin.Enabled=dynamicMax.Enabled=dynamicReserve.Enabled=!vrs;
            vrsRate.Enabled=vrs;cropWidth.Enabled=cropHeight.Enabled=outerBlur.Enabled=nativeOuter.Enabled=outline.Enabled=!vrs;
            Controls.Find("ResetCrop",true)[0].Enabled=!vrs;
            manifest.Enabled = browseManifest.Enabled = qvfr;
            vectorManifest.Enabled = browseVectorManifest.Enabled = vector;
            refreshFov.Enabled=qvfr;
            providerHint.Text = vrs ? "Native stereo with NVIDIA VRS. Auto uses available gaze, with fixed focus fallback. Experimental; compare on your headset."
                : vector ? "Uses VectorXR instead of QVFR. Set resolution and focus in VectorXR's LMU Quadviews profile."
                : qvfr ? "Uses Quad-Views-Foveated with your active OpenXR runtime." : "Use your runtime's quad-view settings, for example Pimax EVO.";
            focusHint.Text=vrs?"Percent of each eye's projected span. Inside stays full shading; outside uses the selected VRS rate."
                :vector?"Set focus size in VectorXR's LMU Quadviews profile."
                :focusGeometry==null?"Per-axis % until measured: run LMU once, then read FOV below."
                :"Equal-axis sizes: 50 / 50 is square near the center. Re-read FOV after headset/FOV changes.";
            resolutionHint.Text = vrs ? "Stereo VRS: full resolution comes from your runtime/game. Quad crop, smoothing and research options are inactive; saved values remain. Restart LMU after changes."
                :qvfr ? "200% resolution means 2x width and 2x height (4x pixels). Save updates only LMU's QVFR profile and backs up existing settings. Restart LMU after changes."
                : vector ? "Resolution, focus size and tracking are configured in VectorXR's LMU profile. QVFR and OpenXR Toolkit are bypassed for this launch. Restart LMU after changes."
                : "Resolution and focus size are controlled by your native runtime. Switch to Quad-Views-Foveated to adjust them here. Restart LMU after changes.";
            UpdateAdvanced();
        }
        void UpdateAdvanced() {
            bool quad=backend.SelectedIndex!=3;
            bool outerReuse=cpuOuterReuse.Checked&&!cpuUploadBatch.Checked;
            cpuUploadBatch.Enabled=quad&&(!outerReuse||cpuUploadBatch.Checked);
            cpuOuterReuse.Enabled=quad&&!cpuUploadBatch.Checked;
            cpuUploadReuse.Enabled=quad&&!cpuUploadBatch.Checked&&!outerReuse;
            outerSsr.Enabled=quad&&nativeOuter.Checked;
            renderProfiling.Enabled=diagnosticsEnabled.Checked;
            profileDuration.Enabled=diagnosticsEnabled.Checked&&renderProfiling.Checked;
            stereoDrawAudit.Enabled=quad&&diagnosticsEnabled.Checked&&!cpuUploadBatch.Checked&&!outerReuse;
            stereoShaderCapture.Enabled=stereoPairProof.Enabled=stereoDrawAudit.Enabled&&stereoDrawAudit.Checked;
        }
        void SetFocusDisplay(decimal width,decimal height,bool vrs) {
            focusDisplayedAsVrs=vrs;
            bool calibrated=focusGeometry!=null&&!vrs;
            focusWidth.SetRange(calibrated?focusGeometry.DisplayWidth(10):10,calibrated?focusGeometry.DisplayWidth(90):90);
            focusHeight.SetRange(calibrated?focusGeometry.DisplayHeight(10):10,calibrated?focusGeometry.DisplayHeight(90):90);
            focusWidth.Value=calibrated?focusGeometry.DisplayWidth(width):width;
            focusHeight.Value=calibrated?focusGeometry.DisplayHeight(height):height;
        }
        void Fill(LauncherSettings s)
        {
            filling=true;
            focusGeometry=s.FocusAspectRatio==0?null:new FocusGeometry(s.FocusAspectRatio);
            focusWidth.SetRange(focusGeometry==null?10:focusGeometry.DisplayWidth(10),focusGeometry==null?90:focusGeometry.DisplayWidth(90));
            focusHeight.SetRange(focusGeometry==null?10:focusGeometry.DisplayHeight(10),focusGeometry==null?90:focusGeometry.DisplayHeight(90));
            focusHint.Text=focusGeometry==null?"Per-axis % until measured: run LMU once, then read FOV below."
                :"Equal-axis sizes: 50 / 50 is square near the center. Re-read FOV after headset/FOV changes.";
            game.Text = s.GameDirectory ?? ""; manifest.Text = s.QuadViewsManifest ?? "";
            vectorManifest.Text=s.VectorXrManifest ?? "";
            backend.SelectedIndex = s.Backend=="VRS"?3:s.Backend == "VectorXR" ? 2 : s.Backend == "QVFR" ? 1 : 0; mode.SelectedIndex = s.FoveationMode == "fixed" ? 1 : 0;
            vrsRate.SelectedIndex=s.VrsPeripheralRate==4?2:s.VrsPeripheralRate==1?0:1;
            outline.Checked = s.DebugFocus; nativeOuter.Checked = s.NativeOuterResolution;
            renderProfiling.Checked=s.RenderProfiling;
            diagnosticsEnabled.Checked=s.DiagnosticsEnabled;
            benchmarkEnabled.Checked=s.BenchmarkEnabled;benchmarkTemplate.SelectedIndex=s.BenchmarkTemplate;
            benchmarkX.Value=s.BenchmarkX;benchmarkY.Value=s.BenchmarkY;benchmarkDistance.Value=s.BenchmarkDistance;benchmarkWidth.Value=s.BenchmarkWidth;
            visibilityBinding=ReadBinding(s.BenchmarkVisibilityDevice,s.BenchmarkVisibilityName,s.BenchmarkVisibilityButton);
            recordingBinding=ReadBinding(s.BenchmarkRecordingDevice,s.BenchmarkRecordingName,s.BenchmarkRecordingButton);
            UpdateBindings();ApplyBenchmarkPlacement();UpdateBenchmarkNotice();
            stereoDrawAudit.Checked=s.StereoDrawAudit;
            stereoShaderCapture.Checked=s.StereoShaderCapture;stereoShaderCapture.Enabled=s.DiagnosticsEnabled&&s.StereoDrawAudit;
            stereoPairProof.Checked=s.StereoPairProof;stereoPairProof.Enabled=s.DiagnosticsEnabled&&s.StereoDrawAudit;cpuUploadReuse.Checked=s.CpuUploadReuse;
            cpuUploadBatch.Checked=s.CpuUploadBatch;
            cpuOuterReuse.Checked=s.CpuOuterReuse;outerSsr.Checked=s.OuterSsr;
            profileDuration.Value=s.RenderProfileSeconds;
            outer.Value = s.OuterPercent; inner.Value = s.InnerPercent;
            dynamicFocus.Checked=s.DynamicFocusEnabled;dynamicMin.Value=s.DynamicFocusMinPercent;dynamicMax.Value=s.DynamicFocusMaxPercent;dynamicReserve.Value=s.DynamicFocusReservePercent;
            cropWidth.Value=s.CropWidthPercent;cropHeight.Value=s.CropHeightPercent;
            SetFocusDisplay(s.FocusWidthPercent,s.FocusHeightPercent,s.Backend=="VRS");
            focusOffsetX.Value=s.FocusOffsetXPercent;focusOffsetY.Value=s.FocusOffsetYPercent;
            outerBlur.Value = Math.Max(outerBlur.Minimum, Math.Min(outerBlur.Maximum, s.OuterBlur));
            filling=false;UpdateProvider();
        }
        LauncherSettings Values()
        {
            var s = new LauncherSettings { GameDirectory = game.Text.Trim(), QuadViewsManifest = manifest.Text.Trim(),VectorXrManifest=vectorManifest.Text.Trim(),
                Backend = backend.SelectedIndex==3?"VRS":backend.SelectedIndex == 2 ? "VectorXR" : backend.SelectedIndex == 1 ? "QVFR" : "Native", FoveationMode = mode.SelectedIndex == 1 ? "fixed" : "auto",
                VrsPeripheralRate=vrsRate.SelectedIndex==2?4:vrsRate.SelectedIndex==0?1:2,
                CpuOuterReuse=cpuOuterReuse.Checked,OuterSsr=outerSsr.Checked,
                DebugFocus = outline.Checked, RenderProfiling=renderProfiling.Checked, DiagnosticsEnabled=diagnosticsEnabled.Checked,StereoDrawAudit=stereoDrawAudit.Checked,StereoShaderCapture=stereoShaderCapture.Checked,StereoPairProof=stereoPairProof.Checked,CpuUploadReuse=cpuUploadReuse.Checked,CpuUploadBatch=cpuUploadBatch.Checked, RenderProfileSeconds=Decimal.ToInt32(profileDuration.Value), NativeOuterResolution = nativeOuter.Checked,
                BenchmarkEnabled=benchmarkEnabled.Checked,BenchmarkTemplate=benchmarkTemplate.SelectedIndex,
                BenchmarkX=benchmarkX.Value,BenchmarkY=benchmarkY.Value,BenchmarkDistance=benchmarkDistance.Value,BenchmarkWidth=benchmarkWidth.Value,
                BenchmarkVisibilityDevice=visibilityBinding==null?"":visibilityBinding.Device.ToString("D"),BenchmarkVisibilityName=visibilityBinding==null?"":visibilityBinding.Name,BenchmarkVisibilityButton=visibilityBinding==null?-1:visibilityBinding.Button,
                BenchmarkRecordingDevice=recordingBinding==null?"":recordingBinding.Device.ToString("D"),BenchmarkRecordingName=recordingBinding==null?"":recordingBinding.Name,BenchmarkRecordingButton=recordingBinding==null?-1:recordingBinding.Button,
                OuterPercent = outer.Value, InnerPercent = inner.Value,
                DynamicFocusEnabled=dynamicFocus.Checked,DynamicFocusMinPercent=dynamicMin.Value,DynamicFocusMaxPercent=dynamicMax.Value,DynamicFocusReservePercent=dynamicReserve.Value,
                CropWidthPercent=cropWidth.Value,CropHeightPercent=cropHeight.Value,
                FocusWidthPercent = focusGeometry==null||focusDisplayedAsVrs?focusWidth.Value:focusGeometry.ProviderWidth(focusWidth.Value),
                FocusHeightPercent = focusGeometry==null||focusDisplayedAsVrs?focusHeight.Value:focusGeometry.ProviderHeight(focusHeight.Value),
                FocusOffsetXPercent=focusOffsetX.Value,FocusOffsetYPercent=focusOffsetY.Value,
                FocusAspectRatio=focusGeometry==null?0:focusGeometry.Aspect, OuterBlur = outerBlur.Value };
            QvfrSettings.Validate(s); return s;
        }
        void RefreshFocusGeometry() {
            try {
                var measured=FocusGeometry.Latest(root);
                if(measured==null) {
                    var old=Values();old.FocusAspectRatio=0;Fill(old);
                    throw new InvalidOperationException("No compatible stereo FOV measurement in the latest run. Returned to per-axis percentages. Start LMU with this package and headset, then try again.");
                }
                var settings=Values();settings.FocusAspectRatio=measured.Aspect;Fill(settings);
                status.Text="Headset FOV refreshed. Current box preserved; equal width/height now uses equal-axis sizes. Save to keep this calibration.";
            } catch(Exception error) { ShowError(error.Message); }
        }
        void SaveSettings()
        {
            if (busy) return;
            try { var s = Values(); if (s.Backend == "QVFR") QvfrSettings.SaveProfile(s); QvfrSettings.Save(SettingsPath, s);
                status.Text = s.Backend=="VRS"?"Saved. Stereo VRS uses runtime/game resolution and these focus/shading settings. Restart LMU to apply."
                    :s.Backend == "VectorXR" ? "Saved. Enable Quadviews for LMU in VectorXR; its profile controls resolution and focus."
                    : s.Backend == "QVFR" ? "Saved. LMU's QVFR profile is applied; existing settings are backed up when changed." : "Saved. Native resolution and focus settings stay in your runtime.";
                status.ForeColor = Theme.Muted; }
            catch (Exception ex) { ShowError(ex.Message); }
        }
        void SetBusy(bool value)
        {
            busy = value; save.Enabled = launch.Enabled = !value; renderingPage.Enabled = setupPage.Enabled = !value;
            launch.Text = value ? "Starting..." : "Launch LMU"; UseWaitCursor = value;
        }
        void StartGame()
        {
            if (busy) return;
            LauncherSettings settings;
            try { settings = Values(); QvfrSettings.Save(SettingsPath, settings); }
            catch (Exception ex) { ShowError(ex.Message); return; }
            SetBusy(true); status.ForeColor = Theme.Muted; status.Text = "Checking the game build and preparing OpenXR...";
            var worker = new BackgroundWorker();
            worker.DoWork += delegate(object sender, DoWorkEventArgs e) { e.Result = LaunchService.Launch(root, settings); };
            worker.RunWorkerCompleted += delegate(object sender, RunWorkerCompletedEventArgs e) {
                worker.Dispose(); if (IsDisposed || Disposing) return; SetBusy(false);
                if (e.Error != null) { ShowError(e.Error.Message); return; }
                status.ForeColor = Color.FromArgb(149, 207, 169); status.Text = "LMU started (PID " + e.Result + "). Changes apply on your next launch.";
            };
            worker.RunWorkerAsync();
        }
        void OpenLink(string url) { try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); } catch (Exception ex) { ShowError(ex.Message); } }
        void ShowError(string message)
        {
            status.ForeColor = Color.FromArgb(241, 174, 159);
            status.Text = message.Length > 190 ? message.Substring(0, 190) + "..." : message;
            MessageBox.Show(this, message, "LMU Quad View", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Launcher());
        }
    }
}
