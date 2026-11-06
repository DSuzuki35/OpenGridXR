using System;
using System.IO;
using System.Text;
using LmuQuadViewLauncher;

class UpdateServiceTests {
    const string Hash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const string Url="https://github.com/DSuzuki35/LMU-Quad-View-Mod/releases/download/v0.4.0/LMU-Quad-View-Mod-0.4.0-Setup-x64.exe";
    static void Check(bool value,string message){if(!value)throw new Exception(message);}
    static string Release(){return "{\"tag_name\":\"v0.4.0\",\"draft\":false,\"prerelease\":false,\"html_url\":\"https://github.com/DSuzuki35/LMU-Quad-View-Mod/releases/tag/v0.4.0\",\"assets\":[{\"name\":\"LMU-Quad-View-Mod-0.4.0-Setup-x64.exe\",\"browser_download_url\":\""+Url+"\",\"size\":3,\"digest\":\"sha256:"+Hash+"\"}]}";}
    static void Rejected(string json,string message){Check(UpdateService.ParseRelease(json,new Version(0,3,3))==null,message);}
    static void BadCopy(byte[] data,long size,string hash,string message) {
        bool rejected=false;
        try{using(var input=new MemoryStream(data))using(var output=new MemoryStream())UpdateService.CopyVerified(input,output,size,hash);}
        catch(InvalidDataException){rejected=true;}
        Check(rejected,message);
    }
    public static int Main() {
        string valid=Release();var candidate=UpdateService.ParseRelease(valid,new Version(0,3,3));
        Check(candidate!=null&&candidate.Version==new Version(0,4,0)&&candidate.DownloadUri.AbsoluteUri==Url&&candidate.Sha256==Hash&&candidate.Size==3,"Valid stable installer was not offered");
        Check(UpdateService.ParseRelease(valid,new Version(0,4,0))==null,"Equal version offered");
        Check(UpdateService.ParseRelease(valid,new Version(0,5,0))==null,"Downgrade offered");
        Check(UpdateService.ParseRelease(valid,new Version(0,4,0,1))==null,"Version revision downgrade offered");
        Rejected("{","Malformed JSON accepted");Rejected("[]","Wrong JSON shape accepted");Rejected("null","Null release accepted");
        Rejected(valid.Replace("\"draft\":false","\"draft\":true"),"Draft accepted");
        Rejected(valid.Replace("\"prerelease\":false","\"prerelease\":true"),"Prerelease accepted");
        Rejected(valid.Replace("\"prerelease\":false,",""),"Missing release stability accepted");
        foreach(string tag in new[]{"v0.4.0-beta.1","v0.4.0+local","v00.4.0","v0.04.0","v0.4.0.1","v2147483648.0.0","0.4","V0.4.0"})
            Rejected(valid.Replace("v0.4.0",tag),"Invalid stable semantic version accepted: "+tag);
        foreach(string url in new[]{Url.Replace("github.com","evil.example"),Url.Replace("github.com","github.com.evil.example"),Url.Replace("https:","http:"),Url.Replace("DSuzuki35/","attacker/"),Url.Replace("/v0.4.0/","/v0.4.1/"),Url+"?x=1",Url+"#x",Url.Replace("https://","https://evil@"),Url.Replace("Setup-x64","Setup-x86")})
            Rejected(valid.Replace(Url,url),"Untrusted download location accepted: "+url);
        Rejected(valid.Replace("\"name\":\"LMU-Quad-View-Mod-0.4.0-Setup-x64.exe\"","\"name\":\"other.exe\""),"Wrong installer name accepted");
        Rejected(valid.Replace("\"size\":3","\"size\":0"),"Empty installer accepted");
        Rejected(valid.Replace("\"size\":3","\"size\":536870913"),"Oversized installer accepted");
        Rejected(valid.Replace("\"size\":3","\"size\":3.5"),"Fractional size accepted");
        foreach(string hash in new[]{"", "sha1:"+Hash,"sha256:abc","sha256:"+new string('g',64)})
            Rejected(valid.Replace("sha256:"+Hash,hash),"Invalid SHA256 accepted");
        Rejected(new string(' ',2097153),"Oversized metadata accepted");
        Rejected(valid.Replace("sha256:"+Hash,"sha256:"+Hash+"\\n"),"Trailing newline in hash accepted");
        string arguments=UpdateService.BuildInstallArguments(1234,@"C:\Users\test\LMU Quad View\");
        Check(arguments==@"/WAITPID=1234 /D=C:\Users\test\LMU Quad View","Current package destination was not the final NSIS argument");
        foreach(string path in new[]{"relative",@"C:relative",@"\relative",@"\\server\package",@"C:\", "C:\\bad\r\n/D=other"}) {
            bool badPath=false;try{UpdateService.BuildInstallArguments(1234,path);}catch(ArgumentException){badPath=true;}
            Check(badPath,"Unsafe installer destination accepted: "+path);
        }
        byte[] abc=Encoding.ASCII.GetBytes("abc");
        using(var input=new MemoryStream(abc))using(var output=new MemoryStream()){UpdateService.CopyVerified(input,output,3,Hash);Check(Encoding.ASCII.GetString(output.ToArray())=="abc","Verified bytes changed");}
        BadCopy(abc,2,Hash,"Extra download data accepted");BadCopy(abc,4,Hash,"Truncated download accepted");BadCopy(abc,3,new string('0',64),"Corrupt download accepted");
        Console.WriteLine("Update metadata trust, stable version, size and SHA256 tests passed");return 0;
    }
}
