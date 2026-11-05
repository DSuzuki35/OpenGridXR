using System;
using System.Collections.Generic;
using LmuQuadViewLauncher;
class JoystickInputTests
{
    sealed class Source : IJoystickButtonSource {
        public IList<JoystickButtons> States=new List<JoystickButtons>();
        public IList<JoystickButtons> Read() {return States;}
        public void Dispose() {}
    }
    static void Check(bool good,string message) {if(!good)throw new Exception(message);}
    static JoystickButtons State(Guid id,bool held) {var buttons=new bool[128];buttons[127]=held;return new JoystickButtons {Device=id,Name="Wheel",Buttons=buttons};}
    static void Main() {
        var source=new Source();var a=Guid.NewGuid();var b=Guid.NewGuid();
        using(var input=new JoystickInput(source)) {
            input.Visibility=new JoystickBinding {Device=a,Button=127};input.Recording=new JoystickBinding {Device=b,Button=127};
            Check(input.Visibility.ToString().Contains("button 128"),"Human-readable button number must be one-based");
            int visible=0,recording=0;input.VisibilityPressed+=delegate {visible++;};input.RecordingPressed+=delegate {recording++;};
            source.States=new[]{State(a,true),State(b,false)};input.Tick();input.Tick();Check(visible==0,"Initial held button synthesized an edge");
            source.States=new[]{State(a,false),State(b,false)};input.Tick();
            source.States=new[]{State(a,true),State(b,false)};input.Tick();input.Tick();Check(visible==1&&recording==0,"Rising edge, hold suppression or device isolation failed");
            source.States=new[]{State(a,true),State(b,true)};input.Tick();Check(visible==1&&recording==1,"Distinct devices with same button index confused");
            source.States=new JoystickButtons[0];input.Tick();source.States=new[]{State(a,true)};input.Tick();Check(visible==1,"Reconnect held button toggled");
            source.States=new[]{State(a,false)};input.Tick();source.States=new[]{State(a,true)};input.Tick();Check(visible==2,"Reconnect did not rearm after release");
            JoystickBinding assigned=null;input.Listen(delegate(JoystickBinding binding) {assigned=binding;});input.Tick();Check(assigned==null,"Listen accepted held button");
            source.States=new[]{State(a,false)};input.Tick();source.States=new[]{State(a,true)};input.Tick();
            Check(assigned!=null&&assigned.Device==a&&assigned.Button==127&&assigned.Name=="Wheel","Guided listen did not capture correct device/button/name");
            input.Tick();Check(visible==2,"Assigning button also fired command");
            input.Listen(delegate(JoystickBinding binding) {throw new Exception("Cancelled callback fired");});input.CancelListen();Check(!input.Listening,"Cancel failed");
            assigned=null;input.Listen(delegate(JoystickBinding binding) {assigned=binding;});
            source.States=new JoystickButtons[0];input.Tick();Check(input.DeviceCount==0&&input.ListenStatus.Contains("No joystick connected"),"Missing device guidance absent");source.States=new[]{State(b,true)};input.Tick();Check(assigned==null,"Listen accepted held reconnect");
            source.States=new[]{State(b,false)};input.Tick();source.States=new[]{State(b,true)};input.Tick();Check(assigned!=null&&assigned.Device==b,"Listen failed after reconnect release");
        }
        using(var input=new JoystickInput(source)) {
            input.Visibility=new JoystickBinding {Device=a,Button=127};int count=0;input.VisibilityPressed+=delegate {count++;};
            source.States=new[]{State(a,false)};input.Tick();var reconnected=State(a,true);reconnected.ConnectionEpoch=1;
            source.States=new[]{reconnected};input.Tick();Check(count==0,"Missed disconnect snapshot synthesized an edge");
        }
        Console.WriteLine("Joystick input tests passed (injected devices; hardware not exercised).");
    }
}
