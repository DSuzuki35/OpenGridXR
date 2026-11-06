#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <bcrypt.h>
#include <algorithm>
#include <vector>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include "autoload_policy.hpp"
#include "autoload_activation.hpp"

namespace {
using namespace lmuqv::autoload;
constexpr char layerName[]="XR_APILAYER_LMU_autostart";
XrNegotiateLoaderInfo loaderInfo{};
std::mutex dispatchMutex;
std::unordered_map<XrInstance,PFN_xrGetInstanceProcAddr> dispatch;
std::wstring env(const wchar_t* key) {
    wchar_t value[32768]{};auto size=GetEnvironmentVariableW(key,value,32768);
    return size&&size<32768?value:L"";
}
// Keep bootstrap diagnostics independent of the package/helper: even a skipped
// or failed preparation must explain why no normal quad log was created.
void startupLog(const std::string& message) noexcept {
    try {
        wchar_t exe[32768]{};
        auto length=GetModuleFileNameW(nullptr,exe,32768);
        if(!length||length>=32768||_wcsicmp(std::filesystem::path(exe).filename().c_str(),L"Le Mans Ultimate.exe"))return;
        auto local=env(L"LOCALAPPDATA");if(local.empty())return;
        auto directory=local+L"\\LMUQuadView";CreateDirectoryW(directory.c_str(),nullptr);
        auto path=directory+L"\\autoload-"+std::to_wstring(GetCurrentProcessId())+L".log";
        HANDLE file=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return;
        SYSTEMTIME time{};GetLocalTime(&time);char prefix[48]{};
        snprintf(prefix,sizeof(prefix),"%02u:%02u:%02u.%03u ",time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
        auto line=std::string(prefix)+message+"\r\n";DWORD written=0;
        WriteFile(file,line.data(),static_cast<DWORD>(line.size()),&written,nullptr);CloseHandle(file);
    }catch(...){} // Diagnostics cannot change OpenXR behavior.
}
bool elevated() {
    HANDLE token=nullptr;TOKEN_ELEVATION value{};DWORD size=0;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)){startupLog("token query failed error="+std::to_string(GetLastError()));return true;}
    bool ok=GetTokenInformation(token,TokenElevation,&value,sizeof(value),&size)!=FALSE;CloseHandle(token);
    return !ok||value.TokenIsElevated;
}
std::wstring initialRoot() {
    wchar_t path[32768]{};auto length=GetModuleFileNameW(nullptr,path,32768);
    if(!length||length>=32768||_wcsicmp(std::filesystem::path(path).filename().c_str(),L"Le Mans Ultimate.exe"))return {};
    if(elevated()){startupLog("skipped: elevated process or unavailable token information");return {};}
    if(env(L"XR_ENABLE_API_LAYERS").find(L"XR_APILAYER_LMU_quadview")!=std::wstring::npos){startupLog("skipped: explicit LMU adapter requested");return {};}
    if(!env(L"LMU_QV_DISABLE_AUTOLOAD").empty()){startupLog("skipped: LMU_QV_DISABLE_AUTOLOAD is set");return {};}
    if(env(L"OPENGRIDXR_LMU_INTEGRATED")==L"1"){startupLog("skipped: OPENGRIDXR_LMU_INTEGRATED=1");return {};}
    // Some protected game launches cannot see our HKCU value even though the
    // same user's launcher can. Use the per-user file written by Setup first.
    // LOCALAPPDATA access is also used by the early log and provider settings.
    auto local=env(L"LOCALAPPDATA");
    if(!local.empty()){
        auto root=activation_file(local+L"\\LMUQuadView\\autoload.ini");
        if(!root.empty()){startupLog("activation loaded from user file");return root;}
    }
    DWORD bytes=sizeof(path);
    const auto registration=RegGetValueW(HKEY_CURRENT_USER,L"Software\\LMUQuadView\\AutoStart",L"PackageRoot",RRF_RT_REG_SZ,nullptr,path,&bytes);
    if(registration!=ERROR_SUCCESS){startupLog("skipped: user activation unavailable error="+std::to_string(registration));return {};}
    std::filesystem::path root(path);
    if(!root.is_absolute())throw std::runtime_error("Automatic-start package path is invalid. Reinstall from Setup.");
    return root.wstring();
}
// Use the explicit-layer request, not LMU_QV_MODE, to recognize older launchers.
// Automatic settings persist through instance destruction/loader reloads, but
// do not contain a manifest disable switch or an explicit layer request.
const std::wstring& activeRoot() {static const auto root=initialRoot();return root;}
PFN_xrGetInstanceProcAddr lookup(XrInstance instance) {
    std::lock_guard<std::mutex> lock(dispatchMutex);auto found=dispatch.find(instance);return found==dispatch.end()?nullptr:found->second;
}
XrResult XRAPI_CALL destroy(XrInstance instance) {
    auto get=lookup(instance);if(!get)return XR_ERROR_HANDLE_INVALID;
    PFN_xrVoidFunction function=nullptr;auto result=get(instance,"xrDestroyInstance",&function);
    if(XR_FAILED(result)||!function)return XR_ERROR_FUNCTION_UNSUPPORTED;
    result=reinterpret_cast<PFN_xrDestroyInstance>(function)(instance);
    if(XR_SUCCEEDED(result)){std::lock_guard<std::mutex> lock(dispatchMutex);dispatch.erase(instance);}return result;
}
XrResult XRAPI_CALL getProc(XrInstance instance,const char* name,PFN_xrVoidFunction* function) {
    if(!name||!function)return XR_ERROR_VALIDATION_FAILURE;*function=nullptr;
    if(!strcmp(name,"xrGetInstanceProcAddr")){*function=reinterpret_cast<PFN_xrVoidFunction>(getProc);return XR_SUCCESS;}
    auto get=lookup(instance);if(!get)return XR_ERROR_FUNCTION_UNSUPPORTED;
    if(!strcmp(name,"xrDestroyInstance")){*function=reinterpret_cast<PFN_xrVoidFunction>(destroy);return XR_SUCCESS;}
    return get(instance,name,function);
}
std::wstring moduleDirectory() {
    HMODULE module=nullptr;wchar_t path[32768]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(moduleDirectory),&module))
        throw std::runtime_error("Cannot locate installed LMU layer.");
    auto length=GetModuleFileNameW(module,path,32768);if(!length||length>=32768)throw std::runtime_error("Invalid installed layer path.");
    return std::filesystem::path(path).parent_path().wstring();
}
// 0.3.3: the protected installation records the launcher hash approved by the
// elevated installer. The user-writable package copy must still match it.
std::string expectedLauncherHash(const wchar_t* record) {
    HANDLE file=CreateFileW((moduleDirectory()+L"\\"+record).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("The installed automatic startup predates launcher verification. Reinstall automatic startup from Setup.");
    char buffer[80]{};DWORD read=0;const bool ok=ReadFile(file,buffer,sizeof(buffer)-1,&read,nullptr)!=FALSE;CloseHandle(file);
    std::string value(buffer,ok?read:0);while(!value.empty()&&(value.back()=='\r'||value.back()=='\n'||value.back()==' '))value.pop_back();
    if(value.size()!=64||value.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid launcher verification record. Reinstall automatic startup from Setup.");
    return value;
}
std::string sha256(HANDLE file) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::string result;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 is unavailable.");
    try {
        if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("SHA256 is unavailable.");
        std::vector<unsigned char> chunk(1<<20);DWORD read=0;
        for(;;) {
            if(!ReadFile(file,chunk.data(),DWORD(chunk.size()),&read,nullptr))throw std::runtime_error("Cannot read the launcher for verification.");
            if(!read)break;
            if(BCryptHashData(hash,chunk.data(),read,0)<0)throw std::runtime_error("Cannot hash the launcher.");
        }
        unsigned char digest[32]{};
        if(BCryptFinishHash(hash,digest,sizeof(digest),0)<0)throw std::runtime_error("Cannot hash the launcher.");
        const char* digits="0123456789abcdef";for(auto b:digest){result+=digits[b>>4];result+=digits[b&15];}
    }catch(...){if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);throw;}
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);return result;
}
void helper(const std::wstring& root,const std::wstring& arguments,bool wait) {
    auto executable=root+L"\\LMU-QuadView-Launcher.exe";
    auto command=quote(executable)+L" "+arguments;
    // Deny writes and deletion while verifying and starting, so the checked file is the started file.
    HANDLE image=CreateFileW(executable.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(image==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open the LMU launcher. Reinstall automatic startup from the launcher.");
    HANDLE config=CreateFileW((executable+L".config").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(config==INVALID_HANDLE_VALUE){CloseHandle(image);throw std::runtime_error("Cannot open the launcher configuration. Reinstall automatic startup from Setup.");}
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);PROCESS_INFORMATION process{};
    SIZE_T attributesSize=0;
    InitializeProcThreadAttributeList(nullptr,1,0,&attributesSize);
    std::vector<unsigned char> attributes(attributesSize);
    startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    bool initialized=false;
    try {
        if(sha256(image)!=expectedLauncherHash(L"launcher.sha256")||sha256(config)!=expectedLauncherHash(L"launcher-config.sha256"))
            throw std::runtime_error("LMU-QuadView-Launcher.exe differs from the copy approved during installation. Reinstall automatic startup from Setup.");
        // Inherit only these two read handles. The child keeps both files locked
        // until exit, including asynchronous --attach and delayed CLR startup.
        HANDLE handles[]{image,config};
        if(!SetHandleInformation(image,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT)||!SetHandleInformation(config,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT)||
           !InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&attributesSize))
            throw std::runtime_error("Cannot protect launcher startup handles.");
        initialized=true;
        if(!UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr))
            throw std::runtime_error("Cannot protect launcher startup handles.");
        if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,root.c_str(),&startup.StartupInfo,&process))
            throw std::runtime_error("Cannot start LMU settings helper. Reinstall automatic startup from the launcher.");
    }catch(...){if(initialized)DeleteProcThreadAttributeList(startup.lpAttributeList);CloseHandle(config);CloseHandle(image);throw;}
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(config);CloseHandle(image);
    CloseHandle(process.hThread);
    DWORD code=0;bool ok=true;
    if(wait){ok=WaitForSingleObject(process.hProcess,30000)==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code)&&code==0;}
    CloseHandle(process.hProcess);
    if(!ok)throw std::runtime_error("LMU automatic preparation failed. Open the launcher and check logs/autoload-error.txt.");
}
std::wstring ini(const std::wstring& path,const wchar_t* key) {
    wchar_t buffer[32768]{};auto count=GetPrivateProfileStringW(L"Meta",key,L"",buffer,32768,path.c_str());
    if(count>=32767)throw std::runtime_error("Automatic-start setting is too long.");return buffer;
}
void applyEnvironment(const std::wstring& path) {
    wchar_t buffer[32768]{};auto count=GetPrivateProfileSectionW(L"Environment",buffer,32768,path.c_str());
    if(!count||count>=32766)throw std::runtime_error("Automatic-start environment is missing or too large.");
    std::vector<std::pair<std::wstring,std::wstring>> values;
    for(auto* entry=buffer;*entry;entry+=wcslen(entry)+1) {
        std::wstring line(entry);auto split=line.find(L'=');
        if(split==std::wstring::npos||line.rfind(L"LMU_QV_",0)!=0||split<=7)throw std::runtime_error("Invalid automatic-start environment.");
        auto key=line.substr(0,split);if(key.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::wstring::npos)throw std::runtime_error("Invalid environment name.");
        values.emplace_back(key,line.substr(split+1));
    }
    auto inherited=GetEnvironmentStringsW();std::vector<std::wstring> clear;
    if(inherited){for(auto* p=inherited;*p;p+=wcslen(p)+1)if(!_wcsnicmp(p,L"LMU_QV_",7))clear.push_back(std::wstring(p).substr(0,std::wstring(p).find(L'=')));FreeEnvironmentStringsW(inherited);}
    for(auto& key:clear)SetEnvironmentVariableW(key.c_str(),nullptr);
    for(auto& value:values)if(!SetEnvironmentVariableW(value.first.c_str(),value.second.c_str()))throw std::runtime_error("Cannot apply automatic-start settings.");
    // OpenGrid's newer LMU integration must not install a second adapter in VRS.
    SetEnvironmentVariableW(L"OPENGRIDXR_LMU_INTEGRATED",L"0");
    if(env(L"LMU_QV_MODE")!=L"quad"&&env(L"LMU_QV_MODE")!=L"vrs")throw std::runtime_error("Missing automatic render mode.");
}
Entry load(const std::wstring& library,const std::string& name) {
    if(!std::filesystem::path(library).is_absolute())throw std::runtime_error("Layer library path must be absolute.");
    auto module=LoadLibraryExW(library.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module)throw std::runtime_error("Cannot load the installed adapter or selected quad-view provider.");
    // Keep modules alive until process exit: the downstream runtime may retain callbacks.
    auto negotiate=reinterpret_cast<PFN_xrNegotiateLoaderApiLayerInterface>(GetProcAddress(module,"xrNegotiateLoaderApiLayerInterface"));
    XrNegotiateApiLayerRequest request{};request.structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST;
    request.structVersion=XR_API_LAYER_INFO_STRUCT_VERSION;request.structSize=sizeof(request);
    if(!negotiate||XR_FAILED(negotiate(&loaderInfo,name.c_str(),&request))||request.layerInterfaceVersion!=1||!request.getInstanceProcAddr||!request.createApiLayerInstance)
        throw std::runtime_error("Layer negotiation failed for the adapter or selected provider.");
    return {name,request.getInstanceProcAddr,request.createApiLayerInstance};
}
XrResult XRAPI_CALL create(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* instance) {
    startupLog("instance creation entered");
    if(!info||!layer||!instance||layer->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO||layer->structVersion!=XR_API_LAYER_CREATE_INFO_STRUCT_VERSION||
       layer->structSize!=sizeof(*layer)||!layer->nextInfo)return XR_ERROR_INITIALIZATION_FAILED;
    auto* head=layer->nextInfo;
    if(head->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO||head->structVersion!=XR_API_LAYER_NEXT_INFO_STRUCT_VERSION||head->structSize!=sizeof(*head)||
       !memchr(head->layerName,0,sizeof(head->layerName))||strcmp(head->layerName,layerName)||!head->nextGetInstanceProcAddr||!head->nextCreateApiLayerInstance)return XR_ERROR_INITIALIZATION_FAILED;
    try {
        auto root=activeRoot();auto downstream=*layer;
        XrResult result;PFN_xrGetInstanceProcAddr get=head->nextGetInstanceProcAddr;
        if(root.empty()) {downstream.nextInfo=head->next;result=head->nextCreateApiLayerInstance(info,&downstream,instance);}
        else {
            GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Cannot create startup request.");
            wchar_t nonce[33]{};auto* bytes=reinterpret_cast<unsigned char*>(&id);
            for(int i=0;i<16;i++)swprintf_s(nonce+i*2,3,L"%02x",bytes[i]);
            auto pid=std::to_wstring(GetCurrentProcessId());
            auto path=root+L"\\runtime\\autoload-"+nonce+L".ini";
            startupLog("preparing saved settings");
            helper(root,L"--prepare-autoload "+pid+L" "+nonce,true);
            startupLog("settings helper completed");
            auto backend=ini(path,L"Backend");auto providerName=ini(path,L"ProviderName");auto providerLibrary=ini(path,L"ProviderLibrary");
            if(!valid_backend(backend))throw std::runtime_error("Invalid automatic rendering provider.");
            applyEnvironment(path);DeleteFileW(path.c_str());
            auto backendName=backend==L"QVFR"?"QVFR":backend==L"VectorXR"?"VectorXR":backend==L"Native"?"Native":"VRS";
            Chain chain;chain.read(head,backendName);
            startupLog(std::string("building chain backend=")+backendName);
            Entry provider{};bool hasProvider=false;
            if(backend==L"QVFR"||backend==L"VectorXR") {
                auto expected=backend==L"QVFR"?L"XR_APILAYER_MBUCCHIA_quad_views_foveated":L"XR_APILAYER_DIENERTECH_VECTORXR";
                if(providerName!=expected)throw std::runtime_error("Automatic provider does not match saved settings.");
                provider=load(providerLibrary,backend==L"QVFR"?"XR_APILAYER_MBUCCHIA_quad_views_foveated":"XR_APILAYER_DIENERTECH_VECTORXR");hasProvider=true;
            }
            auto adapter=load(moduleDirectory()+L"\\lmu_quadview.dll","XR_APILAYER_LMU_quadview");
            chain.complete(adapter,hasProvider?&provider:nullptr);
            downstream.nextInfo=chain.nodes.data();get=chain.entries.front().get;
            for(const auto& entry:chain.entries)startupLog("chain layer="+entry.name);
            result=chain.entries.front().create(info,&downstream,instance);
            startupLog("adapter instance result="+std::to_string(result));
            if(XR_SUCCEEDED(result)&&env(L"LMU_QV_BENCHMARK")==L"1") {
                // Failure to attach the optional benchmark UI cannot invalidate a live XR instance.
                try{helper(root,L"--attach "+pid,false);}catch(...){}
            }
        }
        if(XR_SUCCEEDED(result)){std::lock_guard<std::mutex> lock(dispatchMutex);dispatch[*instance]=get;}return result;
    }catch(const std::exception& error){startupLog(std::string("failed: ")+error.what());MessageBoxA(nullptr,error.what(),"LMU Quad View - automatic startup",MB_OK|MB_ICONERROR);return XR_ERROR_INITIALIZATION_FAILED;}
}
}
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loader,const char* name,XrNegotiateApiLayerRequest* request) {
    startupLog("loader negotiation entered");
    if(!loader||!name||strcmp(name,layerName)||!request||loader->structType!=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO||
       loader->structVersion!=XR_LOADER_INFO_STRUCT_VERSION||loader->structSize!=sizeof(*loader)||
       request->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST||request->structVersion!=XR_API_LAYER_INFO_STRUCT_VERSION||request->structSize!=sizeof(*request)||
       loader->minInterfaceVersion>1||loader->maxInterfaceVersion<1||loader->minApiVersion>XR_CURRENT_API_VERSION||loader->maxApiVersion<XR_MAKE_VERSION(1,0,0))return XR_ERROR_INITIALIZATION_FAILED;
    loaderInfo=*loader;request->layerInterfaceVersion=1;request->layerApiVersion=std::min(loader->maxApiVersion,XR_CURRENT_API_VERSION);
    request->getInstanceProcAddr=getProc;request->createApiLayerInstance=create;return XR_SUCCESS;
}
