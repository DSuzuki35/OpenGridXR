using System;
using System.Globalization;
using System.IO;
using System.Text.RegularExpressions;

namespace LmuQuadViewLauncher {
    // Shared QVFR fractions cannot independently compensate different left/right
    // aspect ratios. Only accept matching stereo geometry (within 1%).
    public sealed class FocusGeometry {
        public readonly decimal Aspect;
        public FocusGeometry(decimal aspect) {
            if(aspect<0.1m||aspect>10m)throw new ArgumentOutOfRangeException("aspect");
            Aspect=aspect;
        }
        public decimal WidthScale { get { return Math.Max(1m,Aspect); } }
        public decimal HeightScale { get { return Math.Max(1m,1m/Aspect); } }
        public decimal DisplayWidth(decimal provider) { return provider*WidthScale; }
        public decimal DisplayHeight(decimal provider) { return provider*HeightScale; }
        // Remove decimal reciprocal noise before the exact buffer-fit guards.
        // Eight decimal places far exceed QVFR profile precision (0.01%).
        public decimal ProviderWidth(decimal display) { return Decimal.Round(display/WidthScale,8); }
        public decimal ProviderHeight(decimal display) { return Decimal.Round(display/HeightScale,8); }
        public static FocusGeometry FromLog(string text) {
            var aspects=new double[2];var found=new bool[2];
            foreach(Match match in Regex.Matches(text??"",@"Raw runtime locate FOV view=([01]) left=(\S+) right=(\S+) up=(\S+) down=(\S+) runtimeSettings=1")) {
                int eye=Int32.Parse(match.Groups[1].Value,CultureInfo.InvariantCulture);
                var angles=new double[4];
                for(int i=0;i<4;++i)if(!Double.TryParse(match.Groups[i+2].Value,NumberStyles.Float,CultureInfo.InvariantCulture,out angles[i])||
                    Double.IsNaN(angles[i])||Double.IsInfinity(angles[i])||Math.Abs(angles[i])>=Math.PI/2)return null;
                if(angles[0]>=angles[1]||angles[3]>=angles[2])return null;
                aspects[eye]=(Math.Tan(angles[1])-Math.Tan(angles[0]))/(Math.Tan(angles[2])-Math.Tan(angles[3]));
                found[eye]=true;
            }
            if(!found[0]||!found[1]||Math.Abs(aspects[0]/aspects[1]-1)>0.01)return null;
            double aspect=(aspects[0]+aspects[1])/2;
            return aspect>=0.1&&aspect<=10?new FocusGeometry((decimal)aspect):null;
        }
        public static FocusGeometry Latest(string root) {
            string directory=Path.Combine(root,"logs");if(!Directory.Exists(directory))return null;
            var files=new DirectoryInfo(directory).GetFiles("quad-*.log");
            Array.Sort(files,delegate(FileInfo a,FileInfo b){return b.LastWriteTimeUtc.CompareTo(a.LastWriteTimeUtc);});
            // Do not silently substitute an older headset's measurement.
            if(files.Length==0)return null;
            try {
                using(var file=new FileStream(files[0].FullName,FileMode.Open,FileAccess.Read,FileShare.ReadWrite))
                using(var reader=new StreamReader(file)) {
                    // The one-time FOV record is near startup. Long drives need
                    // not load an entire growing frame log into the launcher.
                    var prefix=new char[256*1024];int count=reader.ReadBlock(prefix,0,prefix.Length);
                    return FromLog(new string(prefix,0,count));
                }
            } catch(IOException) { return null; } catch(UnauthorizedAccessException) { return null; }
        }
        public static FocusGeometry LoadCalibration(string root,decimal savedAspect) {
            string directory=Path.Combine(root,"logs");
            if(Directory.Exists(directory)&&Directory.GetFiles(directory,"quad-*.log").Length!=0)return Latest(root);
            return savedAspect==0?null:new FocusGeometry(savedAspect);
        }
    }
}
