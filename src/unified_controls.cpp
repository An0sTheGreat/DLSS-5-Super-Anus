// Import-free control layer injected into the matching RenoDX DLSS binary.
// All host and ImGui calls are resolved from the original module at runtime.

using u8 = unsigned char;
using u32 = unsigned int;
using u64 = unsigned long long;
using uptr = unsigned long long;
using usize = unsigned long long;
using s16 = short;

extern "C" int _fltused = 0;

struct Vec2 {
  float x;
  float y;
};

using DllEntryFn = int (*)(void*, u32, void*);
using LoadLibraryAFn = void* (*)(const char*);
using GetProcAddressFn = void* (*)(void*, const char*);
using CreateThreadFn = void* (*)(void*, usize, u32 (*)(void*), void*, u32, u32*);
using GetAsyncKeyStateFn = s16 (*)(int);
using SleepFn = void (*)(u32);
using GetTickCount64Fn = u64 (*)();
using VirtualProtectFn = int (*)(void*, usize, u32, u32*);
using GetHostModuleFn = void* (*)();
using GetConfigFn = bool (*)(void*, void*, const char*, const char*, char*, usize*);
using SetConfigFn = void (*)(void*, void*, const char*, const char*, const char*);
using LogFn = void (*)(void*, int, const char*);

// RVAs in the 2026-09-05 official RenoDX build (SHA-256 is checked by patcher).
constexpr uptr kOriginalEntryRva = 0x1771B4;
constexpr uptr kPresetApplyContinuationRva = 0x0CC59A;
constexpr uptr kPresetRva = 0x2677F8;
constexpr uptr kNativeScaleRva = 0x218250;
constexpr uptr kNativeStructureRva = 0x26D85C;
constexpr uptr kNativeResetRva = 0x26D900;
constexpr uptr kImguiTableRva = 0x271000;
constexpr uptr kGetHostModuleRva = 0x025D90;
constexpr uptr kIatGetProcAddressRva = 0x233A40;
constexpr uptr kIatGetTickCount64Rva = 0x233A88;
constexpr uptr kIatLoadLibraryARva = 0x233B48;
constexpr uptr kIatSleepRva = 0x233C80;
constexpr uptr kIatVirtualProtectRva = 0x233CF0;

// ReShade ImGui function table 19250 offsets.
constexpr usize kImGuiGetIo = 0x000;
constexpr usize kImGuiBegin = 0x018;
constexpr usize kImGuiEnd = 0x020;
constexpr usize kImGuiSetNextWindowPos = 0x090;
constexpr usize kImGuiSetNextWindowBgAlpha = 0x0C8;
constexpr usize kImGuiPushStyleVar = 0x1A0;
constexpr usize kImGuiPopStyleVar = 0x1C0;
constexpr usize kImGuiSameLine = 0x288;
constexpr usize kImGuiSpacing = 0x298;
constexpr usize kImGuiTextUnformatted = 0x338;
constexpr usize kImGuiSeparatorText = 0x370;
constexpr usize kImGuiButton = 0x378;
constexpr usize kImGuiSliderInt = 0x4A8;

constexpr u32 kPageReadWrite = 0x04;
constexpr u32 kVkDivide = 0x6F;
constexpr u32 kVkMultiply = 0x6A;

constexpr int kWindowFlags =
    (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 5) |
    (1 << 6) | (1 << 8) | (1 << 9) | (1 << 12) | (1 << 13) |
    (1 << 16) | (1 << 17) | (1 << 19);

static volatile uptr g_base = 0;
static volatile u8 g_stop = 0;
static volatile int g_pending_scale = 100;
static volatile int g_applied_scale = 100;
static volatile int g_sharpness = 100;
static volatile int g_overlay_message = 0;
static volatile u64 g_overlay_start = 0;
static GetConfigFn g_get_config = nullptr;
static SetConfigFn g_set_config = nullptr;
static LogFn g_log = nullptr;

