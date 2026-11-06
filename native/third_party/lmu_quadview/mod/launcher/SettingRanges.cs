using System;

namespace LmuQuadViewLauncher {
    // QVFR stores linear per-axis scales. Calibrated display units are converted
    // by the UI before entering these calculations, just like Validate().
    internal sealed class SettingRanges {
        public readonly decimal OuterMaximum,InnerMinimum,FocusWidthMinimum,FocusHeightMinimum;
        static decimal Up(decimal value){return Decimal.Ceiling(value*100000000m)/100000000m;}
        internal static decimal OuterCapacity(LauncherSettings s){return s.InnerPercent*Math.Min(s.FocusWidthPercent,s.FocusHeightPercent)/100m;}
        public SettingRanges(LauncherSettings s){
            OuterMaximum=400;InnerMinimum=25;FocusWidthMinimum=FocusHeightMinimum=10;
            if(s.Backend!="QVFR")return;
            OuterMaximum=Math.Min(400,OuterCapacity(s));
            InnerMinimum=Math.Max(25,Up(s.OuterPercent*100m/Math.Min(s.FocusWidthPercent,s.FocusHeightPercent)));
            decimal focus=Math.Max(10,Up(s.OuterPercent*100m/s.InnerPercent));
            FocusWidthMinimum=FocusHeightMinimum=focus;
            // Keep exact valid imported fractions at the boundary. Interactive
            // limits otherwise round inward to the provider's eight digits.
            if(s.OuterPercent<=OuterCapacity(s))InnerMinimum=Math.Min(InnerMinimum,s.InnerPercent);
            if(s.OuterPercent<=s.InnerPercent*s.FocusWidthPercent/100m)FocusWidthMinimum=Math.Min(focus,s.FocusWidthPercent);
            if(s.OuterPercent<=s.InnerPercent*s.FocusHeightPercent/100m)FocusHeightMinimum=Math.Min(focus,s.FocusHeightPercent);
        }
        public static void NormalizeForProvider(LauncherSettings s){
            if(s.Backend!="QVFR")return;
            var focus=Math.Min(s.FocusWidthPercent,s.FocusHeightPercent);
            if(OuterCapacity(s)<10)s.InnerPercent=Math.Max(s.InnerPercent,Up(1000m/focus));
            s.OuterPercent=Math.Min(s.OuterPercent,OuterCapacity(s));
        }
    }
}
