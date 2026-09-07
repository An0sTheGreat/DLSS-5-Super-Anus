// Included inside dx12 after tracked resources are declared. Captures use the
// same recording pins and post-recording queue fences as resource retirement.
// Never map/free a buffer merely because a number of frames elapsed.
namespace capture {
std::atomic_bool armed = false, writing = false;
std::atomic_bool deferred_request = false;
std::atomic_uint status = 0; // 0 idle, 1 armed, 2 GPU, 3 writing, 4 saved, 5 failed
std::atomic<const char *> failure = "No compatible capture reached. Try Auto/Upscaled.";
std::atomic_uint native_codec_calls = 0, api_codec_calls = 0;
std::atomic_uint observed_encoding = 0;
std::atomic_uint bypass_calls = 0;
// Scoped to the verified no-codec parent, not a guess based on hook selection.
// Protected by g_render_mutex; no PE TLS is required by the grafted component.
struct BypassScope {
    void *parent = nullptr;
    reshade::api::command_list *command = nullptr;
    unsigned thread = 0, passes = 0, depth = 0;
} bypass;
std::uint64_t reject(const char *reason) { failure.store(reason); return 0; }
ULONGLONG requested_at = 0;
std::size_t slot = SIZE_MAX;
reshade::api::resource watched_color = {};
reshade::api::command_list *watched_command = nullptr;
reshade::api::device *watched_device = nullptr;
bool pair_complete = false, capture_hdr = false, gpu_wait_reported = false;
unsigned formats[2] = {}, pitches[2] = {}, width = 0, height = 0;
nr::CaptureChain chain;
void *watched_codec = nullptr;
unsigned watched_encoding = 0;
std::atomic_uint final_phase = 0; // 1 waiting ON, 2 waiting OFF, 3 draining
nr::FinalCaptureTiming final_timing;
void *final_window = nullptr;
reshade::api::effect_runtime *final_runtime = nullptr; // identity only, never retained/dereferenced
unsigned final_success_baseline = 0, final_generation = 0;
int final_preset = 0;
float final_hook = 0, capture_white_nits = 80.f;
struct DisplayObservation {
    void *window = nullptr;
    reshade::api::device *device = nullptr;
    reshade::api::color_space space = reshade::api::color_space::unknown;
    ULONGLONG at = 0;
};
std::array<DisplayObservation,4> displays = {};
bool hold_final_pair(ULONGLONG now)
{
    if (final_phase.load() != 2) return false;
    if (!final_timing.expired(now)) return true;
    g_capture_off_until.store(0); final_phase.store(3); pair_complete = false;
    failure.store("Final-frame pair exceeded 500 ms; NR restored, no images saved.");
    log_text(reshade::log::level::warning,"NR final-screen capture expired at 500 ms; suppression expired automatically. No pair saved.");
    return false;
}
reshade::api::device *pipeline_device = nullptr;
reshade::api::pipeline_layout copy_layout = {};
reshade::api::pipeline copy_pipeline = {};
nr::screenshots::Encoding encodings[2] = {};
struct Job {
    nr::screenshots::Image images[2];
    bool hdr;
    std::uint64_t tick;
    unsigned evaluation;
    char root_utf8[1024];
    wchar_t root[1200], paths[2][1400];
};
void discard(ResourceSet &set)
{
    if (slot != static_cast<std::size_t>(&set-g_resource_sets.data())) return;
    slot = SIZE_MAX; armed.store(false); watched_color = {}; watched_device = nullptr;
    watched_command = nullptr; chain.abort();
    final_phase.store(0); g_capture_off_until.store(0); final_runtime = nullptr;
    if (status.load() != 3 && status.load() != 4) status.store(5);
}
DWORD WINAPI write_job(void *argument)
{
    auto *job = static_cast<Job *>(argument);
    auto &root_utf8 = job->root_utf8;
    std::size_t root_size = sizeof(root_utf8);
    const auto get_host = reinterpret_cast<GetHostModuleFunction>(reinterpret_cast<std::uintptr_t>(g_target_module)+kGetHostModuleRva);
    using BasePath = void (*)(char *, std::size_t *);
    const auto get_base = reinterpret_cast<BasePath>(GetProcAddress(get_host(),"ReShadeGetBasePath"));
    if (get_base) get_base(root_utf8,&root_size);
    auto &root = job->root;
    auto &paths = job->paths;
    bool ok = root_utf8[0] && root_size < sizeof(root_utf8) &&
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,root_utf8,-1,root,1100) > 0;
    bool made[2] = {};
    if (ok) {
        const std::size_t length = std::wcslen(root);
        std::swprintf(root+length,1200-length,L"\\DLSS5 Screenshots");
        ok = CreateDirectoryW(root,nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
    }
    if (ok) {
        const wchar_t *suffixes[] = {L".png",L"_NR_OFF.png"};
        for (unsigned i = 0; i < 2; ++i)
            std::swprintf(paths[i],1400,L"%ls\\NR_CAPTURE_%llu_%u%ls",root,job->tick,job->evaluation,suffixes[i]);
        // ON then OFF uses one immutable captured pair, without changing NR settings.
        made[0] = nr::screenshots::write_png(paths[0],job->images[1],job->hdr);
        made[1] = made[0] && nr::screenshots::write_png(paths[1],job->images[0],job->hdr);
        ok = made[0] && made[1];
        // Roll back only files this request successfully created. Existing files
        // are never overwritten or removed, including on a naming collision.
        if (!ok) for (unsigned i = 0; i < 2; ++i) if (made[i]) DeleteFileW(paths[i]);
    }
    for (auto &image : job->images) HeapFree(GetProcessHeap(),0,const_cast<unsigned char *>(image.pixels));
    log_message(ok ? reshade::log::level::info : reshade::log::level::error,
        "NR screenshot %s: NR_CAPTURE_%llu_%u in ReShade base/DLSS5 Screenshots (%s).",
        ok ? "pair written" : "failed; no partial pair retained",job->tick,job->evaluation,
        job->hdr ? "HDR-to-SDR PNG only" : "PNG only");
    if (!ok) failure.store("PNG encoding or file creation failed; check the screenshot folder permissions.");
    HeapFree(GetProcessHeap(),0,job); status.store(ok ? 4u : 5u); writing.store(false);
    return 0;
}
void finish(ResourceSet &set)
{
    if (slot != static_cast<std::size_t>(&set-g_resource_sets.data()) || !pair_complete || set.unsafe_tracking) {
        failure.store("Capture interrupted before a complete pair; no images saved."); status.store(5);
        log_text(reshade::log::level::warning,"NR screenshot pair aborted before completion; no files written.");
        return;
    }
    auto *job = static_cast<Job *>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Job)));
    bool ok = job != nullptr;
    const reshade::api::resource buffers[] = {set.work_color,set.work_output};
    if (job) {
        job->hdr = capture_hdr; job->tick = GetTickCount64(); job->evaluation = g_evaluation_calls.load();
        for (unsigned i = 0; i < 2; ++i) {
            const auto bytes = static_cast<std::size_t>(pitches[i])*height;
            auto *pixels = static_cast<unsigned char *>(HeapAlloc(GetProcessHeap(),0,bytes));
            job->images[i] = {pixels,width,height,pitches[i],formats[i],encodings[i]};
            job->images[i].sdr_white_nits = capture_white_nits;
            void *mapped = nullptr;
            if (pixels && set.device->map_buffer_region(buffers[i],0,bytes,reshade::api::map_access::read_only,&mapped)) {
                std::memcpy(pixels,mapped,bytes); set.device->unmap_buffer_region(buffers[i]);
            } else ok = false;
        }
    }
    HANDLE thread = nullptr;
    if (ok) {
        writing.store(true); status.store(3);
        thread = CreateThread(nullptr,0,&write_job,job,0,nullptr);
    }
    if (thread) CloseHandle(thread);
    else {
        if (job) { for (auto &image : job->images) if (image.pixels) HeapFree(GetProcessHeap(),0,const_cast<unsigned char *>(image.pixels)); HeapFree(GetProcessHeap(),0,job); }
        failure.store("Readback allocation, mapping or worker creation failed."); writing.store(false); status.store(5);
        log_text(reshade::log::level::error,"NR screenshot readback/worker failed; no files written.");
    }
}
void reset_command(reshade::api::command_list *cmd)
{
    if (!final_phase.load() && cmd == watched_command && !pair_complete) chain.abort();
}
void forget_resource(reshade::api::device *device, reshade::api::resource resource)
{
    if (device == watched_device && resource == watched_color && !pair_complete) chain.abort();
}
void destroy_pipeline(reshade::api::device *device)
{
    if (device != pipeline_device) return;
    const auto pipeline = copy_pipeline;
    const auto layout = copy_layout;
    copy_pipeline = {}; copy_layout = {}; pipeline_device = nullptr;
    if (pipeline.handle) device->destroy_pipeline(pipeline);
    if (layout.handle) device->destroy_pipeline_layout(layout);
}
bool initialize_pipeline(reshade::api::device *device)
{
    if (pipeline_device) return device == pipeline_device && copy_pipeline.handle != 0;
    pipeline_device = device;
    const reshade::api::descriptor_range srv = {0,0,0,1,reshade::api::shader_stage::all_compute,1,
        reshade::api::descriptor_type::texture_shader_resource_view};
    const reshade::api::descriptor_range uav = {0,0,0,1,reshade::api::shader_stage::all_compute,1,
        reshade::api::descriptor_type::texture_unordered_access_view};
    const reshade::api::constant_range constants = {0,0,0,4,reshade::api::shader_stage::all_compute};
    const reshade::api::pipeline_layout_param params[] = {
        reshade::api::pipeline_layout_param(1,&srv), reshade::api::pipeline_layout_param(1,&uav),
        reshade::api::pipeline_layout_param(constants)};
    reshade::api::shader_desc shader = {g_screenshot_copy_shader,g_screenshot_copy_shader_size,"main"};
    const reshade::api::pipeline_subobject object = {reshade::api::pipeline_subobject_type::compute_shader,1,&shader};
    if (device->create_pipeline_layout(3,params,&copy_layout) && device->create_pipeline(copy_layout,1,&object,&copy_pipeline)) return true;
    destroy_pipeline(device); return false;
}
std::uint64_t before(void *input, reshade::api::resource_view borrowed_source, unsigned encoding, unsigned passes = 1)
{
    if (final_phase.load() || (!armed.load() && status.load() != 2) || !input || !nr_enabled()) return 0;
    ScopedLock lock(g_render_mutex);
    auto *cmd = field<reshade::api::command_list *>(input,0);
    auto *record = find_command_list_locked(cmd);
    if (!record || !nr::backends::handles(record->device)) return reject("Capture command list is not tracked by the DX12 backend.");
    cmd = record->command_list; // The intercepted ABI can also contain a native handle.
    const unsigned pass = field<unsigned>(input,8);
    if (slot != SIZE_MAX) return chain.enter(reinterpret_cast<std::uintptr_t>(cmd),GetCurrentThreadId(),pass);
    if (!armed.load() || writing.load() || pass != 0 || !passes || passes > 10) return 0;
    const auto color = reshade::api::resource{field<std::uint64_t>(input,0x10)};
    auto *device = record->device;
    const auto output = reshade::api::resource{field<std::uint64_t>(input,0x18)};
    if (!color.handle || !output.handle) return 0;
    // Reconstruction writes UAV output. Other output-state contracts are not
    // certified for capture; keep evaluation unchanged instead of guessing.
    if (field<unsigned>(input,0x40) != static_cast<unsigned>(reshade::api::resource_usage::unordered_access)) return 0;
    const auto descs = std::array{device->get_resource_desc(color),device->get_resource_desc(output)};
    width = field<unsigned>(input,0x44); height = field<unsigned>(input,0x48);
    if (!width || !height || width > 16384 || height > 16384) return 0;
    const unsigned x = field<unsigned>(input,0x60), y = field<unsigned>(input,0x64);
    const unsigned color_width = field<unsigned>(input,0x68), color_height = field<unsigned>(input,0x6c);
    if ((color_width && color_width != width) || (color_height && color_height != height)) return 0;
    std::uint64_t total = 0;
    capture_hdr = g_capture_hdr.load();
    for (unsigned i = 0; i < 2; ++i) {
        const auto &d = descs[i];
        if (d.type != reshade::api::resource_type::texture_2d || d.texture.samples != 1 || d.texture.depth_or_layers != 1 ||
            d.texture.levels != 1 || d.texture.width < width || d.texture.height < height) return 0;
        formats[i] = static_cast<unsigned>(shader_view_format(d.texture.format));
        if (!nr::screenshots::pixel_bytes(formats[i])) return 0;
        const bool linear = formats[i] == 2 || formats[i] == 10 || formats[i] == 26;
        // Verified codec modes: 2 PQ BT.2020, 3 linear BT.709, 4 linear scRGB.
        // Mode 1 (scRGB-nl) is not yet supported; do not guess its transfer.
        if (encoding != 0 && encoding != 2 && encoding != 3 && encoding != 4) return reject("Capture encoding is unsupported (including scRGB-nl). Try Auto/Upscaled with Encoding Auto.");
        if (capture_hdr && encoding != 0 && encoding != 2 && !linear) return reject("HDR capture requires PQ or a floating-point linear source.");
        encodings[i] = encoding == 0 ? nr::screenshots::Encoding::srgb : encoding == 2 ? nr::screenshots::Encoding::pq2020 :
            encoding == 4 ? nr::screenshots::Encoding::scrgb : nr::screenshots::Encoding::linear709;
        if (i == 0) formats[i] = 2; // exact texel Load to owned float32 RGBA
        const auto bytes = nr::screenshots::pixel_bytes(formats[i]);
        pitches[i] = (width*bytes+255) & ~255u;
        total += static_cast<std::uint64_t>(pitches[i])*height;
    }
    if (x > descs[0].texture.width-width || y > descs[0].texture.height-height) return 0;
    const reshade::api::resource_desc snapshot_desc(width,height,1,1,reshade::api::format::r32g32b32a32_float,1,
        reshade::api::memory_heap::gpu_only,reshade::api::resource_usage::unordered_access | reshade::api::resource_usage::copy_source);
    // Query actual texture allocation, not just texel count.
    D3D12_RESOURCE_DESC native_desc = {};
    native_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; native_desc.Width = width; native_desc.Height = height;
    native_desc.DepthOrArraySize = 1; native_desc.MipLevels = 1; native_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    native_desc.SampleDesc.Count = 1; native_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto allocation = reinterpret_cast<ID3D12Device *>(device->get_native())->GetResourceAllocationInfo(0,1,&native_desc);
    if (allocation.SizeInBytes == UINT64_MAX || !allocation.SizeInBytes) return 0;
    total += allocation.SizeInBytes;
    std::uint64_t used = 0; for (const auto &set : g_resource_sets) if (set.active) used += set.allocated_bytes;
    if (!allocation_fits(used,total,kWorkingTextureBudget)) return reject("Screenshot exceeds the shared 512 MiB texture/readback budget.");
    if (!initialize_pipeline(device)) return reject("Screenshot copy pipeline creation failed or another device owns it.");
    ResourceSet *set = nullptr;
    for (auto &candidate : g_resource_sets) if (!candidate.active) { set = &candidate; break; }
    if (!set) return reject("No free tracked resource slot for screenshot.");
    reshade::api::resource buffers[2] = {};
    for (unsigned i = 0; i < 2; ++i) {
        const reshade::api::resource_desc desc(static_cast<std::uint64_t>(pitches[i])*height,
            reshade::api::memory_heap::gpu_to_cpu,reshade::api::resource_usage::copy_dest);
        if (!device->create_resource(desc,nullptr,reshade::api::resource_usage::copy_dest,&buffers[i])) {
            if (buffers[0].handle) device->destroy_resource(buffers[0]); return 0;
        }
    }
    reshade::api::resource snapshot = {};
    auto source_srv = borrowed_source;
    const bool own_source = !source_srv.handle;
    if (own_source && !device->create_resource_view(color,reshade::api::resource_usage::shader_resource,
        reshade::api::resource_view_desc(shader_view_format(descs[0].texture.format)),&source_srv)) {
        for (auto buffer : buffers) device->destroy_resource(buffer);
        return reject("No-codec screenshot source view creation failed.");
    }
    // An sRGB SRV decodes texels during Load; only the snapshot is linear.
    if (encoding == 0 && (static_cast<unsigned>(shader_view_format(descs[0].texture.format)) == 29 ||
        static_cast<unsigned>(shader_view_format(descs[0].texture.format)) == 91))
        encodings[0] = nr::screenshots::Encoding::linear709;
    reshade::api::resource_view snapshot_uav = {};
    if (!device->create_resource(snapshot_desc,nullptr,reshade::api::resource_usage::unordered_access,&snapshot) ||
        !source_srv.handle ||
        !device->create_resource_view(snapshot,reshade::api::resource_usage::unordered_access,
            reshade::api::resource_view_desc(reshade::api::format::r32g32b32a32_float),&snapshot_uav)) {
        if (snapshot_uav.handle) device->destroy_resource_view(snapshot_uav);
        if (snapshot.handle) device->destroy_resource(snapshot);
        if (own_source) device->destroy_resource_view(source_srv);
        for (auto buffer : buffers) device->destroy_resource(buffer);
        return 0;
    }
    *set = {}; set->active = true; set->capture = true; set->device = device;
    set->work_color = buffers[0]; set->work_output = buffers[1]; set->allocated_bytes = total;
    // Source SRV belongs to the codec heap. Never free a borrowed descriptor.
    set->work_motion = snapshot; set->work_motion_uav = snapshot_uav;
    if (own_source) set->source_color_srv = source_srv;
    set->display_width = width; set->display_height = height;
    slot = static_cast<std::size_t>(set-g_resource_sets.data());
    watched_command = cmd; watched_device = device; watched_color = color;
    // One outer codec transaction encompasses all of the native NR passes.
    chain.start(reinterpret_cast<std::uintptr_t>(cmd),GetCurrentThreadId(),passes);
    const auto ticket = chain.enter(reinterpret_cast<std::uintptr_t>(cmd),GetCurrentThreadId(),0);
    pair_complete = false; record->references.recording(); record->references.sets |= 1ull<<slot;
    // NR already requires compute-readable input. Read it without modifying its
    // state; only our owned snapshot transitions to COPY_SOURCE.
    const reshade::api::descriptor_table_update srv = {{},0,0,1,reshade::api::descriptor_type::texture_shader_resource_view,&source_srv};
    const reshade::api::descriptor_table_update uav = {{},0,0,1,reshade::api::descriptor_type::texture_unordered_access_view,&snapshot_uav};
    const unsigned constants[] = {x,y,width,height};
    cmd->bind_pipeline(reshade::api::pipeline_stage::all_compute,copy_pipeline);
    cmd->push_descriptors(reshade::api::shader_stage::all_compute,copy_layout,0,srv);
    cmd->push_descriptors(reshade::api::shader_stage::all_compute,copy_layout,1,uav);
    cmd->push_constants(reshade::api::shader_stage::all_compute,copy_layout,2,0,4,constants);
    cmd->dispatch((width+7)/8,(height+7)/8,1);
    cmd->barrier(snapshot,reshade::api::resource_usage::unordered_access,reshade::api::resource_usage::copy_source);
    cmd->copy_texture_to_buffer(snapshot,0,nullptr,buffers[0],0,pitches[0]/16,height);
    armed.store(false); status.store(2);
    log_message(reshade::log::level::info,"NR screenshot recorded native input: %ux%u output format=%u NR passes=%u encoding=%u HDR=%u; %.0f MiB owned GPU/readback storage.",
        width,height,formats[1],field<unsigned>(g_target_module,0x266fa4),encoding,capture_hdr ? 1u : 0u,static_cast<double>(total)/(1u<<20));
    return ticket;
}
void after(void *input, std::uint64_t result, std::uint64_t ticket)
{
    ScopedLock lock(g_render_mutex);
    if (slot == SIZE_MAX || pair_complete || !input) return;
    if (!chain.leave(ticket,(result & 255) == 1)) return;
    auto *cmd = field<reshade::api::command_list *>(input,0);
    auto *record = find_command_list_locked(cmd);
    auto &set = g_resource_sets[slot];
    if (!record || record->device != set.device || record->command_list != watched_command) return;
    cmd = record->command_list;
    const auto output = reshade::api::resource{field<std::uint64_t>(input,0x18)};
    const auto desc = set.device->get_resource_desc(output);
    const unsigned x = field<unsigned>(input,0x70), y = field<unsigned>(input,0x74);
    if (desc.type != reshade::api::resource_type::texture_2d || desc.texture.samples != 1 || desc.texture.levels != 1 ||
        desc.texture.depth_or_layers != 1 || desc.texture.width < width || desc.texture.height < height || x > desc.texture.width-width || y > desc.texture.height-height ||
        static_cast<unsigned>(shader_view_format(desc.texture.format)) != formats[1]) return;
    record->references.recording(); record->references.sets |= 1ull<<slot;
    const auto old = static_cast<reshade::api::resource_usage>(field<unsigned>(input,0x40));
    if (old != reshade::api::resource_usage::unordered_access) return;
    cmd->barrier(output,old,reshade::api::resource_usage::copy_source);
    const reshade::api::subresource_box box{x,y,0,x+width,y+height,1};
    cmd->copy_texture_to_buffer(output,0,&box,set.work_output,0,pitches[1]/nr::screenshots::pixel_bytes(formats[1]),height);
    cmd->barrier(output,reshade::api::resource_usage::copy_source,old);
    pair_complete = true;
}
void request()
{
    log_message(reshade::log::level::info,"NR screenshot request received: hook=%.1f enabled=%u HDR=%u status=%u.",
        static_cast<double>(field<float>(g_target_module,0x270FB0)), nr_enabled() ? 1u : 0u,
        g_capture_hdr.load() ? 1u : 0u,status.load());
    // Overlay callbacks may own a presentation/queue lock. Never wait for the
    // render mutex here; defer an unserved request to a later overlay tick.
    if (!TryEnterCriticalSection(&g_render_mutex)) {
        deferred_request.store(true);
        log_text(reshade::log::level::info,"NR screenshot request deferred: render lock busy; no buffers allocated."); return;
    }
    struct Unlock { ~Unlock() { LeaveCriticalSection(&g_render_mutex); } } unlock;
    deferred_request.store(false);
    if (!nr_enabled() || !g_lifetime_events_registered) {
        failure.store("Enable Neural Rendering; capture also requires registered lifetime tracking.");
        status.store(5); log_text(reshade::log::level::warning,"NR screenshot requires active Neural Rendering and lifetime tracking."); return;
    }
    if (slot != SIZE_MAX || writing.load()) { log_text(reshade::log::level::warning,"NR screenshot still pending; no additional buffers allocated."); return; }
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&write_job),&pinned)) {
        const DWORD error = GetLastError();
        failure.store("Unable to retain addon module for screenshot worker."); status.store(5);
        log_message(reshade::log::level::error,"NR screenshot module pin failed: Win32=%lu. No capture started.",error); return;
    }
    requested_at = GetTickCount64(); watched_color = {}; chain.abort(); gpu_wait_reported = false;
    capture_white_nits = 80.f;
    const bool final_screen = g_capture_hdr.load() &&
        g_runtime_api.load() == static_cast<unsigned>(reshade::api::device_api::d3d12);
    final_phase.store(final_screen ? 1u : 0u);
    final_runtime = nullptr; final_window = nullptr;
    final_success_baseline = g_successful_evaluations.load();
    armed.store(true); status.store(1);
    failure.store("No compatible codec capture reached. FrameGen/Present may use another path; try Auto/Upscaled.");
    native_codec_calls.store(0); api_codec_calls.store(0); observed_encoding.store(0); bypass_calls.store(0);
    log_text(reshade::log::level::info,"NR screenshot armed for next compatible successful evaluation.");
    if (final_screen) log_text(reshade::log::level::info,"NR final-screen HDR pair armed: target <100 ms, reject pair at 500 ms; PNG only, preset unchanged.");
}
void tick()
{
    if (!deferred_request.load() && !armed.load() && status.load() != 2) return;
    if (!TryEnterCriticalSection(&g_render_mutex)) return;
    struct Unlock { ~Unlock() { LeaveCriticalSection(&g_render_mutex); } } unlock;
    if (deferred_request.exchange(false)) request();
    hold_final_pair(GetTickCount64());
    if (armed.load() && GetTickCount64()-requested_at >= 10000) {
        armed.store(false); status.store(5);
        final_phase.store(0); g_capture_off_until.store(0);
        log_message(reshade::log::level::warning,"NR screenshot timed out: native-codec=%u API-codec=%u no-codec=%u encoding=%u hook=%.1f; %s No files written.",
            native_codec_calls.load(),api_codec_calls.load(),bypass_calls.load(),observed_encoding.load(),
            static_cast<double>(field<float>(g_target_module,0x270FB0)),failure.load());
    }
    if (slot != SIZE_MAX && !gpu_wait_reported && GetTickCount64()-requested_at >= 10000) {
        gpu_wait_reported = true;
        log_text(reshade::log::level::warning,"NR screenshot pending: recording references/GPU fences have not drained. Holding one bounded request; not mapping or freeing unsafely.");
    }
}