static const char kKernel32[] = "kernel32.dll";
static const char kUser32[] = "user32.dll";
static const char kCreateThread[] = "CreateThread";
static const char kGetAsyncKeyState[] = "GetAsyncKeyState";
static const char kGetConfigName[] = "ReShadeGetConfigValue";
static const char kSetConfigName[] = "ReShadeSetConfigValue";
static const char kLogName[] = "ReShadeLogMessage";
static const char kConfigSection[] = "RenoDX DLSS Unified";
static const char kScaleKey[] = "NeuralResolutionScale";
static const char kSharpnessKey[] = "ReconstructionSharpness";
static const char kNeuralHeading[] = "Neural Rendering Performance";
static const char kScaleLabel[] = "Resolution Scale";
static const char kSharpnessLabel[] = "Reconstruction Sharpness";
static const char kPercentFormat[] = "%d%%";
static const char kApply[] = "Apply Resolution Scale";
static const char kAppliedPrefix[] = "Applied scale: ";
static const char kSharpnessHelp[] = "Native DLSS-NR detail strength: 0% softer, 100% maximum detail.";
static const char kControls[] = "Controls";
static const char kDivideHelp[] = "Numpad /    Toggle Neural Rendering On / Off";
static const char kMultiplyHelp[] = "Numpad *    Cycle Preset 1 -> 2 -> 3 -> 1";
static const char kOsdWindow[] = "##RenoDXUnifiedStatus";
static const char kPreset1[] = "PRESET 1";
static const char kPreset2[] = "PRESET 2";
static const char kPreset3[] = "PRESET 3";
static const char kNrOn[] = "NR ON";
static const char kNrOff[] = "NR OFF";
static const char kInitLog[] = "RenoDX DLSS unified controls initialized (native NR scaling).";
static const char kScaleLog[] = "Native neural resolution scale applied.";

template <typename T>
static T imported(uptr rva) {
  return *reinterpret_cast<T*>(static_cast<uptr>(g_base) + rva);
}

template <typename T>
static T module_fn(uptr rva) {
  return reinterpret_cast<T>(static_cast<uptr>(g_base) + rva);
}

static void** imgui() {
  if (g_base == 0) return nullptr;
  return *reinterpret_cast<void***>(static_cast<uptr>(g_base) + kImguiTableRva);
}

template <typename T>
static T imgui_fn(void** table, usize offset) {
  return reinterpret_cast<T>(table[offset / sizeof(void*)]);
}

static int clamp_percent(int value, int minimum) {
  if (value < minimum) return minimum;
  if (value > 100) return 100;
  return value;
}

static int parse_percent(const char* value, int fallback, int minimum) {
  if (value == nullptr || *value == 0) return fallback;
  int result = 0;
  bool found = false;
  while (*value >= '0' && *value <= '9') {
    found = true;
    result = result * 10 + (*value - '0');
    ++value;
  }
  return found ? clamp_percent(result, minimum) : fallback;
}

static void format_percent(int value, char (&buffer)[8]) {
  value = clamp_percent(value, 0);
  int digits = 0;
  if (value >= 100) buffer[digits++] = '1';
  if (value >= 10) buffer[digits++] = static_cast<char>('0' + (value / 10) % 10);
  buffer[digits++] = static_cast<char>('0' + value % 10);
  buffer[digits] = 0;
}

static void save_percent(const char* key, int value) {
  if (g_set_config == nullptr) return;
  char text[8] = {};
  format_percent(value, text);
  g_set_config(reinterpret_cast<void*>(static_cast<uptr>(g_base)), nullptr,
               kConfigSection, key, text);
}

static void request_native_reset() {
  *reinterpret_cast<volatile u8*>(static_cast<uptr>(g_base) + kNativeResetRva) = 1;
}

static void apply_scale(int percent) {
  percent = clamp_percent(percent, 50);
  float scale = static_cast<float>(percent) * 0.01f;
  void* address = reinterpret_cast<void*>(static_cast<uptr>(g_base) + kNativeScaleRva);
  auto protect = imported<VirtualProtectFn>(kIatVirtualProtectRva);
  u32 old_protection = 0;
  if (protect != nullptr && protect(address, sizeof(float), kPageReadWrite, &old_protection)) {
    *reinterpret_cast<volatile float*>(address) = scale;
    u32 ignored = 0;
    protect(address, sizeof(float), old_protection, &ignored);
    request_native_reset();
  }
}

static void apply_sharpness(int percent) {
  percent = clamp_percent(percent, 0);
  const float strength = static_cast<float>(percent) * 0.01f;
  *reinterpret_cast<volatile float*>(static_cast<uptr>(g_base) + kNativeStructureRva) = strength;
  request_native_reset();
}

static void load_settings() {
  if (g_get_config == nullptr) return;
  char value[16] = {};
  usize size = sizeof(value);
  if (g_get_config(reinterpret_cast<void*>(static_cast<uptr>(g_base)), nullptr,
                   kConfigSection, kScaleKey, value, &size)) {
    g_applied_scale = parse_percent(value, 100, 50);
    g_pending_scale = g_applied_scale;
  }
  size = sizeof(value);
  value[0] = 0;
  if (g_get_config(reinterpret_cast<void*>(static_cast<uptr>(g_base)), nullptr,
                   kConfigSection, kSharpnessKey, value, &size)) {
    g_sharpness = parse_percent(value, 100, 0);
  }
  apply_scale(g_applied_scale);
  apply_sharpness(g_sharpness);
}

