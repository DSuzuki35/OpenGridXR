using System;
using System.Globalization;
using System.IO;
using System.Threading;
using LmuQuadViewLauncher;
class FocusGeometryTests {
    static void Check(bool value,string message) { if(!value)throw new Exception(message); }
    static void Near(decimal actual,decimal expected,decimal tolerance,string message) { Check(Math.Abs(actual-expected)<tolerance,message); }
    static string Eye(int eye,string left,string right,string up,string down) {
        return "08:00 Raw runtime locate FOV view="+eye+" left="+left+" right="+right+" up="+up+" down="+down+" runtimeSettings=1\n";
    }
    static void Main() {
        Thread.CurrentThread.CurrentCulture=CultureInfo.GetCultureInfo("de-DE");
        var measured=FocusGeometry.FromLog(Eye(0,"-1.06798363","0.855053425","0.594999373","-0.594999373")+Eye(1,"-0.855053425","1.06798363","0.594999373","-0.594999373"));
        Check(measured!=null,"Measured stereo FOV rejected");
        Near(measured.ProviderWidth(50),22.801511m,.00001m,"50/50 did not compensate wide headset FOV");
        Near(measured.ProviderHeight(50),50,.00001m,"Short axis changed");
        Near(measured.DisplayWidth(23),50.435253m,.00001m,"Old 23/50 focus area changed");
        Near(measured.ProviderWidth(measured.DisplayWidth(23)),23,.00000001m,"Migration did not preserve provider width");
        var s=new LauncherSettings {Backend="QVFR",OuterPercent=10,InnerPercent=208,FocusWidthPercent=measured.ProviderWidth(50),FocusHeightPercent=measured.ProviderHeight(50)};
        Check(QvfrSettings.ProfileText("",s).Contains("horizontal_focus_section=0.228\nvertical_focus_section=0.5\n"),"Provider did not receive normalized values");
        var tall=FocusGeometry.FromLog(Eye(0,"-0.463647609","0.463647609","0.785398163","-0.785398163")+Eye(1,"-0.463647609","0.463647609","0.785398163","-0.785398163"));
        Near(tall.ProviderWidth(50),50,.00001m,"Tall view width"); Near(tall.ProviderHeight(50),25,.00001m,"Tall view height");
        var boundary=new FocusGeometry(.456m);
        var unchanged=new LauncherSettings {Backend="QVFR",OuterPercent=50,InnerPercent=100,FocusWidthPercent=90,FocusHeightPercent=boundary.ProviderHeight(boundary.DisplayHeight(50))};
        QvfrSettings.Validate(unchanged);
        var square=FocusGeometry.FromLog(Eye(0,"-0.785398163","0.785398163","0.785398163","-0.785398163")+Eye(1,"-0.785398163","0.785398163","0.785398163","-0.785398163"));
        Near(square.ProviderWidth(50),50,.00001m,"Square view changed");
        Check(FocusGeometry.FromLog("")==null,"Missing FOV accepted");
        Check(FocusGeometry.FromLog(Eye(0,"-1","1","0.5","-0.5"))==null,"One eye accepted");
        Check(FocusGeometry.FromLog(Eye(0,"NaN","1","0.5","-0.5")+Eye(1,"-1","1","0.5","-0.5"))==null,"Invalid FOV accepted");
        Check(FocusGeometry.FromLog(Eye(0,"-2","1","0.5","-0.5")+Eye(1,"-1","1","0.5","-0.5"))==null,"Singular FOV accepted");
        Check(FocusGeometry.FromLog(Eye(0,"1","-1","0.5","-0.5")+Eye(1,"-1","1","0.5","-0.5"))==null,"Reversed FOV accepted");
        Check(FocusGeometry.FromLog(Eye(0,"-1","1","0.5","-0.5")+Eye(1,"-0.5","0.5","1","-1"))==null,"Incompatible eyes accepted");
        string root=Path.Combine(Path.GetTempPath(),"focus-geometry-"+Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root,"logs"));
        try {
            Check(FocusGeometry.LoadCalibration(root,2)!=null,"Saved calibration lost without new measurement");
            File.WriteAllText(Path.Combine(root,"logs","quad-invalid.log"),"incomplete new headset measurement");
            Check(FocusGeometry.LoadCalibration(root,2)==null,"Old headset calibration reused after incompatible latest run");
        } finally { Directory.Delete(root,true); }
        Console.WriteLine("Focus geometry tests passed: measured FOV, equal axes, migration, provider profile, tall/square views, invalid data.");
    }
}
