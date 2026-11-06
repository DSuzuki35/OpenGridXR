#include "../autoload.cpp"
#include <fstream>
#include <iostream>

static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void write(const std::filesystem::path& path,const std::string& text){std::ofstream(path,std::ios::binary)<<text;}
static std::string fileHash(const std::filesystem::path& path){
    auto file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    require(file!=INVALID_HANDLE_VALUE,"fixture read failed");auto result=sha256(file);CloseHandle(file);return result;
}
static bool denied(const std::filesystem::path& path,DWORD access){
    auto file=CreateFileW(path.c_str(),access,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_SHARING_VIOLATION;
    CloseHandle(file);return false;
}
int main(int argc,char**){
    const auto module=std::filesystem::path(moduleDirectory());
    if(argc>1){
        write(module/L"ready","1");
        for(unsigned i=0;i<1000&&!std::filesystem::exists(module/L"release");++i)Sleep(10);
        return 0;
    }
    wchar_t temporary[MAX_PATH]{};GetTempPathW(MAX_PATH,temporary);
    // PIDs are reused (quickly under Wine), so the folder name also carries a
    // high-resolution timestamp; a leftover folder from a crashed run can never collide.
    LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
    auto root=std::filesystem::path(temporary)/(L"lmu-helper-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(stamp.QuadPart));
    const auto record=module/L"launcher.sha256",configRecord=module/L"launcher-config.sha256";
    auto exe=root/L"LMU-QuadView-Launcher.exe",config=root/L"LMU-QuadView-Launcher.exe.config";
    // The child's image stays locked for a moment after it signals its exit, so cleanup
    // retries instead of failing (and leaving fixtures behind) on a transient denial.
    bool ownsRecords=false,ownsRoot=false;
    auto cleanup=[&]()->bool{
        std::error_code ignored;
        if(ownsRecords){std::filesystem::remove(record,ignored);std::filesystem::remove(configRecord,ignored);}
        if(!ownsRoot)return true;
        for(unsigned i=0;i<500;++i){
            std::error_code error;std::filesystem::remove_all(root,error);
            if(!error&&!std::filesystem::exists(root,error))return true;
            Sleep(10);
        }
        return false;
    };
    auto rejected=[&](){try{helper(root,L"--probe",false);return false;}catch(const std::runtime_error&){return true;}};
    try{
        require(!std::filesystem::exists(record)&&!std::filesystem::exists(configRecord),"fixture hash records already exist");
        require(std::filesystem::create_directory(root),"fixture directory already exists");ownsRoot=true;
        wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
        std::filesystem::copy_file(self,exe);write(config,"approved config");
        bool readFailed=false;try{sha256(INVALID_HANDLE_VALUE);}catch(const std::runtime_error&){readFailed=true;}
        require(readFailed,"failed read was treated as EOF");
        require(rejected(),"missing verification records accepted");
        ownsRecords=true;write(record,"invalid");write(configRecord,fileHash(config));
        require(rejected(),"malformed verification record accepted");
        write(record,fileHash(exe));write(config,"tampered");
        require(rejected(),"tampered .NET config accepted");write(config,"approved config");
        write(record,std::string(64,'0'));require(rejected(),"wrong EXE hash accepted");write(record,fileHash(exe));
        auto saved=root/L"saved.config";std::filesystem::rename(config,saved);
        require(rejected(),"missing config accepted");std::filesystem::rename(saved,config);
        require(!std::filesystem::exists(root/L"ready"),"unverified child was started");
        helper(root,L"--probe",false);
        for(unsigned i=0;i<500&&!std::filesystem::exists(root/L"ready");++i)Sleep(10);
        require(std::filesystem::exists(root/L"ready"),"verified child did not start");
        require(denied(exe,GENERIC_WRITE)&&denied(config,GENERIC_WRITE)&&denied(config,DELETE),"child lifetime did not retain file locks");
        write(root/L"release","1");
        for(unsigned i=0;i<500&&denied(config,GENERIC_WRITE);++i)Sleep(10);
        require(!denied(config,GENERIC_WRITE),"config lock leaked after child exit");
        for(unsigned i=0;i<500&&denied(exe,GENERIC_WRITE);++i)Sleep(10);
        require(!denied(exe,GENERIC_WRITE),"launcher image lock leaked after child exit");
        require(cleanup(),"fixture folder could not be removed");
        std::cout<<"Autoload verification tests passed: hashes, config, read failure, inherited file locks.\n";
    }catch(const std::exception& e){
        std::error_code ignored;
        if(ownsRoot&&std::filesystem::exists(root,ignored))write(root/L"release","1");
        cleanup();std::cerr<<e.what()<<'\n';return 1;
    }
}