static u64 ticks() {
  auto fn = imported<GetTickCount64Fn>(kIatGetTickCount64Rva);
  return fn != nullptr ? fn() : 0;
}

static void show_message(int message) {
  g_overlay_start = ticks();
  g_overlay_message = message;
}

static const char* overlay_text(int message) {
  switch (message) {
    case 1: return kPreset1;
    case 2: return kPreset2;
    case 3: return kPreset3;
    case 4: return kNrOn;
    case 5: return kNrOff;
    default: return nullptr;
  }
}

extern "C" void apply_current_preset(uptr module_base);
extern "C" uptr module_base_slot;

static u32 hotkey_worker(void*) {
  auto load_library = imported<LoadLibraryAFn>(kIatLoadLibraryARva);
  auto get_proc = imported<GetProcAddressFn>(kIatGetProcAddressRva);
  auto sleep = imported<SleepFn>(kIatSleepRva);
  if (load_library == nullptr || get_proc == nullptr || sleep == nullptr) return 0;
  void* user32 = load_library(kUser32);
  if (user32 == nullptr) return 0;
  auto get_key = reinterpret_cast<GetAsyncKeyStateFn>(get_proc(user32, kGetAsyncKeyState));
  if (get_key == nullptr) return 0;

  int current = *reinterpret_cast<volatile int*>(static_cast<uptr>(g_base) + kPresetRva);
  int last_preset = (current >= 1 && current <= 3) ? current : 1;
  bool divide_down = false;
  bool multiply_down = false;

  while (!g_stop) {
    const bool divide_now = (get_key(kVkDivide) & static_cast<s16>(0x8000)) != 0;
    if (divide_now && !divide_down) {
      volatile int* preset = reinterpret_cast<volatile int*>(static_cast<uptr>(g_base) + kPresetRva);
      current = *preset;
      if (current != 0) {
        if (current >= 1 && current <= 3) last_preset = current;
        *preset = 0;
        apply_current_preset(static_cast<uptr>(g_base));
        show_message(5);
      } else {
        if (last_preset < 1 || last_preset > 3) last_preset = 1;
        *preset = last_preset;
        apply_current_preset(static_cast<uptr>(g_base));
        show_message(4);
      }
    }
    divide_down = divide_now;

    const bool multiply_now = (get_key(kVkMultiply) & static_cast<s16>(0x8000)) != 0;
    if (multiply_now && !multiply_down) {
      volatile int* preset = reinterpret_cast<volatile int*>(static_cast<uptr>(g_base) + kPresetRva);
      current = *preset;
      if (current == 0) {
        last_preset = (last_preset >= 1 && last_preset < 3) ? last_preset + 1 : 1;
        show_message(last_preset);
      } else {
        current = (current >= 1 && current < 3) ? current + 1 : 1;
        last_preset = current;
        *preset = current;
        apply_current_preset(static_cast<uptr>(g_base));
        show_message(current);
      }
    }
    multiply_down = multiply_now;
    sleep(25);
  }
  return 0;
}

extern "C" void draw_settings(void* setting) {
  if (setting == nullptr) return;
  const uptr address = reinterpret_cast<uptr>(setting);
  // The Links/Discord entry is the stable insertion marker used by V4.
  if (*reinterpret_cast<const u64*>(address + 0x90) != 7 ||
      *reinterpret_cast<const u64*>(address + 0x80) != 0x0064726F63736944ULL) return;

  void** table = imgui();
  if (table == nullptr) return;
  auto spacing = imgui_fn<void (*)()>(table, kImGuiSpacing);
  auto separator_text = imgui_fn<void (*)(const char*)>(table, kImGuiSeparatorText);
  auto text = imgui_fn<void (*)(const char*, const char*)>(table, kImGuiTextUnformatted);
  auto slider_int = imgui_fn<bool (*)(const char*, int*, int, int, const char*, int)>(table, kImGuiSliderInt);
  auto button = imgui_fn<bool (*)(const char*, const Vec2&)>(table, kImGuiButton);
  if (spacing == nullptr || separator_text == nullptr || text == nullptr ||
      slider_int == nullptr || button == nullptr) return;

  spacing();
  separator_text(kNeuralHeading);

  int pending = g_pending_scale;
  if (slider_int(kScaleLabel, &pending, 50, 100, kPercentFormat, 0)) {
    g_pending_scale = clamp_percent(pending, 50);
  }
  const Vec2 automatic_size = {0.0f, 0.0f};
  if (button(kApply, automatic_size)) {
    g_applied_scale = clamp_percent(g_pending_scale, 50);
    apply_scale(g_applied_scale);
    save_percent(kScaleKey, g_applied_scale);
    if (g_log != nullptr) g_log(reinterpret_cast<void*>(static_cast<uptr>(g_base)), 2, kScaleLog);
  }
  char applied_text[24] = {};
  int position = 0;
  for (const char* source = kAppliedPrefix; *source != 0; ++source)
    applied_text[position++] = *source;
  char percent[8] = {};
  format_percent(g_applied_scale, percent);
  for (const char* source = percent; *source != 0; ++source)
    applied_text[position++] = *source;
  applied_text[position++] = '%';
  applied_text[position] = 0;
  text(applied_text, nullptr);

  int sharpness = g_sharpness;
  if (slider_int(kSharpnessLabel, &sharpness, 0, 100, kPercentFormat, 0)) {
    g_sharpness = clamp_percent(sharpness, 0);
    apply_sharpness(g_sharpness);
    save_percent(kSharpnessKey, g_sharpness);
  }
  text(kSharpnessHelp, nullptr);

  spacing();
  separator_text(kControls);
  text(kDivideHelp, nullptr);
  text(kMultiplyHelp, nullptr);
}

