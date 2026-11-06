using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Principal;
using Microsoft.Win32.SafeHandles;

namespace LmuQuadViewLauncher {
    // Preserve arbitrary original owners (including SYSTEM) without changing the
    // process token or granting users additional rights on OpenXR registry keys.
    internal sealed class RegistryRestoreScope : IDisposable {
        sealed class Token : SafeHandleZeroOrMinusOneIsInvalid {
            public Token():base(true){}
            protected override bool ReleaseHandle(){return CloseHandle(handle);}
        }
        [StructLayout(LayoutKind.Sequential)]struct Luid {public uint Low;public int High;}
        [StructLayout(LayoutKind.Sequential)]struct Privileges {public uint Count;public Luid Id;public uint Attributes;}
        [DllImport("kernel32.dll",SetLastError=true)]static extern bool CloseHandle(IntPtr handle);
        [DllImport("advapi32.dll",SetLastError=true)]static extern bool DuplicateTokenEx(IntPtr token,uint access,IntPtr attributes,int level,int type,out Token copy);
        [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool LookupPrivilegeValue(string system,string name,out Luid id);
        [DllImport("advapi32.dll",SetLastError=true)]static extern bool AdjustTokenPrivileges(Token token,bool disableAll,ref Privileges state,uint length,IntPtr previous,IntPtr returned);
        WindowsImpersonationContext context;
        RegistryRestoreScope(WindowsImpersonationContext value){context=value;}
        internal static RegistryRestoreScope TryEnter() {
            using(var identity=WindowsIdentity.GetCurrent(TokenAccessLevels.Query|TokenAccessLevels.Duplicate)) {
                Token copy;
                // Query, duplicate, impersonate and adjust privileges on a copy.
                if(!DuplicateTokenEx(identity.Token,0x2e,IntPtr.Zero,2,2,out copy))throw new Win32Exception(Marshal.GetLastWin32Error());
                using(copy) {
                    Luid id;if(!LookupPrivilegeValue(null,"SeRestorePrivilege",out id))throw new Win32Exception(Marshal.GetLastWin32Error());
                    var state=new Privileges{Count=1,Id=id,Attributes=2};
                    bool okay=AdjustTokenPrivileges(copy,false,ref state,0,IntPtr.Zero,IntPtr.Zero);
                    int error=Marshal.GetLastWin32Error();
                    // Non-admin callers can still register keys they own. No token
                    // is installed when policy does not grant restore privilege.
                    if(okay&&error==1300)return null; // ERROR_NOT_ALL_ASSIGNED
                    if(!okay||error!=0)throw new Win32Exception(error);
                    return new RegistryRestoreScope(WindowsIdentity.Impersonate(copy.DangerousGetHandle()));
                }
            }
        }
        public void Dispose(){if(context!=null){context.Dispose();context=null;}}
    }
}
