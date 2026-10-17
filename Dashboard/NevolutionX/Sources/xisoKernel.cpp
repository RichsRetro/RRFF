#include "xisoKernel.hpp"
#include "dashboardPaths.hpp"
#include "infoLog.hpp"
#ifdef NXDK
#include <hal/xbox.h>
#include <hal/fileio.h>
#include <xboxkrnl/xboxkrnl.h>
#include <windows.h>
#include <stdint.h>
#include <string.h>
namespace {
static int readable(uintptr_t address, unsigned count) {
    if (address < 0x80010000u || address > 0x80200000u - count) return 0;
    for (unsigned i = 0; i < count; ++i)
        if (!MmIsAddressValid((void *)(address + i))) return 0;
    return 1;
}

static int compatible(void) {
    static const unsigned char expected[64] = {0x83,0x0d,0x98,0x6d,0x03,0x80,0xff,0xc7,0x05,0x9c,0x6d,0x03,0x80,0xff,0xff,0xff,0x7f,0xc6,0x05,0x50,0xc3,0x03,0x80,0x00,0xc3,0x55,0x8b,0xec,0x83,0xec,0x20,0x53,0x56,0x8d,0x45,0xfc,0x50,0x33,0xdb,0x53,0x6a,0x02,0xbe,0xb0,0x23,0x01,0x80,0x56,0x53,0x68,0xd0,0x23,0x01,0x80,0xe8,0x79,0xf3,0xfe,0xff,0x3b,0xc3,0x7d,0x08,0x50};
    if (XboxKrnlVersion.Major != 1 || XboxKrnlVersion.Minor != 0 || XboxKrnlVersion.Build != 5838 || XboxKrnlVersion.Qfe != 1 || !readable(0x80026900u, 64)) return 0;
    const volatile unsigned char *p = (const volatile unsigned char *)0x80026900u;
    for (unsigned i = 0; i < 64; ++i) if (p[i] != expected[i]) return 0;
    return 1;
}
static int patcher_payload_valid(const char *path) {
    ANSI_STRING name;RtlInitAnsiString(&name,path);
    OBJECT_ATTRIBUTES attributes={.ObjectName=&name,.Attributes=OBJ_CASE_INSENSITIVE};
    IO_STATUS_BLOCK io={0};HANDLE file=NULL;
    NTSTATUS opened=NtOpenFile(&file,GENERIC_READ|SYNCHRONIZE,&attributes,&io,FILE_SHARE_READ,FILE_SYNCHRONOUS_IO_NONALERT);
    if(!NT_SUCCESS(opened)){InfoLog::outputLine(InfoLog::ERROR, "Patcher open error: %08lx\n",(unsigned long)opened);return 0;}
    unsigned char data[9432];LARGE_INTEGER offset;unsigned received=0;NTSTATUS status;
    while(received<sizeof(data)) {
        offset.QuadPart=received;io.Information=0;
        status=NtReadFile(file,NULL,NULL,NULL,&io,data+received,sizeof(data)-received,&offset);
        if(!NT_SUCCESS(status)||io.Information==0||io.Information>sizeof(data)-received) {
            InfoLog::outputLine(InfoLog::ERROR, "Patcher read error: %08lx / %lu\n",(unsigned long)status,(unsigned long)received);NtClose(file);return 0;
        }
        received+=(unsigned)io.Information;
    }
    // Verify the file ends at the exact expected length, without relying on
    // a filesystem-specific metadata query. Extra bytes still fail preflight.
    unsigned char extra;offset.QuadPart=sizeof(data);io.Information=0;
    status=NtReadFile(file,NULL,NULL,NULL,&io,&extra,1,&offset);NtClose(file);
    int exact_end=status==(NTSTATUS)STATUS_END_OF_FILE||(NT_SUCCESS(status)&&io.Information==0);
    if(!exact_end){InfoLog::outputLine(InfoLog::ERROR, "Patcher length check failed: %08lx\n",(unsigned long)status);return 0;}
    uint32_t crc=0xffffffffu;
    for (unsigned i=0;i<sizeof(data);++i) {crc^=data[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    if((crc^0xffffffffu)!=0x2403ea20u)InfoLog::outputLine(InfoLog::ERROR, "Patcher checksum does not match.\n");
    return (crc^0xffffffffu)==0x2403ea20u;
}
static bool nativePath(const std::string& path,char *native){
    // A: is the dashboard's home-directory link, not a HAL partition letter.
    if(path.size()>=3&&(path[0]=='A'||path[0]=='a')&&path[1]==':'&&path[2]=='\\'){
        std::string executable(XeImageFileName->Buffer,XeImageFileName->Length);
        std::string::size_type slash=executable.find_last_of('\\');
        if(slash==std::string::npos)return false;
        std::string resolved=executable.substr(0,slash+1)+path.substr(3);
        if(resolved.size()>=512)return false;memcpy(native,resolved.c_str(),resolved.size()+1);return true;
    }
    if(path.size()>=400)return false;
    return XConvertDOSFilenameToXBOX(path.c_str(),native)==0;
}
static bool virtualDriver(){ANSI_STRING name;PVOID object=nullptr;RtlInitAnsiString(&name,"\\Device\\CdRom1");NTSTATUS status=ObReferenceObjectByName(&name,OBJ_CASE_INSENSITIVE,&IoDeviceObjectType,nullptr,&object);if(NT_SUCCESS(status)&&object){ObfDereferenceObject(object);return true;}return false;}
static void restoreDVD(){
    ANSI_STRING virtualName;RtlInitAnsiString(&virtualName,"\\Device\\CdRom1");OBJECT_ATTRIBUTES attr={};attr.ObjectName=&virtualName;attr.Attributes=OBJ_CASE_INSENSITIVE;
    IO_STATUS_BLOCK io={};HANDLE handle=nullptr;
    if(NT_SUCCESS(NtOpenFile(&handle,GENERIC_READ|SYNCHRONIZE,&attr,&io,FILE_SHARE_READ,FILE_SYNCHRONOUS_IO_NONALERT))){
        NTSTATUS status=NtDeviceIoControlFile(handle,nullptr,nullptr,nullptr,&io,0x1ee7cd02u,nullptr,0,nullptr,0);NtClose(handle);
        if(NT_SUCCESS(status))IoDismountVolumeByName(&virtualName);
        else InfoLog::outputLine(InfoLog::ERROR,"Virtual DVD detach failed (%08lx).\n",(unsigned long)status);
    }
    ANSI_STRING drive,target;RtlInitAnsiString(&drive,"\\??\\D:");RtlInitAnsiString(&target,"\\Device\\CdRom0");IoDeleteSymbolicLink(&drive);IoCreateSymbolicLink(&drive,&target);
}
static bool attemptMarker(const char *path,bool create){
    ANSI_STRING name;RtlInitAnsiString(&name,path);OBJECT_ATTRIBUTES attr={};attr.ObjectName=&name;attr.Attributes=OBJ_CASE_INSENSITIVE;IO_STATUS_BLOCK io={};HANDLE file=nullptr;
    NTSTATUS status=NtCreateFile(&file,GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE,&attr,&io,nullptr,FILE_ATTRIBUTE_NORMAL,FILE_SHARE_READ,
        create?FILE_CREATE:FILE_OPEN,FILE_NON_DIRECTORY_FILE|FILE_SYNCHRONOUS_IO_NONALERT);
    if(NT_SUCCESS(status)){NtClose(file);return true;}return false;
}
}
#endif
void initializeXisoKernel(){
#ifdef NXDK
    // Cerbios owns its native image driver and DVD mappings.
    if(XboxKrnlVersion.Build>=8008)return;
    std::string marker=dashboardDataPath("system\\xiso-attempt.bin");
    if(virtualDriver()){restoreDVD();DeleteFileA(marker.c_str());return;}
    if(!compatible())return;
    char patcher[512]={},guard[512]={};
    if(!nativePath(dashboardDataPath("system\\kernel-patcher.xbe"),patcher)||!nativePath(marker,guard))return;
    if(attemptMarker(guard,false)){InfoLog::outputLine(InfoLog::ERROR,"XISO startup attempt previously failed; automatic retry blocked.\n");return;}
    if(!patcher_payload_valid(patcher)){InfoLog::outputLine(InfoLog::ERROR,"XISO kernel payload check failed; dashboard continuing normally.\n");return;}
    if(!attemptMarker(guard,true)){InfoLog::outputLine(InfoLog::ERROR,"Could not create XISO restart guard; patch skipped.\n");return;}
    XLaunchXBE(patcher);
    InfoLog::outputLine(InfoLog::ERROR,"XISO kernel patcher launch returned; dashboard continuing normally.\n");
#endif
}
