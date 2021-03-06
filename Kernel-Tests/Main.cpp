#include "pch.h"

#include "WdkTypes.h"
#include "CtlTypes.h"
#include "FltTypes.h"
#include "User-Bridge.h"
#include "Rtl-Bridge.h"

#include <fltUser.h>
#include "CommPort.h"
#include "Flt-Bridge.h"

#include "Kernel-Tests.h"

#include <vector>
#include <string>
#include <iostream>
#include <set>

#define _NO_CVCONST_H
#include <dbghelp.h>
#include "SymParser.h"

#include <winnt.h>
#include <winternl.h>
#include <cstdarg>
#include "../Kernel-Bridge/API/StringsAPI.h"

void RunTests() {
    BeeperTest tBeeper(L"Beeper");
    IoplTest tIopl(L"IOPL");
    VirtualMemoryTest tVirtualMemory(L"VirtualMemory");
    MdlTest tMdl(L"Mdl");
    PhysicalMemoryTest tPhysicalMemory(L"PhysicalMemory");
    ProcessesTest tProcesses(L"Processes");
    ShellTest tShell(L"Shells");
    StuffTest tStuff(L"Stuff");
}

class FilesReader final {
private:
    PVOID Buffer;
    ULONG Size;
public:
    FilesReader(LPCWSTR FilePath) {
        HANDLE hFile = CreateFile(FilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) throw std::runtime_error("File not found!");
        ULONG Size = GetFileSize(hFile, NULL);
        BOOL Status = FALSE;
        if (Size) {
            Buffer = VirtualAlloc(NULL, Size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (!Buffer) {
                CloseHandle(hFile);
                throw std::runtime_error("Memory not allocated!");
            }
            Status = ReadFile(hFile, Buffer, Size, &Size, NULL);
        }
        CloseHandle(hFile);
        if (!Status) throw std::runtime_error("Reading failure!");
    }
    ~FilesReader() {
        if (Buffer) VirtualFree(Buffer, 0, MEM_RELEASE);
    }
    PVOID Get() const { return Buffer; }
    ULONG GetSize() const { return Size; }
};

void MapDriver(LPCWSTR Path) {
    FilesReader DriverImage(Path);
    KbRtl::KbMapDriver(DriverImage.Get(), L"\\Driver\\EnjoyTheRing0");
}

VOID WINAPI Thread(PVOID Arg)  {
    while (true);
}

#define TEST_CALLBACKS

class Stopwatch {
    LARGE_INTEGER StartTime, Frequency;
public:
    Stopwatch() {
        QueryPerformanceFrequency(&Frequency);
        Start();
    }
    ~Stopwatch() = default;
    void Start() {
        QueryPerformanceCounter(&StartTime);
    }
    float Stop() {
        LARGE_INTEGER StopTime;
        QueryPerformanceCounter(&StopTime);
        return static_cast<float>(StopTime.QuadPart - StartTime.QuadPart) / static_cast<float>(Frequency.QuadPart);
    }
};

static void Fill(char* Buffer, char Sym) {
    size_t Length = strlen(Buffer);
    for (size_t i = 0; i < Length; i++) {
        Buffer[i] = Sym;
    }
}

static bool OnDeviceControl(PKB_FLT_DEVICE_CONTROL_INFO Data, bool PreFiltering) {
    if (Data->Ioctl != IOCTL_STORAGE_QUERY_PROPERTY) return false;

    if (!Data->InputLockedMdl || !Data->OutputLockedMdl || !Data->InputSize || !Data->OutputSize) {
        if (!Data->InputLockedMdl)  printf("[IOCTL_STORAGE_QUERY_PROPERTY]: InputMdl == NULL\r\n");
        if (!Data->OutputLockedMdl) printf("[IOCTL_STORAGE_QUERY_PROPERTY]: OutputMdl == NULL\r\n");
        if (!Data->InputSize)       printf("[IOCTL_STORAGE_QUERY_PROPERTY]: InputSize == 0\r\n");
        if (!Data->OutputSize)      printf("[IOCTL_STORAGE_QUERY_PROPERTY]: OutputSize == 0\r\n");
        return false;
    }
    
    using namespace Mdl;

    if (PreFiltering) {
        // Mapping input buffer:
        WdkTypes::PVOID Input = NULL;
        BOOL Status = KbMapMdl(&Input, 0, 0, Data->InputLockedMdl, FALSE);
        if (!Status) {
            printf("[IOCTL_STORAGE_QUERY_PROPERTY]: Unable to map input buffer!\r\n");
            return false;
        }

        bool NeedFiltering = false;
        if (Data->InputSize == sizeof(STORAGE_PROPERTY_QUERY)) {
            __try {
                auto Query = reinterpret_cast<PSTORAGE_PROPERTY_QUERY>(Input);
                NeedFiltering = Query->QueryType == PropertyStandardQuery && Query->PropertyId == StorageDeviceProperty;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                printf("[IOCTL_STORAGE_QUERY_PROPERTY]: Exception catched at NeedFiletring\r\n");
            }
        }
        printf("[IOCTL_STORAGE_QUERY_PROPERTY]: Need filtering %i for %ws\r\n", NeedFiltering, Data->Path);
        KbUnmapMdl(Data->InputLockedMdl, Input, FALSE);
        return NeedFiltering;
    }

    // Mapping output buffer:
    WdkTypes::PVOID Output = NULL;
    BOOL Status = KbMapMdl(&Output, 0, 0, Data->OutputLockedMdl, FALSE);
    if (!Status) {
        printf("[IOCTL_STORAGE_QUERY_PROPERTY]: Unable to map output buffer!\r\n");
        return false;
    }

    __try {
        auto Info = reinterpret_cast<PSTORAGE_DEVICE_DESCRIPTOR>(Output);
        if (
            Data->OutputSize > sizeof(STORAGE_DEVICE_DESCRIPTOR) &&
            Info->Version == sizeof(STORAGE_DEVICE_DESCRIPTOR) &&
            Info->Size > sizeof(STORAGE_DEVICE_DESCRIPTOR)
        ) {
            LPSTR ProductId = Info->ProductIdOffset 
                ? reinterpret_cast<PCHAR>(Info) + Info->ProductIdOffset
                : NULL;
            LPSTR VendorId = Info->VendorIdOffset 
                ? reinterpret_cast<PCHAR>(Info) + Info->VendorIdOffset
                : NULL;
            LPSTR Serial = Info->SerialNumberOffset 
                ? reinterpret_cast<PCHAR>(Info) + Info->SerialNumberOffset
                : NULL;
            LPSTR Revision = Info->ProductRevisionOffset 
                ? reinterpret_cast<PCHAR>(Info) + Info->ProductRevisionOffset
                : NULL;

            LPCSTR EmptyValue = "EMPTY";

            printf(
                "[IOCTL_STORAGE_QUERY_PROPERTY]: Product: '%s', Vendor: '%s', Serial: '%s', Revision: '%s' for %ws\r\n",
                (ProductId ? ProductId : EmptyValue),
                (VendorId  ? VendorId  : EmptyValue),
                (Serial    ? Serial    : EmptyValue),
                (Revision  ? Revision  : EmptyValue),
                Data->Path
            );

            if (ProductId) Fill(ProductId, 'P');
            if (VendorId)  Fill(VendorId , 'V');
            if (Serial)    Fill(Serial   , 'S');
            if (Revision)  Fill(Revision , 'R');

            printf(
                "[IOCTL_STORAGE_QUERY_PROPERTY]:[CHANGED]: Product: '%s', Vendor: '%s', Serial: '%s', Revision: '%s' for %ws\r\n",
                (ProductId ? ProductId : EmptyValue),
                (VendorId  ? VendorId  : EmptyValue),
                (Serial    ? Serial    : EmptyValue),
                (Revision  ? Revision  : EmptyValue),
                Data->Path
            );
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        printf("[IOCTL_STORAGE_QUERY_PROPERTY]: Exception catched!\r\n");
    }

    KbUnmapMdl(Data->OutputLockedMdl, Output, FALSE);

    return true;
}

std::set<UINT64> Tids;
CRITICAL_SECTION TidsLock;

int main() {
    KbLoader::KbUnload();
    if (KbLoader::KbLoadAsFilter(
        L"C:\\Temp\\Kernel-Bridge\\Kernel-Bridge.sys",
        L"260000" // Altitude of minifilter
    )) {
#ifdef TEST_CALLBACKS
        BOOL Status = TRUE;

        InitializeCriticalSectionAndSpinCount(&TidsLock, 0xC0000000);

        CommPortListener<KB_FLT_DEVICE_CONTROL_INFO, KbFltPreDeviceControl> PreDeviceControlFilter;
        Status = PreDeviceControlFilter.Subscribe([](CommPort& Port, MessagePacket<KB_FLT_DEVICE_CONTROL_INFO>& Message) -> VOID {
            auto Data = static_cast<PKB_FLT_DEVICE_CONTROL_INFO>(Message.GetData());
            if (Data->Ioctl == IOCTL_STORAGE_QUERY_PROPERTY && OnDeviceControl(Data, true)) {
                EnterCriticalSection(&TidsLock);
                Tids.emplace(Data->ThreadId);
                LeaveCriticalSection(&TidsLock);
            }
            ReplyPacket<KB_FLT_DEVICE_CONTROL_INFO> Reply(Message, ERROR_SUCCESS, *Data);
            Port.Reply(Reply);
        });

        CommPortListener<KB_FLT_DEVICE_CONTROL_INFO, KbFltPostDeviceControl> PostDeviceControlFilter;
        Status = PostDeviceControlFilter.Subscribe([](CommPort& Port, MessagePacket<KB_FLT_DEVICE_CONTROL_INFO>& Message) -> VOID {
            auto Data = static_cast<PKB_FLT_DEVICE_CONTROL_INFO>(Message.GetData());
            if (Data->Ioctl == IOCTL_STORAGE_QUERY_PROPERTY) {
                EnterCriticalSection(&TidsLock);
                bool NeedPatch = Tids.find(Data->ThreadId) != Tids.end();
                LeaveCriticalSection(&TidsLock);

                if (NeedPatch) {
                    Tids.erase(Data->ThreadId);
                    OnDeviceControl(Data, false);
                }
            }
            
            ReplyPacket<KB_FLT_DEVICE_CONTROL_INFO> Reply(Message, ERROR_SUCCESS, *Data);
            Port.Reply(Reply);
        });

        CommPortListener<KB_FLT_DEVICE_CONTROL_INFO, KbFltPreInternalDeviceControl> PreInternalDeviceControlFilter;
        Status = PreInternalDeviceControlFilter.Subscribe([](CommPort& Port, MessagePacket<KB_FLT_DEVICE_CONTROL_INFO>& Message) -> VOID {
            auto Data = static_cast<PKB_FLT_DEVICE_CONTROL_INFO>(Message.GetData());
            if (Data->Ioctl == IOCTL_STORAGE_QUERY_PROPERTY && OnDeviceControl(Data, true)) {
                EnterCriticalSection(&TidsLock);
                Tids.emplace(Data->ThreadId);
                LeaveCriticalSection(&TidsLock);
            }
            ReplyPacket<KB_FLT_DEVICE_CONTROL_INFO> Reply(Message, ERROR_SUCCESS, *Data);
            Port.Reply(Reply);
        });

        CommPortListener<KB_FLT_DEVICE_CONTROL_INFO, KbFltPostInternalDeviceControl> PostInternalDeviceControlFilter;
        Status = PostInternalDeviceControlFilter.Subscribe([](CommPort& Port, MessagePacket<KB_FLT_DEVICE_CONTROL_INFO>& Message) -> VOID {
            auto Data = static_cast<PKB_FLT_DEVICE_CONTROL_INFO>(Message.GetData());
            if (Data->Ioctl == IOCTL_STORAGE_QUERY_PROPERTY) {
                EnterCriticalSection(&TidsLock);
                bool NeedPatch = Tids.find(Data->ThreadId) != Tids.end();
                LeaveCriticalSection(&TidsLock);

                if (NeedPatch) {
                    Tids.erase(Data->ThreadId);
                    OnDeviceControl(Data, false);
                }
            }
            
            ReplyPacket<KB_FLT_DEVICE_CONTROL_INFO> Reply(Message, ERROR_SUCCESS, *Data);
            Port.Reply(Reply);
        });
/*
        CommPortListener<KB_FLT_DATA_INFO, KbFltPostRead> ReadFilter;
        Status = ReadFilter.Subscribe([](CommPort& Port, MessagePacket<KB_FLT_DATA_INFO>& Message) -> VOID {
            auto Data = static_cast<PKB_FLT_DATA_INFO>(Message.GetData());
            if (wcsstr(Data->Path, L".prot") && Data->Size && Data->Size <= 4096) {
                if (Data->Mdl) {
                    printf("[READ]: Mdl: 0x%p at %i bytes\r\n", reinterpret_cast<PVOID>(Data->Mdl), Data->Size);
                    WdkTypes::PVOID Mapping = NULL;
                    BOOL MappingStatus = Mdl::KbMapMdl(&Mapping, Data->ProcessId, 0, Data->Mdl);
                    if (MappingStatus) {
                        printf("[READ]: Successfully mapped!\r\n");
                        try {
                            PCHAR Buf = reinterpret_cast<PCHAR>(Mapping);
                            printf("[READ]: %s\r\n", Buf);
                        } catch (...) {
                            printf("[READ]: Exception catched!\r\n");
                        }
                        Mdl::KbUnmapMdl(Data->Mdl, Mapping);
                    }
                } else {
                    printf("[READ]: Mdl not present!\r\n");
                }
            }
            ReplyPacket<KB_FLT_DATA_INFO> Reply(Message, ERROR_SUCCESS, *Data);
            Port.Reply(Reply);
        });

        CommPortListener<KB_FLT_DATA_INFO, KbFltPreWrite> WriteFilter;
        Status = WriteFilter.Subscribe([](CommPort& Port, MessagePacket<KB_FLT_DATA_INFO>& Message) -> VOID {
            auto Data = static_cast<PKB_FLT_DATA_INFO>(Message.GetData());
            if (wcsstr(Data->Path, L".prot") && Data->Size && Data->Size <= 4096) {
                if (Data->Mdl) {
                    printf("[WRITE]: Mdl: 0x%p at %i bytes\r\n", reinterpret_cast<PVOID>(Data->Mdl), Data->Size);
                    WdkTypes::PVOID Mapping = NULL;
                    BOOL MappingStatus = Mdl::KbMapMdl(&Mapping, Data->ProcessId, 0, Data->Mdl);
                    if (MappingStatus) {
                        printf("[WRITE]: Successfully mapped!\r\n");
                        try {
                            PCHAR Buf = reinterpret_cast<PCHAR>(Mapping);
                            printf("[WRITE]: %s\r\n", Buf);
                        } catch (...) {
                            printf("[WRITE]: Exception catched!\r\n");
                        }
                        Mdl::KbUnmapMdl(Data->Mdl, Mapping);
                    }
                }
            }
            ReplyPacket<KB_FLT_DATA_INFO> Reply(Message, ERROR_SUCCESS, *Data);
            Port.Reply(Reply);
        });
*/

        if (!Status) std::cout << "Callbacks failure!" << std::endl;
        while (true) Sleep(100);
#endif

        RunTests();
        KbLoader::KbUnload();
    } else {
        std::wcout << L"Unable to load driver!" << std::endl;
    }

    std::wcout << L"Press any key to exit..." << std::endl;
    std::cin.get();
}