extern "C" void draw_osd() {
  const int message = g_overlay_message;
  const char* message_text = overlay_text(message);
  if (message_text == nullptr) return;
  const u64 elapsed = ticks() - g_overlay_start;
  if (elapsed >= 3000) {
    g_overlay_message = 0;
    return;
  }

  float alpha = 1.0f;
  if (elapsed > 1000) alpha = static_cast<float>(3000 - elapsed) / 2000.0f;
  void** table = imgui();
  if (table == nullptr) return;
  auto get_io = imgui_fn<void* (*)()>(table, kImGuiGetIo);
  auto set_position = imgui_fn<void (*)(const Vec2&, int, const Vec2&)>(table, kImGuiSetNextWindowPos);
  auto set_background_alpha = imgui_fn<void (*)(float)>(table, kImGuiSetNextWindowBgAlpha);
  auto push_style_var = imgui_fn<void (*)(int, float)>(table, kImGuiPushStyleVar);
  auto pop_style_var = imgui_fn<void (*)(int)>(table, kImGuiPopStyleVar);
  auto begin = imgui_fn<bool (*)(const char*, bool*, int)>(table, kImGuiBegin);
  auto end = imgui_fn<void (*)()>(table, kImGuiEnd);
  auto text = imgui_fn<void (*)(const char*, const char*)>(table, kImGuiTextUnformatted);
  if (get_io == nullptr || set_position == nullptr || set_background_alpha == nullptr ||
      push_style_var == nullptr || pop_style_var == nullptr || begin == nullptr ||
      end == nullptr || text == nullptr) return;

  const u8* io = static_cast<const u8*>(get_io());
  if (io == nullptr) return;
  const Vec2 display = *reinterpret_cast<const Vec2*>(io + 8);
  const Vec2 position = {display.x - 16.0f, 16.0f};
  const Vec2 pivot = {1.0f, 0.0f};
  push_style_var(0, alpha);
  set_position(position, 1, pivot);
  set_background_alpha(0.82f);
  if (begin(kOsdWindow, nullptr, kWindowFlags)) text(message_text, nullptr);
  end();
  pop_style_var(1);
}

extern "C" int unified_entry(void* module, u32 reason, void* reserved) {
  g_base = reinterpret_cast<uptr>(module);
  module_base_slot = static_cast<uptr>(g_base);
  if (reason == 0) g_stop = 1;
  const int result = module_fn<DllEntryFn>(kOriginalEntryRva)(module, reason, reserved);
  if (!result || reason != 1) return result;

  auto load_library = imported<LoadLibraryAFn>(kIatLoadLibraryARva);
  auto get_proc = imported<GetProcAddressFn>(kIatGetProcAddressRva);
  if (load_library == nullptr || get_proc == nullptr) return result;

  void* host = module_fn<GetHostModuleFn>(kGetHostModuleRva)();
  if (host != nullptr) {
    g_get_config = reinterpret_cast<GetConfigFn>(get_proc(host, kGetConfigName));
    g_set_config = reinterpret_cast<SetConfigFn>(get_proc(host, kSetConfigName));
    g_log = reinterpret_cast<LogFn>(get_proc(host, kLogName));
  }
  load_settings();
  if (g_log != nullptr) g_log(module, 2, kInitLog);

  void* kernel32 = load_library(kKernel32);
  if (kernel32 != nullptr) {
    auto create_thread = reinterpret_cast<CreateThreadFn>(get_proc(kernel32, kCreateThread));
    if (create_thread != nullptr)
      create_thread(nullptr, 0, hotkey_worker, nullptr, 0, nullptr);
  }
  return result;
}