// Parent 573B0 bypasses both codecs when its encoding byte is zero. Observe
// the existing evaluations in that scope; never replay NR or change hooks.
std::uint64_t parent(void *descriptor)
{
    using Original = std::uint64_t (*)(void *);
    const auto original = reinterpret_cast<Original>(reinterpret_cast<std::uintptr_t>(g_target_module)+0x573B0);
    if (final_phase.load() || (!armed.load() && status.load() != 2) || !descriptor) return original(descriptor);
    bool owner = false, nested = false;
    {
        ScopedLock lock(g_render_mutex);
        if (bypass.parent && bypass.thread == GetCurrentThreadId()) {
            ++bypass.depth; nested = true;
            // Nested chains must not satisfy an outer screenshot transaction.
            if (watched_codec == bypass.parent) chain.abort();
        } else if (!bypass.parent && armed.load() && field<unsigned char>(descriptor,0x38) == 0) {
            bypass = {descriptor,field<reshade::api::command_list *>(descriptor,0),GetCurrentThreadId(),
                field<unsigned>(g_target_module,0x266fa4),1};
            bypass_calls.fetch_add(1); observed_encoding.store(0); owner = true;
        }
    }
    const auto result = original(descriptor);
    {
        ScopedLock lock(g_render_mutex);
        if (nested) --bypass.depth;
        if (owner) {
            if (watched_codec == descriptor && slot != SIZE_MAX && ((result & 255) != 1 || !chain.complete)) {
                pair_complete = false; chain.abort();
                failure.store("No-codec NR transaction did not complete; no pair saved.");
            }
            bypass = {};
        }
    }
    return result;
}
std::uint64_t before_bypass(void *input)
{
    if ((!armed.load() && status.load() != 2) || !input) return 0;
    ScopedLock lock(g_render_mutex);
    if (!bypass.parent || bypass.depth != 1 || bypass.thread != GetCurrentThreadId()) return 0;
    auto *record = find_command_list_locked(field<reshade::api::command_list *>(input,0));
    if (!record || record->command_list != bypass.command) return reject("No-codec NR command list is not tracked for safe capture.");
    const auto ticket = before(input,{},0,bypass.passes);
    if (ticket) { watched_codec = bypass.parent; watched_encoding = 0; }
    return ticket;
}

