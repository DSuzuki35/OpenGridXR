// Hidden-window smoke tests. Manual geometry/font scaling is a simulation,
// not a substitute for moving the launcher between physical DPI monitors.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Reflection;
using System.Windows.Forms;
using LmuQuadViewLauncher;

class GuiLayoutTests {
    [DllImport("user32.dll")] static extern IntPtr GetThreadDpiAwarenessContext();
    [DllImport("user32.dll")] [return:MarshalAs(UnmanagedType.Bool)]
    static extern bool AreDpiAwarenessContextsEqual(IntPtr first,IntPtr second);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll",EntryPoint="GetWindowLongW")] static extern int GetWindowLong(IntPtr window,int index);
    [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr window,int message,IntPtr wParam,IntPtr lParam);
    static void Check(bool good,string message) { if(!good) throw new InvalidOperationException(message); }
    static List<Control> Descendants(Control root) {
        var controls=new List<Control>();controls.Add(root);
        foreach(Control child in root.Controls) controls.AddRange(Descendants(child));
        return controls;
    }
    static Control Named(Control root,string name) {
        var found=root.Controls.Find(name,true);Check(found.Length==1,"Expected exactly one named control: "+name);return found[0];
    }
    static void Layout(Control root) {
        foreach(Control child in root.Controls) Layout(child);
        root.PerformLayout();
    }
    static void Settle(Form form) { for(int i=0;i<3;++i) { Layout(form);Application.DoEvents(); } }
    static void TextFits(Control control) {
        var flags=TextFormatFlags.SingleLine;
        using(var graphics=control.CreateGraphics()) {
            var measured=TextRenderer.MeasureText(graphics,control.Text,control.Font,new Size(Int32.MaxValue,Int32.MaxValue),flags);
            int reserve=control is Button?6:0;
            Check(measured.Width<=control.ClientSize.Width-control.Padding.Horizontal-reserve,
                control.Name+" text width "+measured.Width+" exceeds client "+control.ClientSize.Width);
            Check(measured.Height<=control.ClientSize.Height-control.Padding.Vertical,
                control.Name+" text height "+measured.Height+" exceeds client "+control.ClientSize.Height);
        }
    }
    static void FullyVisible(Control control) {
        Check(control.Visible,control.Name+" is hidden");
        var rectangle=control.RectangleToScreen(control.ClientRectangle);
        for(var parent=control.Parent;parent!=null;parent=parent.Parent) {
            var bounds=parent.RectangleToScreen(parent.ClientRectangle);
            bounds.Inflate(1,1);
            Check(bounds.Contains(rectangle),control.Name+" "+rectangle+" is clipped by "+parent.GetType().Name+" "+parent.Name+" "+bounds);
        }
    }
    static void NoNativeScrollBars(Form form) {
        foreach(var control in Descendants(form)) {
            var scroll=control as ScrollableControl;
            if(scroll==null) continue;
            Check(!scroll.AutoScroll,control.GetType().Name+" unexpectedly enables native AutoScroll");
            if(control.IsHandleCreated) Check((GetWindowLong(control.Handle,-16)&0x00300000)==0,
                control.GetType().Name+" has a native horizontal/vertical scrollbar");
        }
    }
    static void SimulateScale(Form form,float factor) {
        if(factor==1) return;
        var controls=Descendants(form);var fonts=new Dictionary<Control,Font>();
        foreach(var control in controls) { fonts.Add(control,control.Font);control.SuspendLayout();
            var container=control as ContainerControl;if(container!=null)container.AutoScaleMode=AutoScaleMode.None; }
        form.Scale(new SizeF(factor,factor));
        // Snapshot first so inherited fonts are not multiplied repeatedly.
        foreach(var control in controls) {
            var original=fonts[control];control.Font=new Font(original.FontFamily,original.SizeInPoints*factor,original.Style,GraphicsUnit.Point);
        }
        for(int i=controls.Count-1;i>=0;--i)controls[i].ResumeLayout(true);
        Settle(form);
    }
    static void Screenshot(Form form,string path) {
        using(var bitmap=new Bitmap(form.Width,form.Height)) { form.DrawToBitmap(bitmap,new Rectangle(Point.Empty,form.Size));bitmap.Save(path,ImageFormat.Png); }
    }
    static void NumericSpacing(Control page) {
        foreach(var control in Descendants(page)) {
            var number=control as NumericUpDown;if(number==null||!number.Visible)continue;
            TextBox edit=null;foreach(Control child in number.Controls)if(child is TextBox)edit=(TextBox)child;
            Check(edit!=null&&edit.IsHandleCreated,"Native numeric edit missing: "+number.AccessibleName);
            int right=(int)((SendMessage(edit.Handle,0xD4,IntPtr.Zero,IntPtr.Zero).ToInt64()>>16)&65535);
            Check(right>=Math.Round(6*number.DeviceDpi/96.0),"Numeric value touches spinner arrows: "+number.AccessibleName+" margin="+right+" dpi="+number.DeviceDpi+" font="+number.Font.SizeInPoints);
            foreach(var value in new[]{number.Minimum,number.Maximum}) {
                var text=value.ToString("F"+number.DecimalPlaces,System.Globalization.CultureInfo.CurrentCulture);
                int width=TextRenderer.MeasureText(text,number.Font,Size.Empty,TextFormatFlags.NoPadding|TextFormatFlags.SingleLine).Width;
                Check(width<=edit.ClientSize.Width-right,"Signed/max/decimal numeric text is clipped: "+number.AccessibleName+" "+text);
            }
            decimal saved=number.Value;number.Value=number.Minimum;number.UpButton();
            Check(number.Value==Math.Min(number.Maximum,number.Minimum+number.Increment),"Native numeric increment changed");
            number.DownButton();Check(number.Value==number.Minimum,"Native numeric decrement changed");
            decimal typed=Math.Min(number.Maximum,number.Minimum+number.Increment);
            edit.Text=typed.ToString(System.Globalization.CultureInfo.CurrentCulture);
            typeof(NumericUpDown).GetMethod("ValidateEditText",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(number,null);
            Check(number.Value==typed,"Native numeric text editing changed");number.Value=saved;
        }
    }
    static void BrandAlignment(Form form) {
        var title=(Label)Named(form,"BrandTitle");
        foreach(var name in new[]{"BrandTitle","BrandSubtitle","BrandRenderer"}) {
            var label=(Label)Named(form,name);TextFits(label);FullyVisible(label);
            Check(label.TextAlign==ContentAlignment.MiddleLeft,"Brand is not left aligned: "+name);
            Check(label.Left==title.Left&&label.Right==title.Right&&label.Margin==title.Margin&&label.Padding==title.Padding,"Brand labels do not share bounds/margins/padding: "+name);
        }
    }
    static void Page(Form form,string pageName,string navigation,string output,string stem) {
        ((Button)Named(form,navigation)).PerformClick();Settle(form);
        var page=(DarkScrollPage)Named(form,pageName);
        Check(page.Visible,pageName+" did not become active");
        var scroll=(DarkScrollBar)Named(page,"PageScrollBar");
        scroll.Value=0;Settle(form);
        foreach(var name in new[]{"BrandTitle","CoffeeButton","SaveButton","LaunchButton"}) {
            var control=Named(form,name);TextFits(control);FullyVisible(control);
        }
        if(pageName=="RenderingPage") {
            var provider=(ComboBox)Named(form,"ProviderSelector");
            foreach(var item in provider.Items)Check(TextRenderer.MeasureText(item.ToString(),provider.Font).Width+SystemInformation.VerticalScrollBarWidth<provider.Width,"Provider name is clipped");
        }
        if(pageName=="BenchmarkPage") {
            foreach(var name in new[]{"BenchmarkEnabled","BenchmarkTemplate","BenchmarkVisibilityButton","BenchmarkRecordButton"})FullyVisible(Named(form,name));
            foreach(var name in new[]{"BenchmarkVisibilityButton","BenchmarkRecordButton","AssignVisibility","AssignRecording","ClearVisibility","ClearRecording","BenchmarkReportButton","BenchmarkDiagnoseButton"})TextFits(Named(form,name));
            var preview=(PictureBox)Named(form,"BenchmarkPreview");Check(preview.Image!=null,"Benchmark preview bitmap missing");
        }
        NumericSpacing(page);BrandAlignment(form);
        NoNativeScrollBars(form);
        var freeNotice=Named(form,"FreeNotice");
        foreach(var line in freeNotice.Text.Split('\n'))Check(TextRenderer.MeasureText(line,freeNotice.Font).Width<=freeNotice.Width,"Optional donation notice is clipped");
        Check(freeNotice.Height>=TextRenderer.MeasureText("Text",freeNotice.Font).Height*3,"Optional donation notice height is clipped");
        Screenshot(form,Path.Combine(output,stem+"-"+pageName+"-top.png"));
        Check(scroll.TabStop&&scroll.AccessibleRole==AccessibleRole.ScrollBar,"Custom scrollbar is not keyboard/accessibility reachable");
        if(scroll.Maximum>0) {
            typeof(DarkScrollBar).GetMethod("OnMouseWheel",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(scroll,
                new object[]{new MouseEventArgs(MouseButtons.None,0,0,0,-120)});
            Check(scroll.Value>0,pageName+" mouse wheel did not scroll");
        }
        typeof(DarkScrollBar).GetMethod("OnKeyDown",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(scroll,
            new object[]{new KeyEventArgs(Keys.End)});
        Check(scroll.Value==scroll.Maximum,pageName+" End key did not reach the bottom");Settle(form);
        Panel viewport=null;foreach(Control child in page.Controls)if(child is Panel)viewport=(Panel)child;
        Check(viewport!=null&&viewport.Controls.Count==1,pageName+" viewport content missing");
        var content=viewport.Controls[0];
        Check(content.Width<=viewport.ClientSize.Width+1,pageName+" content has horizontal overflow");
        Check(content.Bottom<=viewport.ClientSize.Height+1,pageName+" last content is unreachable at scroll bottom");
        foreach(var name in new[]{"SaveButton","LaunchButton","CoffeeButton"})FullyVisible(Named(form,name));
        NoNativeScrollBars(form);
        if(pageName=="SetupPage") {
            foreach(var name in new[]{"CpuOuterReuse","OuterSsr"}) {
                var control=Named(form,name);control.Focus();Settle(form);FullyVisible(control);TextFits(control);
            }
        }
        if(pageName=="RenderingPage") {
            var reset=Named(form,"ResetFocusOffsets");reset.Focus();Settle(form);FullyVisible(reset);TextFits(reset);
            foreach(var name in new[]{"FocusOffsetXPercent","FocusOffsetYPercent"}) {
                var control=Named(form,name);control.Focus();Settle(form);FullyVisible(control);
            }
            foreach(var name in new[]{"CropWidthPercent","CropHeightPercent","ResetCrop"}) {
                var control=Named(form,name);control.Focus();Settle(form);FullyVisible(control);
            }
            TextFits(Named(form,"ResetCrop"));TextFits(Named(form,"CropTitle"));
            var cropHint=Named(form,"CropHint");
            var wrapped=TextRenderer.MeasureText(cropHint.Text,cropHint.Font,new Size(cropHint.ClientSize.Width,Int32.MaxValue),TextFormatFlags.WordBreak);
            Check(wrapped.Height<=cropHint.ClientSize.Height,"Crop explanation is clipped");
            Screenshot(form,Path.Combine(output,stem+"-crop.png"));
            foreach(var name in new[]{"DynamicFocusEnabled","DynamicFocusMinPercent","DynamicFocusMaxPercent","DynamicFocusReservePercent"}) {
                var control=Named(form,name);control.Focus();Settle(form);FullyVisible(control);
            }
            TextFits(Named(form,"DynamicFocusEnabled"));
            Screenshot(form,Path.Combine(output,stem+"-dynamic-focus.png"));
        }
        Screenshot(form,Path.Combine(output,stem+"-"+pageName+"-bottom.png"));
        Console.WriteLine(stem+" "+pageName+": text/ancestor bounds, footer reachability, dark scrolling PASS; scroll="+scroll.Maximum);
    }
    [STAThread]
    static int Main(string[] args) {
        try {
            Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
            string output=args.Length>0?Path.GetFullPath(args[0]):Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"gui-qa");
            Directory.CreateDirectory(output);
            using(var form=new Launcher()) {
                var flags=BindingFlags.Instance|BindingFlags.NonPublic;
                var experiments=new LauncherSettings {CpuOuterReuse=true,OuterSsr=true,DiagnosticsEnabled=false,StereoDrawAudit=true,StereoShaderCapture=true,StereoPairProof=true};
                typeof(Launcher).GetMethod("Fill",flags).Invoke(form,new object[]{experiments});
                var experimentValues=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(experimentValues.CpuOuterReuse&&experimentValues.OuterSsr,"GUI lost performance experiment preferences");
                Check(Named(form,"CpuOuterReuse").Enabled&&Named(form,"OuterSsr").Enabled&&!Named(form,"CpuUploadBatch").Enabled&&!Named(form,"CpuUploadReuse").Enabled,"Performance dependencies wrong with diagnostics off");
                ((CheckBox)Named(form,"DiagnosticsEnabled")).Checked=true;
                Check(!Named(form,"StereoDrawAudit").Enabled&&!Named(form,"StereoShaderCapture").Enabled&&!Named(form,"StereoPairProof").Enabled,"Outer reuse enables incompatible audit/proof");
                var nativeOuter=(CheckBox)typeof(Launcher).GetField("nativeOuter",flags).GetValue(form);nativeOuter.Checked=false;
                Check(!Named(form,"OuterSsr").Enabled&&((CheckBox)Named(form,"OuterSsr")).Checked,"Native outer off must preserve but disable SSR preference");
                ((ComboBox)Named(form,"ProviderSelector")).SelectedIndex=3;
                Check(!Named(form,"CpuOuterReuse").Enabled&&!Named(form,"OuterSsr").Enabled,"VRS enables quad performance experiments");
                ((ComboBox)Named(form,"ProviderSelector")).SelectedIndex=0;nativeOuter.Checked=true;
                experimentValues=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(experimentValues.CpuOuterReuse&&experimentValues.OuterSsr&&experimentValues.StereoDrawAudit&&experimentValues.StereoPairProof,"Mode switching destroys experiment or diagnostic preferences");
                experiments.CpuUploadBatch=true;typeof(Launcher).GetMethod("Fill",flags).Invoke(form,new object[]{experiments});
                Check(Named(form,"CpuUploadBatch").Enabled&&!Named(form,"CpuOuterReuse").Enabled,"Saved conflicting CPU options must allow turning off the winning batch option");
                var vrs=new LauncherSettings {Backend="VRS",VrsPeripheralRate=4,FocusAspectRatio=2,FocusWidthPercent=30,FocusHeightPercent=60,CropWidthPercent=75,NativeOuterResolution=true,StereoDrawAudit=true,CpuUploadReuse=true,RenderProfiling=true,DebugFocus=true};
                typeof(Launcher).GetMethod("Fill",flags).Invoke(form,new object[]{vrs});
                var vrsValues=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(vrsValues.Backend=="VRS","GUI lost VRS selection");
                var outline=(CheckBox)typeof(Launcher).GetField("outline",flags).GetValue(form);
                Check(!outline.Enabled&&outline.Checked&&vrsValues.DebugFocus,"VRS must disable quad outline while preserving its saved preference");
                Check(vrsValues.VrsPeripheralRate==4,"GUI lost VRS peripheral rate");
                Check(!Named(form,"DynamicFocusEnabled").Enabled&&!Named(form,"DynamicFocusMinPercent").Enabled&&!Named(form,"CropWidthPercent").Enabled&&!Named(form,"OuterResolution").Enabled&&!Named(form,"CpuUploadReuse").Enabled&&!Named(form,"StereoDrawAudit").Enabled,"VRS enables quad-only controls");
                Check(vrsValues.CropWidthPercent==75&&vrsValues.NativeOuterResolution&&vrsValues.StereoDrawAudit&&vrsValues.CpuUploadReuse,"Selecting VRS destroys saved quad settings");
                Check(Named(form,"RenderProfileSeconds").Enabled,"VRS disabled render profiling");
                var vrsWidth=(PercentSetting)typeof(Launcher).GetField("focusWidth",flags).GetValue(form);
                Check(vrsWidth.Enabled&&vrsWidth.Value==30,"VRS width must use raw projected eye span percent");
                ((ComboBox)Named(form,"ProviderSelector")).SelectedIndex=0;
                Check(outline.Enabled&&outline.Checked,"Returning to quad mode must restore the saved outline control");
                Check(vrsWidth.Value==60,"Switching from VRS lost calibrated quad display");
                ((ComboBox)Named(form,"ProviderSelector")).SelectedIndex=3;
                Check(vrsWidth.Value==30,"Switching back to VRS changed focus geometry");
                foreach(var providerName in new[]{"Native","QVFR","VectorXR"})foreach(var foveation in new[]{"fixed","auto"}) {
                    var offsets=new LauncherSettings {Backend=providerName,FoveationMode=foveation,FocusOffsetXPercent=-12.5m,FocusOffsetYPercent=50,CropWidthPercent=75.5m,CropHeightPercent=25};
                    typeof(Launcher).GetMethod("Fill",flags).Invoke(form,new object[]{offsets});
                    var x=(PercentSetting)Named(form,"FocusOffsetXPercent");var y=(PercentSetting)Named(form,"FocusOffsetYPercent");
                    Check(x.Enabled&&y.Enabled&&x.Value==-12.5m&&y.Value==50,"Offsets disabled or lost for "+providerName+" "+foveation);
                    var saved=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                    var cropX=(PercentSetting)Named(form,"CropWidthPercent");var cropY=(PercentSetting)Named(form,"CropHeightPercent");
                    Check(cropX.Enabled&&cropY.Enabled&&cropX.Value==75.5m&&cropY.Value==25,"Crop disabled or lost for "+providerName+" "+foveation);
                    Check(saved.CropWidthPercent==75.5m&&saved.CropHeightPercent==25,"Crop GUI roundtrip failed");
                    cropX.Value=80;Check(cropY.Value==25,"Crop dimensions are linked");
                    cropX.Value=0;cropY.Value=101;Check(cropX.Value==25&&cropY.Value==100,"Crop UI range incorrect");
                    typeof(Button).GetMethod("OnClick",flags).Invoke(Named(form,"ResetCrop"),new object[]{EventArgs.Empty});
                    var full=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                    Check(full.CropWidthPercent==100&&full.CropHeightPercent==100,"Reset did not restore full FOV");
                    Check(saved.FocusOffsetXPercent==-12.5m&&saved.FocusOffsetYPercent==50,"Offset GUI roundtrip failed");
                    typeof(Button).GetMethod("OnClick",flags).Invoke(Named(form,"ResetFocusOffsets"),new object[]{EventArgs.Empty});
                    saved=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                    Check(saved.FocusOffsetXPercent==0&&saved.FocusOffsetYPercent==0,"Reset did not clear both offsets");
                }
                var benchmarkSettings=new LauncherSettings {BenchmarkEnabled=true,BenchmarkTemplate=1,BenchmarkX=-0.5m,BenchmarkY=0.2m,BenchmarkDistance=1.5m,BenchmarkWidth=0.9m,DiagnosticsEnabled=false};
                typeof(Launcher).GetMethod("Fill",flags).Invoke(form,new object[]{benchmarkSettings});
                var benchmarkValues=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(benchmarkValues.BenchmarkEnabled&&!benchmarkValues.DiagnosticsEnabled&&benchmarkValues.BenchmarkTemplate==1,"Benchmark opt-in silently enabled diagnostics or lost preference");
                var controller=(BenchmarkController)typeof(Launcher).GetField("benchmark",flags).GetValue(form);
                Check(controller.Template==1&&Math.Abs(controller.OverlayX+0.5f)<0.001f&&Math.Abs(controller.OverlayWidth-0.9f)<0.001f,"Benchmark live placement did not reach controller");
                ((NumericUpDown)Named(form,"BenchmarkDistance")).Value=2.25m;Check(controller.OverlayDistance==2.25f,"Live distance edit did not reach controller");
                var original=new LauncherSettings {Backend="QVFR",OuterPercent=10,InnerPercent=208,FocusWidthPercent=23,FocusHeightPercent=50,FocusAspectRatio=2.19283709218806m};
                typeof(Launcher).GetMethod("Fill",flags).Invoke(form,new object[]{original});
                var width=(PercentSetting)typeof(Launcher).GetField("focusWidth",flags).GetValue(form);
                var height=(PercentSetting)typeof(Launcher).GetField("focusHeight",flags).GetValue(form);
                Check(Math.Abs(width.Value-50.435253m)<.00001m,"Legacy width was not displayed in equal-axis units");
                var unchanged=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(Math.Abs(unchanged.FocusWidthPercent-23)<.000001m,"Opening settings changed the existing box");
                width.Value=height.Value=50;
                var normalized=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(Math.Abs(normalized.FocusWidthPercent-22.801511m)<.00001m&&normalized.FocusHeightPercent==50,"Equal UI axes did not reach the provider correctly");
                Check(normalized.FocusAspectRatio==original.FocusAspectRatio,"Calibration lost on save");
                var vector=new LauncherSettings {Backend="VectorXR",FoveationMode="fixed",RenderProfiling=true,RenderProfileSeconds=180,VectorXrManifest=@"E:\VectorXR\layer.json",QuadViewsManifest=@"E:\QVFR\layer.json"};
                typeof(Launcher).GetMethod("Fill",flags).Invoke(form,new object[]{vector});
                var vectorValues=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(vectorValues.RenderProfiling,"Profiling GUI setting lost");
                ((CheckBox)Named(form,"StereoDrawAudit")).Checked=true;
                Check(((LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null)).StereoDrawAudit,"Stereo audit GUI preference lost");
                Check(Named(form,"StereoShaderCapture").Enabled,"Shader export control disabled with audit on");
                ((CheckBox)Named(form,"StereoShaderCapture")).Checked=true;
                ((CheckBox)Named(form,"StereoPairProof")).Checked=true;((CheckBox)Named(form,"CpuUploadReuse")).Checked=true;
                var prototypeValues=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(prototypeValues.StereoPairProof&&prototypeValues.CpuUploadReuse,"Prototype GUI preferences lost");
                Check(((LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null)).StereoShaderCapture,"Shader export GUI preference lost");
                ((CheckBox)Named(form,"StereoDrawAudit")).Checked=false;
                Check(!Named(form,"StereoShaderCapture").Enabled,"Shader export enabled with audit off");
                Check(!Named(form,"StereoPairProof").Enabled,"Paired proof enabled with audit off");
                ((CheckBox)Named(form,"StereoDrawAudit")).Checked=true;
                Check(vectorValues.RenderProfileSeconds==180&&Named(form,"RenderProfileSeconds").Enabled,"Capture length GUI setting lost/disabled");
                ((CheckBox)Named(form,"DiagnosticsEnabled")).Checked=false;
                var quietValues=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(!quietValues.DiagnosticsEnabled&&!Named(form,"RenderProfileSeconds").Enabled,"Diagnostics-off did not disable captures");
                Check(!Named(form,"StereoDrawAudit").Enabled,"Diagnostics-off did not disable audit control");
                Check(!Named(form,"StereoShaderCapture").Enabled,"Diagnostics-off did not disable shader export control");
                Check(!Named(form,"StereoPairProof").Enabled&&Named(form,"CpuUploadReuse").Enabled,"Master diagnostics switch did not preserve rendering while disabling proof");
                ((CheckBox)Named(form,"DiagnosticsEnabled")).Checked=true;
                Check(Named(form,"RenderProfileSeconds").Enabled,"Re-enabling diagnostics lost saved capture preference");
                Check(vectorValues.Backend=="VectorXR"&&vectorValues.VectorXrManifest==vector.VectorXrManifest&&vectorValues.QuadViewsManifest==vector.QuadViewsManifest,"VectorXR GUI settings roundtrip");
                Check(!Named(form,"OuterResolution").Enabled&&!Named(form,"InnerResolution").Enabled,"VectorXR misleadingly enables QVFR resolution controls");
                Check(Named(form,"VectorXrManifest").Enabled&&!Named(form,"QvfrManifest").Enabled,"Wrong manifest picker enabled for VectorXR");
                ((ComboBox)Named(form,"ProviderSelector")).SelectedIndex=1;
                var switched=(LauncherSettings)typeof(Launcher).GetMethod("Values",flags).Invoke(form,null);
                Check(switched.VectorXrManifest==vector.VectorXrManifest&&switched.QuadViewsManifest==vector.QuadViewsManifest,"Switching provider lost a manifest path");
            }
            foreach(float scale in new[]{1f,1.5f,2f})foreach(bool minimum in new[]{false,true}) {
                using(var form=new Launcher()) {
                    form.Opacity=0;form.ShowInTaskbar=false;form.StartPosition=FormStartPosition.Manual;form.Location=new Point(-16000,-16000);
                    if(minimum)form.Size=form.MinimumSize;
                    form.Show();Settle(form);
                    Check(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),new IntPtr(-4)),"Actual thread DPI awareness is not PerMonitorV2");
                    Check(form.Icon!=null&&form.Icon.Width>0,"Launcher icon is missing");
                    Check(typeof(Launcher).Assembly.GetManifestResourceInfo("LmuQuadView.Icon")!=null,"Custom icon resource is missing");
                    Console.WriteLine("Actual hidden HWND DPI="+GetDpiForWindow(form.Handle)+"; thread awareness=PerMonitorV2. Next case adds SIMULATED geometry/font scaling relative to this DPI; it is not a physical Windows display-scale test.");
                    SimulateScale(form,scale);
                    string stem="SIMULATED-"+(int)(scale*100)+"pct-"+(minimum?"minimum":"default");
                    Page(form,"RenderingPage","RenderingNav",output,stem);
                    Page(form,"SetupPage","SetupNav",output,stem);
                    Page(form,"BenchmarkPage","BenchmarkNav",output,stem);
                    ((ComboBox)Named(form,"ProviderSelector")).SelectedIndex=3;
                    ((Button)Named(form,"RenderingNav")).PerformClick();
                    ((DarkScrollBar)Named(Named(form,"RenderingPage"),"PageScrollBar")).Value=0;Settle(form);
                    FullyVisible(Named(form,"VrsPeripheralRate"));
                    Screenshot(form,Path.Combine(output,stem+"-VRS-top.png"));
                    form.Close();
                }
            }
            Console.WriteLine("GUI layout smoke tests passed. Physical-monitor DPI transition testing remains separate.");return 0;
        } catch(Exception error) { Console.Error.WriteLine(error);return 1; }
    }
}
