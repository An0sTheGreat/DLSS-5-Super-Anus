// Read-only stacks of the explicitly named, test-owned executable. Never games.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <psapi.h>
#include <cstdio>
#include <cwchar>
#include <cstdlib>

int wmain(int argc, wchar_t **argv)
{
    if (argc != 3) return 2;
    const DWORD pid = wcstoul(argv[1], nullptr, 10);
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    wchar_t path[32768] = {}; DWORD size = 32768;
    if (!process || !QueryFullProcessImageNameW(process, 0, path, &size) ||
        _wcsicmp(path, argv[2]) || !wcsstr(path, L"\\build\\api-native-")) return 3;
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
    SymInitialize(process, nullptr, TRUE);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry = {}; entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) do
    {
        if (entry.th32OwnerProcessID != pid) continue;
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        if (SuspendThread(thread) == DWORD(-1)) { CloseHandle(thread); continue; }
        CONTEXT context = {}; context.ContextFlags = CONTEXT_FULL;
        printf("Thread %lu\n", entry.th32ThreadID);
        if (GetThreadContext(thread, &context))
        {
            STACKFRAME64 frame = {};
            frame.AddrPC = {context.Rip, 0, AddrModeFlat};
            frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
            frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
            for (unsigned i = 0; i < 40 && frame.AddrPC.Offset; ++i)
            {
                IMAGEHLP_MODULE64 module = {}; module.SizeOfStruct = sizeof(module);
                if (SymGetModuleInfo64(process, frame.AddrPC.Offset, &module))
                    printf("  %s+0x%llx\n", module.ModuleName, frame.AddrPC.Offset - module.BaseOfImage);
                else printf("  0x%llx\n", frame.AddrPC.Offset);
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context,
                    nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
            }
        }
        ResumeThread(thread); CloseHandle(thread);
    } while (Thread32Next(snapshot, &entry));
    CloseHandle(snapshot); SymCleanup(process); CloseHandle(process);
    return 0;
}