#include "final_screen_capture.inl"

// Hash-verified EF250 codec ABI: native command, codec object, encoding,
// diffuse-white bits, uint4 source rectangle, encode(0)/decode(1).
void codec(void *native_command, void *object, unsigned encoding, unsigned balance, const unsigned *rect, unsigned char decode)
{
    using Original = void (*)(void *,void *,unsigned,unsigned,const unsigned *,unsigned char);
    const auto original = reinterpret_cast<Original>(reinterpret_cast<std::uintptr_t>(g_target_module)+0xEF250);
    if (!armed.load() && status.load() != 2) { original(native_command,object,encoding,balance,rect,decode); return; }
    native_codec_calls.fetch_add(1); observed_encoding.store(encoding & 255);
    alignas(8) unsigned char input[kInputSize] = {};
    bool compatible = false;
    {
        ScopedLock lock(g_render_mutex);
        auto *record = find_command_list_locked(static_cast<reshade::api::command_list *>(native_command));
        if (!record && native_command) {
            reshade::api::command_list *api = nullptr; UINT bytes = sizeof(api);
            const auto *guid = reinterpret_cast<const GUID *>(reinterpret_cast<std::uintptr_t>(g_target_module)+0x22AA30);
            if (SUCCEEDED(static_cast<ID3D12GraphicsCommandList *>(native_command)->GetPrivateData(*guid,&bytes,&api)) && bytes == sizeof(api))
                record = find_command_list_locked(api);
        }
        if (!record) reject("Native codec command list has no tracked ReShade wrapper.");
        else if (!object || !rect || rect[0] || rect[1] || !rect[2] || !rect[3]) reject("Native codec capture requires a full-size, zero-origin rectangle.");
        if (record && object && rect && !rect[0] && !rect[1] && rect[2] && rect[3]) {
            field<void *>(input,0) = record->command_list;
            field<std::uint64_t>(input,0x10) = field<std::uint64_t>(object,0x28);
            field<std::uint64_t>(input,0x18) = field<std::uint64_t>(object,0x28);
            field<unsigned>(input,0x40) = 8;
            field<unsigned>(input,0x44) = rect[2]; field<unsigned>(input,0x48) = rect[3];
            compatible = true;
            if (!decode) {
                auto *heap = field<ID3D12DescriptorHeap *>(object,0x18);
                const auto ticket = heap ? before(input,{heap->GetCPUDescriptorHandleForHeapStart().ptr},encoding & 255) : 0;
                if (ticket) { watched_codec = object; watched_encoding = encoding & 255; }
            }
        }
    }
    // Always execute the original codec exactly once, including capture failures.
    original(native_command,object,encoding,balance,rect,decode);
    if (decode && compatible) {
        ScopedLock lock(g_render_mutex);
        if (object == watched_codec && watched_encoding == (encoding & 255) &&
            chain.thread == GetCurrentThreadId() && chain.command == reinterpret_cast<std::uintptr_t>(field<void *>(input,0)))
            after(input,1,chain.ticket);
        else if (chain.active) chain.abort();
    }
}

