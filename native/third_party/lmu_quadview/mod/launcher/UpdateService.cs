using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace LmuQuadViewLauncher {
    public sealed class UpdateCandidate {
        public Version Version {get;private set;}
        public Uri DownloadUri {get;private set;}
        public Uri ReleaseUri {get;private set;}
        public string Sha256 {get;private set;}
        public long Size {get;private set;}
        public string FileName {get;private set;}
        internal UpdateCandidate(Version version,Uri download,Uri release,string hash,long size,string name) {
            Version=version;DownloadUri=download;ReleaseUri=release;Sha256=hash;Size=size;FileName=name;
        }
    }
    public sealed class DownloadedUpdate {
        public string Path {get;private set;}
        public UpdateCandidate Candidate {get;private set;}
        internal DownloadedUpdate(string path,UpdateCandidate candidate){Path=path;Candidate=candidate;}
    }

    // These bounded synchronous operations belong in BackgroundWorker.DoWork.
    // Check may run automatically. Download and StartInstall require separate
    // explicit user actions. Close the launcher only after StartInstall returns.
    public static class UpdateService {
        public const string LatestReleaseEndpoint="https://api.github.com/repos/DSuzuki35/LMU-Quad-View-Mod/releases/latest";
        const string Repository="https://github.com/DSuzuki35/LMU-Quad-View-Mod/releases/";
        const int MetadataLimit=2097152;
        const long InstallerLimit=536870912;
        const int RequestTimeout=30000;
        const int DownloadTimeout=600000;
        public static Version CurrentVersion {get{return typeof(UpdateService).Assembly.GetName().Version;}}
        static Version Comparable(Version version) {
            if(version==null)throw new ArgumentNullException("version");
            return new Version(version.Major,version.Minor,Math.Max(0,version.Build),Math.Max(0,version.Revision));
        }
        static string Text(IDictionary<string,object> value,string key){object field;return value.TryGetValue(key,out field)?field as string:null;}
        static bool False(IDictionary<string,object> value,string key){object field;return value.TryGetValue(key,out field)&&field is bool&&!(bool)field;}
        static bool ExactUri(string value,string expected,out Uri uri) {
            return Uri.TryCreate(value,UriKind.Absolute,out uri)&&uri.Scheme==Uri.UriSchemeHttps&&uri.IsDefaultPort&&
                uri.UserInfo.Length==0&&uri.Query.Length==0&&uri.Fragment.Length==0&&String.Equals(value,expected,StringComparison.Ordinal);
        }
        // Pure, fail-closed metadata parsing. Stable tags are vMAJOR.MINOR.PATCH
        // or MAJOR.MINOR.PATCH; no leading zeros, build labels or prereleases.
        public static UpdateCandidate ParseRelease(string json,Version currentVersion) {
            Comparable(currentVersion);
            if(String.IsNullOrWhiteSpace(json)||json.Length>MetadataLimit)return null;
            try {
                var serializer=new JavaScriptSerializer{MaxJsonLength=MetadataLimit,RecursionLimit=32};
                var release=serializer.DeserializeObject(json) as IDictionary<string,object>;
                if(release==null||!False(release,"draft")||!False(release,"prerelease"))return null;
                string tag=Text(release,"tag_name");
                if(tag==null||!Regex.IsMatch(tag,@"^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\z"))return null;
                string versionText=tag[0]=='v'?tag.Substring(1):tag;Version version;
                if(!Version.TryParse(versionText,out version)||Comparable(version)<=Comparable(currentVersion))return null;
                Uri releaseUri;
                if(!ExactUri(Text(release,"html_url"),Repository+"tag/"+tag,out releaseUri))return null;
                string name="LMU-Quad-View-Mod-"+versionText+"-Setup-x64.exe";
                object assetsObject;
                if(!release.TryGetValue("assets",out assetsObject))return null;
                var assets=assetsObject as object[];if(assets==null||assets.Length>100)return null;
                UpdateCandidate candidate=null;
                foreach(object item in assets) {
                    var asset=item as IDictionary<string,object>;
                    if(asset==null||Text(asset,"name")!=name)continue;
                    if(candidate!=null)return null; // ambiguous installer asset
                    Uri download;
                    if(!ExactUri(Text(asset,"browser_download_url"),Repository+"download/"+tag+"/"+name,out download))return null;
                    string digest=Text(asset,"digest");object sizeValue;
                    if(digest==null||!Regex.IsMatch(digest,@"^sha256:[a-fA-F0-9]{64}\z")||!asset.TryGetValue("size",out sizeValue))return null;
                    if(!(sizeValue is int)&&!(sizeValue is long))return null;
                    long size=Convert.ToInt64(sizeValue);
                    if(size<=0||size>InstallerLimit)return null;
                    candidate=new UpdateCandidate(version,download,releaseUri,digest.Substring(7).ToLowerInvariant(),size,name);
                }
                return candidate;
            }catch(ArgumentException){return null;}catch(InvalidOperationException){return null;}catch(OverflowException){return null;}
        }
        static HttpWebRequest Request(Uri uri) {
            var request=(HttpWebRequest)WebRequest.Create(uri);
            request.Method="GET";request.UserAgent="LMU-QuadView-Launcher/"+CurrentVersion;
            request.Accept="application/vnd.github+json";request.Timeout=RequestTimeout;request.ReadWriteTimeout=RequestTimeout;
            request.AllowAutoRedirect=false;request.AutomaticDecompression=DecompressionMethods.None;
            return request;
        }
        public static UpdateCandidate Check(Version currentVersion) {
            Comparable(currentVersion);
            var request=Request(new Uri(LatestReleaseEndpoint));
            request.Headers.Add("X-GitHub-Api-Version","2022-11-28");
            try {
                using(var response=(HttpWebResponse)request.GetResponse()) {
                    if(response.StatusCode!=HttpStatusCode.OK)throw new InvalidDataException("GitHub did not return release metadata.");
                    if(response.ContentLength>MetadataLimit)throw new InvalidDataException("Release metadata is too large.");
                    using(var stream=response.GetResponseStream())using(var memory=new MemoryStream()) {
                        var watch=Stopwatch.StartNew();var bytes=new byte[8192];int count;
                        while((count=stream.Read(bytes,0,bytes.Length))>0) {
                            if(watch.ElapsedMilliseconds>RequestTimeout||memory.Length+count>MetadataLimit)throw new InvalidDataException("Release metadata exceeded its time or size limit.");
                            memory.Write(bytes,0,count);
                        }
                        return ParseRelease(new UTF8Encoding(false,true).GetString(memory.ToArray()),currentVersion);
                    }
                }
            }catch(WebException error) {
                var response=error.Response as HttpWebResponse;
                if(response!=null){using(response){if(response.StatusCode==HttpStatusCode.NotFound)return null;}}
                throw;
            }
        }
        static void NoGame() {
            var games=Process.GetProcessesByName("Le Mans Ultimate");bool running=games.Length>0;
            foreach(var game in games)game.Dispose();
            if(running)throw new InvalidOperationException("Close LMU before installing an update.");
        }
        static string UpdatesRoot {get{return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"LMUQuadView","updates");}}
        static void PlainPath(string path) {
            for(var current=new DirectoryInfo(path);current!=null;current=current.Parent)
                if(current.Exists&&(current.Attributes&FileAttributes.ReparsePoint)!=0)throw new IOException("The update directory must not contain links.");
        }
        internal static void CopyVerified(Stream source,Stream destination,long size,string sha256) {
            if(size<=0||size>InstallerLimit||sha256==null||!Regex.IsMatch(sha256,@"^[a-fA-F0-9]{64}\z"))throw new InvalidDataException("Invalid installer verification data.");
            using(var hash=SHA256.Create()) {
                var watch=Stopwatch.StartNew();var buffer=new byte[65536];long total=0;int count;
                while((count=source.Read(buffer,0,buffer.Length))>0) {
                    total+=count;
                    if(total>size||watch.ElapsedMilliseconds>DownloadTimeout)throw new InvalidDataException("Installer exceeded its time or size limit.");
                    hash.TransformBlock(buffer,0,count,buffer,0);destination.Write(buffer,0,count);
                }
                hash.TransformFinalBlock(new byte[0],0,0);
                string actual=BitConverter.ToString(hash.Hash).Replace("-","");
                if(total!=size||!String.Equals(actual,sha256,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("The downloaded installer failed its SHA256 or size check.");
            }
        }
        static HttpWebResponse InstallerResponse(Uri uri) {
            for(int redirects=0;redirects<5;redirects++) {
                var request=Request(uri);request.Accept="application/octet-stream";
                var response=(HttpWebResponse)request.GetResponse();
                if(response.StatusCode==HttpStatusCode.OK)return response;
                int status=(int)response.StatusCode;string location=response.Headers[HttpResponseHeader.Location];response.Dispose();
                Uri next;
                if((status!=301&&status!=302&&status!=303&&status!=307&&status!=308)||!Uri.TryCreate(uri,location,out next)||
                    next.Scheme!=Uri.UriSchemeHttps||!next.IsDefaultPort||next.UserInfo.Length!=0||next.Fragment.Length!=0||
                    (next.Host!="release-assets.githubusercontent.com"&&next.Host!="objects.githubusercontent.com"))
                    throw new InvalidDataException("GitHub returned an untrusted installer redirect.");
                uri=next;
            }
            throw new InvalidDataException("Too many installer redirects.");
        }
        // 0.5.1: earlier downloads were never removed. Delete generated download folders
        // older than seven days; skip links, foreign names and anything still in use.
        internal static readonly TimeSpan DownloadRetention=TimeSpan.FromDays(7);
        internal static void PruneDownloads(string root,DateTime nowUtc) {
            try {
                foreach(var child in new DirectoryInfo(root).GetDirectories()) {
                    if(!Regex.IsMatch(child.Name,@"^[a-f0-9]{32}\z")||(child.Attributes&FileAttributes.ReparsePoint)!=0)continue;
                    if(nowUtc-child.LastWriteTimeUtc<DownloadRetention)continue;
                    try {
                        if(child.GetDirectories().Length!=0)continue;
                        var files=child.GetFiles();
                        if(files.Any(f=>(f.Attributes&FileAttributes.ReparsePoint)!=0||!f.Name.EndsWith(".exe",StringComparison.OrdinalIgnoreCase)))continue;
                        foreach(var file in files)file.Delete();
                        child.Delete(false);
                    }catch(IOException){}catch(UnauthorizedAccessException){}
                }
            }catch(IOException){}catch(UnauthorizedAccessException){}
        }
        public static DownloadedUpdate Download(UpdateCandidate candidate) {
            if(candidate==null)throw new ArgumentNullException("candidate");
            if(Comparable(candidate.Version)<=Comparable(CurrentVersion))throw new InvalidOperationException("Only a newer stable release can be installed.");
            string root=UpdatesRoot;PlainPath(root);Directory.CreateDirectory(root);PlainPath(root);
            PruneDownloads(root,DateTime.UtcNow);
            string directory=Path.Combine(root,Guid.NewGuid().ToString("N"));Directory.CreateDirectory(directory);PlainPath(directory);
            string path=Path.Combine(directory,candidate.FileName);
            try {
                using(var response=InstallerResponse(candidate.DownloadUri)) {
                    if(response.ContentLength!=-1&&response.ContentLength!=candidate.Size)throw new InvalidDataException("Installer size differs from release metadata.");
                    using(var stream=response.GetResponseStream())using(var file=new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.None)) {
                        CopyVerified(stream,file,candidate.Size,candidate.Sha256);file.Flush(true);
                    }
                }
                // Authenticode readers must not conflict with our write handle.
                // Reopen read-only, rehash, then verify while replacement is denied.
                using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)) {
                    CopyVerified(file,Stream.Null,candidate.Size,candidate.Sha256);
                    AutoStart.CheckSigned(new[]{path});
                }
                return new DownloadedUpdate(path,candidate);
            }catch {
                if(File.Exists(path))File.Delete(path);
                if(Directory.Exists(directory))Directory.Delete(directory);
                throw;
            }
        }
        public static Process StartInstall(DownloadedUpdate update) {
            if(update==null)throw new ArgumentNullException("update");NoGame();
            var candidate=update.Candidate;
            if(Comparable(candidate.Version)<=Comparable(CurrentVersion))throw new InvalidOperationException("Only a newer stable release can be installed.");
            string path=Path.GetFullPath(update.Path);string directory=Path.GetDirectoryName(path);string root=Path.GetFullPath(UpdatesRoot);
            if(!String.Equals(Path.GetDirectoryName(directory),root,StringComparison.OrdinalIgnoreCase)||
                !Regex.IsMatch(Path.GetFileName(directory),@"^[a-f0-9]{32}\z")||Path.GetFileName(path)!=candidate.FileName)
                throw new InvalidDataException("Installer is outside the update directory.");
            PlainPath(directory);
            if((File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0)throw new IOException("Installer must not be a link.");
            // FileShare.Read denies replacement, deletion and writes throughout
            // verification and CreateProcess, closing the file-swap window.
            using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)) {
                CopyVerified(file,Stream.Null,candidate.Size,candidate.Sha256);
                AutoStart.CheckSigned(new[]{path});NoGame();
                int launcherPid;using(var launcher=Process.GetCurrentProcess())launcherPid=launcher.Id;
                var start=new ProcessStartInfo{FileName=path,Arguments=BuildInstallArguments(launcherPid,AppDomain.CurrentDomain.BaseDirectory),WorkingDirectory=directory,UseShellExecute=false};
                var process=Process.Start(start);
                if(process==null)throw new InvalidOperationException("Windows did not start the installer.");
                return process;
            }
        }
        internal static string BuildInstallArguments(int launcherPid,string installDirectory) {
            if(launcherPid<=0)throw new ArgumentOutOfRangeException("launcherPid");
            if(String.IsNullOrWhiteSpace(installDirectory)||installDirectory.Length<3||installDirectory[1]!=':'||
                (installDirectory[2]!='\\'&&installDirectory[2]!='/')||!Path.IsPathRooted(installDirectory)||installDirectory.IndexOfAny(new[]{'\r','\n','\0','"'})>=0)
                throw new ArgumentException("Installer destination must be an absolute local package directory.","installDirectory");
            string full=Path.GetFullPath(installDirectory).TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar);
            if(full.Length<3||full.StartsWith(@"\\",StringComparison.Ordinal))throw new ArgumentException("Installer destination must be a local package directory.","installDirectory");
            // NSIS consumes /D= to the end, including spaces; it must be last
            // and must not be quoted. CreateProcess receives no shell command.
            return "/WAITPID="+launcherPid.ToString(System.Globalization.CultureInfo.InvariantCulture)+" /D="+full;
        }
    }
}
