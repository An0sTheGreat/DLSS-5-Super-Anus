#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "nvsdk_ngx_vk.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <cassert>
#include "vulkan_direct_binding.hpp"
int wmain(int argc, wchar_t **argv)
{
    assert(argc == 2);
    wchar_t absolute[MAX_PATH] = {};
    const DWORD length = GetFullPathNameW(argv[1], MAX_PATH, absolute, nullptr);
    assert(length && length < MAX_PATH);
    std::wstring directory = absolute;
    if (directory.back() != L'\\' && directory.back() != L'/') directory += L'\\';
    using namespace vulkan_direct_probe;
    wchar_t exe[MAX_PATH] = {}; assert(GetModuleFileNameW(nullptr, exe, MAX_PATH));
    assert(!exact_snippet(exe)); // wrong actual PE/hash rejected
    Binding first, second;
    assert(first.open(directory));
    auto *slot = first.slot; const auto original = first.original;
    assert(!first.open(directory) && !second.open(directory));
    assert(second.close() && caller_module); // empty owner cannot clear first
    wchar_t text[MAX_PATH] = {};
    assert(caller_filename(caller_module, text, MAX_PATH) == 10 && !wcscmp(text, L"_nvngx.dll"));
    wchar_t short_text[3] = {};
    assert(caller_filename(caller_module, short_text, 3) == 3 && short_text[2] == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER);
    assert(!caller_filename(caller_module, short_text, 0));
    wchar_t actual[MAX_PATH] = {};
    assert(caller_filename(first.module, text, MAX_PATH) == GetModuleFileNameW(first.module, actual, MAX_PATH));
    assert(!wcscmp(text, actual)); // unrelated modules forwarded unchanged
    first.attempted = true; assert(!first.close() && first.module && first.patched);
    first.attempted = false; // mock-only; no NGX calls made
    // Retain one reference so restoration can be inspected after close.
    HMODULE retained = nullptr;
    assert(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(first.module), &retained));
    assert(first.close() && *slot == original && !caller_module && !first.module && !first.init);
    assert(first.close()); FreeLibrary(retained);
    std::puts("Vulkan direct binding guards: exact hash, single owner, empty-owner isolation, scoped forwarding, failed-init retention, import restoration and repeat close passed. NO GPU/NGX evaluation.");
}