std::uint64_t api_codec(reshade::api::command_list *cmd, void *pipeline,
    std::uint64_t source_view, std::uint64_t original_view, std::uint64_t destination_view,
    const unsigned *constants, unsigned w, unsigned h)
{
    using Original = std::uint64_t (*)(reshade::api::command_list *,void *,std::uint64_t,std::uint64_t,std::uint64_t,const unsigned *,unsigned,unsigned);
    const auto original = reinterpret_cast<Original>(reinterpret_cast<std::uintptr_t>(g_target_module)+0x5BBC0);
    if (!armed.load() && status.load() != 2) return original(cmd,pipeline,source_view,original_view,destination_view,constants,w,h);
    api_codec_calls.fetch_add(1);
    if (constants) observed_encoding.store(constants[1] & 255);
    alignas(8) unsigned char input[kInputSize] = {};
    bool compatible = false, decode = false;
    unsigned encoding = 0;
    {
        ScopedLock lock(g_render_mutex);
        auto *record = find_command_list_locked(cmd);
        if (!record) reject("API codec command list is not tracked for safe capture.");
        else if (!constants || constants[3] || constants[4] || constants[5] || constants[6] ||
            constants[7] != w || constants[8] != h || constants[0] > 1)
            reject("API codec capture requires a full-size, zero-origin rectangle.");
        if (record && constants && !constants[3] && !constants[4] && !constants[5] && !constants[6] &&
            constants[7] == w && constants[8] == h && constants[0] <= 1) {
            decode = constants[0] == 1; encoding = constants[1] & 255;
            auto *device = record->device;
            reshade::api::resource source = {}, destination = {};
            source = device->get_resource_from_view({original_view});
            if (decode) destination = device->get_resource_from_view({destination_view});
            else destination = source;
            if (source.handle && destination.handle) {
                field<void *>(input,0) = record->command_list;
                field<std::uint64_t>(input,0x10) = source.handle;
                field<std::uint64_t>(input,0x18) = destination.handle;
                field<unsigned>(input,0x40) = 8;
                field<unsigned>(input,0x44) = w; field<unsigned>(input,0x48) = h;
                compatible = true;
                if (!decode) {
                    const auto ticket = before(input,{original_view},encoding);
                    if (ticket) { watched_codec = reinterpret_cast<void *>(original_view); watched_encoding = encoding; }
                }
            }
        }
    }
    const auto result = original(cmd,pipeline,source_view,original_view,destination_view,constants,w,h);
    if (decode && compatible) {
        ScopedLock lock(g_render_mutex);
        if (watched_codec == reinterpret_cast<void *>(original_view) && watched_encoding == encoding &&
            chain.thread == GetCurrentThreadId() && chain.command == reinterpret_cast<std::uintptr_t>(field<void *>(input,0)))
            after(input,result,chain.ticket);
        else if (chain.active) chain.abort();
    }
    return result;
}
}
