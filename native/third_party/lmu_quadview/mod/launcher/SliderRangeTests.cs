using System;
using System.Reflection;
using System.Windows.Forms;
using System.Diagnostics;
using LmuQuadViewLauncher;

class SliderRangeTests {
    const BindingFlags Hidden=BindingFlags.Instance|BindingFlags.NonPublic;
    static void Check(bool good,string message){if(!good)throw new Exception(message);}
    static PercentSetting Setting(Launcher form,string name){return (PercentSetting)typeof(Launcher).GetField(name,Hidden).GetValue(form);}
    static void Fill(Launcher form,LauncherSettings settings){typeof(Launcher).GetMethod("Fill",Hidden).Invoke(form,new object[]{settings});}
    static LauncherSettings Values(Launcher form){return (LauncherSettings)typeof(Launcher).GetMethod("Values",Hidden).Invoke(form,null);}
    static void WatchLayouts(Control root,Action<string> changed){root.Layout+=delegate(object sender,LayoutEventArgs e){changed(root.GetType().Name+"/"+root.Name+":"+e.AffectedProperty);};foreach(Control child in root.Controls)WatchLayouts(child,changed);}
    static void DragRangeRegression(){
        using(var form=new Launcher()){
            Fill(form,new LauncherSettings{Backend="QVFR",OuterPercent=25,InnerPercent=100,FocusWidthPercent=50,FocusHeightPercent=50});
            form.Opacity=0;form.ShowInTaskbar=false;form.StartPosition=FormStartPosition.Manual;form.Location=new System.Drawing.Point(-16000,-16000);
            form.Show();Application.DoEvents();
            var inner=Setting(form,"inner");var outer=Setting(form,"outer");
            int innerCaptions=0,outerCaptions=0,layouts=0;
            inner.Controls.Find("AllowedRange",true)[0].TextChanged+=delegate{++innerCaptions;};
            outer.Controls.Find("AllowedRange",true)[0].TextChanged+=delegate{++outerCaptions;};
            var groups=new System.Collections.Generic.Dictionary<string,int>();
            WatchLayouts(form,delegate(string group){++layouts;if(!groups.ContainsKey(group))groups[group]=0;++groups[group];});
            var slider=(PurpleSlider)typeof(PercentSetting).GetField("slider",Hidden).GetValue(inner);
            var watch=Stopwatch.StartNew();
            for(int i=1;i<=80;++i){slider.Value=100+i*.1m;Application.DoEvents();}
            watch.Stop();
            Console.WriteLine("Slider drag regression: 80 edits, inner captions="+innerCaptions+", outer captions="+outerCaptions+", layouts="+layouts+", elapsed_ms="+watch.Elapsed.TotalMilliseconds.ToString("F1"));
            foreach(var group in groups)Console.WriteLine("  "+group.Key+"="+group.Value);
            Check(innerCaptions==0,"Dragging inner needlessly resets its unchanged allowed range, forcing layout on every input");
            Check(outerCaptions<=80,"Dragging inner shows temporary full-domain outer bounds between real updates");
            Check(layouts==0,"Changing values and legal bounds relayouts fixed-size slider rows");
            Check(inner.Value==108&&outer.Value==25,"Drag lost values while updating legal ranges");
        }
    }
    [STAThread] static void Main(){
        DragRangeRegression();
        using(var form=new Launcher()){
            Fill(form,new LauncherSettings{Backend="QVFR"});
            var outer=Setting(form,"outer");var inner=Setting(form,"inner");
            var width=Setting(form,"focusWidth");var height=Setting(form,"focusHeight");
            Check(outer.RangeText=="Allowed 10–50 %"&&outer.Maximum==50&&inner.Minimum==50,"Allowed range caption or numeric bounds missing");
            var outerSlider=typeof(PercentSetting).GetField("slider",Hidden).GetValue(outer);
            Check(Convert.ToDecimal(outerSlider.GetType().GetProperty("Maximum").GetValue(outerSlider,null))==400&&
                Convert.ToDecimal(outerSlider.GetType().GetProperty("AllowedMaximum").GetValue(outerSlider,null))==50,"Slider hid the unavailable part of its full scale");
            outer.Value=400;
            Check(outer.Value==50,"Outer must stop at 50% for inner 100% and focus 50/50; got "+outer.Value);
            inner.Value=25;Check(inner.Value==100,"Inner crossed its live minimum");
            outer.Value=10;inner.Value=25;outer.Value=400;
            Check(outer.Value==12.5m,"Lower inner did not reduce the outer maximum");
            width.Value=10;Check(width.Value==50,"Focus width crossed required buffer coverage");
            outer.Value=10;width.Value=10;height.Value=10;
            Check(width.Value==40&&height.Value==40,"Focus minimum did not follow outer/inner");
            Values(form);

            var calibrated=new LauncherSettings{Backend="QVFR",OuterPercent=10,InnerPercent=208,FocusWidthPercent=23,FocusHeightPercent=50,FocusAspectRatio=2.19283709218806m};
            Fill(form,calibrated);var loaded=Values(form);
            Check(loaded.OuterPercent==10&&loaded.InnerPercent==208&&loaded.FocusWidthPercent==23,"Loading calibrated settings changed valid values");
            outer.Value=400;Check(outer.Value==47.84m,"Outer used displayed rather than provider focus units");
            width.Value=10;Values(form);inner.Value=25;Values(form);
            Check(inner.Value>=208,"Calibrated inner minimum was too small");
            // Complete loads must clear constraints from the previous tuple.
            Fill(form,new LauncherSettings{Backend="QVFR",OuterPercent=40,InnerPercent=400,FocusWidthPercent=10,FocusHeightPercent=10});
            Check(Values(form).OuterPercent==40,"Old limits clipped a valid loaded profile");
            foreach(var provider in new[]{"Native","VectorXR","VRS"}){
                Fill(form,new LauncherSettings{Backend=provider,OuterPercent=400,InnerPercent=25,FocusWidthPercent=10,FocusHeightPercent=10});
                Check(outer.Value==400&&inner.Value==25,"QVFR limits leaked into "+provider);
            }
            ((ComboBox)form.Controls.Find("ProviderSelector",true)[0]).SelectedIndex=1;
            var switched=Values(form);
            Check(switched.OuterPercent==10&&switched.InnerPercent==100,"Provider switch left an impossible QVFR tuple");
            foreach(var aspect in new[]{.1m,.456m,1m,2.19283709218806m,10m}){
                Fill(form,new LauncherSettings{Backend="QVFR",OuterPercent=10,InnerPercent=200,FocusWidthPercent=10,FocusHeightPercent=10,FocusAspectRatio=aspect});
                outer.Value=400;inner.Value=25;width.Value=0;height.Value=0;Values(form);
                Check(outer.Value==20&&inner.Value==200,"Extreme calibration broke the fit constraint");
            }
            Fill(form,new LauncherSettings{Backend="QVFR",OuterPercent=360,InnerPercent=400,FocusWidthPercent=90,FocusHeightPercent=90});
            Check(width.Minimum==width.Maximum&&height.Minimum==height.Maximum,"Single-value allowed focus range lost");
            width.Value=0;Values(form);
        }
        using(var setting=new PercentSetting("Test",10,100)){
            Check(setting.RangeText=="Allowed 10–100 %","Initial fixed bounds must initialize the allowed-range caption");
            Check(setting.Controls.Find("AllowedRange",true)[0].AccessibleName==setting.RangeText,"Allowed range is missing from accessibility text");
            setting.SetRange(12.25m,87.75m);
            setting.Value=400;Check(setting.Value==87.75m,"Numeric maximum not enforced");
            var slider=typeof(PercentSetting).GetField("slider",Hidden).GetValue(setting);
            slider.GetType().GetProperty("Value").SetValue(slider,400m,null);
            Check(setting.Value==87.75m,"Slider maximum differs from numeric maximum");
            slider.GetType().GetMethod("OnKeyDown",Hidden).Invoke(slider,new object[]{new KeyEventArgs(Keys.Home)});
            Check(setting.Value==12.25m,"Slider Home crossed fractional minimum");
        }
        Console.WriteLine("Slider range tests passed: live bounds, calibrated units, profile loading, provider switching, fractional input and keyboard.");
    }
}
