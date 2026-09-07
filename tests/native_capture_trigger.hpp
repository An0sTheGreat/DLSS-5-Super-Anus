#pragma once
// Test host only. The runner pins the exact addon hash and passes the retained
// capture function RVA plus its first 16 bytes. Never called in a game process.
inline void NativePresetChurnTrigger(unsigned frame) {
    char text[32] = {}, expected[64] = {};
    if (!GetEnvironmentVariableA("NR_PASS_TEST_RVA",text,sizeof(text))) return;
    // Two complete 1 -> 2 -> 3 -> 1 -> off -> 2 sequences, with capture
    // requested separately at frame 180. Never sends input to a game window.
    const unsigned frames[] = {30,60,90,120,150,165,210,240,270,300,330,345};
    const int presets[] = {1,2,3,1,0,2,1,2,3,1,0,2};
    unsigned index = 0;
    while (index < 12 && frames[index] != frame) ++index;
    if (index == 12) return;
    auto *module = reinterpret_cast<unsigned char *>(GetModuleHandleW(L"renodx-dlss5-super-anus.addon64"));
    const unsigned rva = std::strtoul(text,nullptr,16);
    if (!module || !GetEnvironmentVariableA("NR_PASS_TEST_BYTES",expected,sizeof(expected)) || strlen(expected) != 32)
        { puts("FAIL: pass-test identity missing"); ExitProcess(92); }
    const auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(module);
    const auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(module+dos->e_lfanew);
    bool allowed = false;
    const auto *section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (!memcmp(section[i].Name,".nr-dx11",8) && rva >= section[i].VirtualAddress &&
            rva+16 < section[i].VirtualAddress+section[i].Misc.VirtualSize) allowed = true;
    for (unsigned i = 0; allowed && i < 16; ++i) {
        char byte[3] = {expected[i*2],expected[i*2+1],0};
        allowed = module[rva+i] == std::strtoul(byte,nullptr,16);
    }
    if (!allowed) { puts("FAIL: pass-test byte/section mismatch"); ExitProcess(93); }
    const bool result = reinterpret_cast<bool (*)(int)>(module+rva)(presets[index]);
    printf("TEST ONLY: frame %u preset %d via production transaction: %s\n",frame,presets[index],result ? "OK" : "FAILED");
    if (!result) ExitProcess(94);
}
inline void NativeCaptureTestTrigger(unsigned frame) {
    NativePresetChurnTrigger(frame);
    if (frame != 180) return;
    char text[32] = {}, expected[64] = {};
    if (!GetEnvironmentVariableA("NR_CAPTURE_TEST_RVA",text,sizeof(text))) return;
    auto *module = reinterpret_cast<unsigned char *>(GetModuleHandleW(L"renodx-dlss5-super-anus.addon64"));
    const unsigned rva = std::strtoul(text,nullptr,16);
    if (!module || !GetEnvironmentVariableA("NR_CAPTURE_TEST_BYTES",expected,sizeof(expected)) || strlen(expected) != 32) {
        puts("FAIL: capture trigger identity missing"); ExitProcess(90);
    }
    const auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(module);
    const auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(module+dos->e_lfanew);
    bool allowed = false;
    const auto *section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (!memcmp(section[i].Name,".nr-dx11",8) && rva >= section[i].VirtualAddress &&
            rva+16 < section[i].VirtualAddress+section[i].Misc.VirtualSize) allowed = true;
    for (unsigned i = 0; allowed && i < 16; ++i) {
        char byte[3] = {expected[i*2],expected[i*2+1],0};
        allowed = module[rva+i] == std::strtoul(byte,nullptr,16);
    }
    if (!allowed) { puts("FAIL: capture trigger byte/section mismatch"); ExitProcess(91); }
    reinterpret_cast<void (*)()>(module+rva)();
    puts("TEST ONLY: screenshot requested for next NR evaluation (frame 180).");
}
