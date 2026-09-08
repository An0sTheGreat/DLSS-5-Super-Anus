// Included inside dx12::capture. ReShade restores the actual swapchain image to
// PRESENT before reshade_present. Copies are nonblocking and use our existing
// shared resource budget and a dedicated post-submission fence, NOT the
// application recording events or runtime::capture_screenshot's blocking wait.
void observe_present(reshade::api::command_queue *, reshade::api::swapchain *swapchain,
    const reshade::api::rect *, const reshade::api::rect *, unsigned, const reshade::api::rect *)
{
    if (!swapchain || swapchain->get_device()->get_api() != reshade::api::device_api::d3d12 ||
        !TryEnterCriticalSection(&g_render_mutex)) return;
    const auto window = swapchain->get_hwnd();
    for (auto &item : displays) if (item.window == window || !item.window || GetTickCount64()-item.at > 2000) {
        item = {window,swapchain->get_device(),swapchain->get_color_space(),GetTickCount64()}; break;
    }
    LeaveCriticalSection(&g_render_mutex);
}
void final_screen(reshade::api::effect_runtime *runtime)
{
    const unsigned phase = final_phase.load();
    if (!runtime || (phase != 1 && phase != 2) || !TryEnterCriticalSection(&g_render_mutex)) return;
    struct Unlock { ~Unlock() { LeaveCriticalSection(&g_render_mutex); } } unlock;
    const auto now = GetTickCount64();
    if (final_phase.load() != phase || (phase == 2 && !hold_final_pair(now))) return;
    auto *device = runtime->get_device();
    if (!device || device->get_api() != reshade::api::device_api::d3d12) return;
    const auto window = runtime->get_hwnd();
    if (phase == 2 && (runtime != final_runtime || window != final_window)) return;
    if (phase == 1 && (!nr_enabled() || g_successful_evaluations.load() == final_success_baseline)) return;
    const auto fail = [](const char *message) {
        reject(message); armed.store(false); g_capture_off_until.store(0);
        final_phase.store(slot == SIZE_MAX ? 0u : 3u); pair_complete = false;
        if (slot == SIZE_MAX) status.store(5);
        log_message(reshade::log::level::warning,"NR final-screen capture cancelled: %s",message);
    };
    if (phase == 2 && (!nr_enabled() || final_preset != field<int>(g_target_module,kPresetIndexRva) ||
        final_generation != g_stream_generation.load() || final_hook != field<float>(g_target_module,0x270FB0))) {
        fail("Settings changed during capture; no pair saved."); return;
    }
    if (phase == 2 && !final_timing.ready(now,g_capture_skipped.load(),g_successful_evaluations.load())) return;
    reshade::api::color_space space = reshade::api::color_space::unknown;
    for (const auto &item : displays) if (item.window == window && item.device == device && now-item.at < 2000) space = item.space;
    if (space != reshade::api::color_space::scrgb && space != reshade::api::color_space::hdr10_pq &&
        space != reshade::api::color_space::srgb) { reject("Waiting for a known swapchain color space."); return; }
    const auto source = runtime->get_current_back_buffer();
    const auto desc = device->get_resource_desc(source);
    const unsigned fmt = static_cast<unsigned>(shader_view_format(desc.texture.format));
    const unsigned bpp = nr::screenshots::pixel_bytes(fmt);
    const auto encoding = space == reshade::api::color_space::hdr10_pq ? nr::screenshots::Encoding::pq2020 :
        space == reshade::api::color_space::scrgb ? nr::screenshots::Encoding::scrgb : nr::screenshots::Encoding::srgb;
    if (!source.handle || desc.type != reshade::api::resource_type::texture_2d || desc.texture.samples != 1 ||
        desc.texture.levels != 1 || desc.texture.depth_or_layers != 1 || !desc.texture.width || !desc.texture.height ||
        desc.texture.width > 16384 || desc.texture.height > 16384 || !bpp ||
        (space == reshade::api::color_space::scrgb && fmt != 10 && fmt != 2)) {
        fail("Unsupported final backbuffer format; native output unchanged."); return;
    }
    auto *queue = runtime->get_command_queue();
    auto *cmd = queue ? queue->get_immediate_command_list() : nullptr;
    if (!cmd) { fail("No graphics command list for final capture."); return; }
    if (phase == 1) {
        const float white = encoding == nr::screenshots::Encoding::srgb ? 80.f :
            nr::screenshots::display_white_nits(static_cast<HWND>(window));
        if (!white) { fail("Windows display white-level query failed; refusing uncalibrated HDR output."); return; }
        const unsigned pitch = (desc.texture.width*bpp+255)&~255u;
        const std::uint64_t bytes = static_cast<std::uint64_t>(pitch)*desc.texture.height;
        std::uint64_t used = 0; for (const auto &set : g_resource_sets) if (set.active) used += set.allocated_bytes;
        if (!allocation_fits(used,2*bytes,kWorkingTextureBudget)) { fail("Final pair exceeds the shared 512 MiB budget."); return; }
        ResourceSet *set = nullptr;
        for (auto &candidate : g_resource_sets) if (!candidate.active) { set = &candidate; break; }
        if (!set) { fail("No free tracked slot for final capture."); return; }
        reshade::api::resource buffers[2] = {};
        const reshade::api::resource_desc readback(bytes,reshade::api::memory_heap::gpu_to_cpu,reshade::api::resource_usage::copy_dest);
        if (!device->create_resource(readback,nullptr,reshade::api::resource_usage::copy_dest,&buffers[0]) ||
            !device->create_resource(readback,nullptr,reshade::api::resource_usage::copy_dest,&buffers[1])) {
            if (buffers[0].handle) device->destroy_resource(buffers[0]);
            fail("Final pair readback allocation failed."); return;
        }
        *set = {}; set->active = true; set->capture = true; set->device = device;
        set->work_color = buffers[0]; set->work_output = buffers[1]; set->allocated_bytes = 2*bytes;
        if (FAILED(reinterpret_cast<ID3D12Device *>(device->get_native())->CreateFence(0,D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&set->capture_fence)))) {
            for (auto buffer : buffers) device->destroy_resource(buffer);
            *set = {}; fail("Final capture fence creation failed."); return;
        }
        set->capture_queue = queue->get_native();
        width = desc.texture.width; height = desc.texture.height;
        set->display_width = width; set->display_height = height;
        formats[0] = formats[1] = fmt; pitches[0] = pitches[1] = pitch; encodings[0] = encodings[1] = encoding;
        slot = static_cast<std::size_t>(set-g_resource_sets.data()); watched_device = device; watched_command = cmd;
        final_runtime = runtime; final_window = window; capture_hdr = true; capture_white_nits = white;
        final_preset = field<int>(g_target_module,kPresetIndexRva); final_hook = field<float>(g_target_module,0x270FB0);
        final_generation = g_stream_generation.load(); pair_complete = false;
    } else if (slot == SIZE_MAX || watched_device != device || width != desc.texture.width || height != desc.texture.height ||
        formats[1] != fmt || encodings[1] != encoding) { fail("Backbuffer changed during capture; no pair saved."); return; }
    auto &set = g_resource_sets[slot];
    if (set.capture_queue != queue->get_native()) { fail("Graphics queue changed during capture."); return; }
    const auto copy_started = GetTickCount64();
    cmd->barrier(source,reshade::api::resource_usage::present,reshade::api::resource_usage::copy_source);
    cmd->copy_texture_to_buffer(source,0,nullptr,phase == 1 ? set.work_output : set.work_color,0,pitches[1]/bpp,height);
    cmd->barrier(source,reshade::api::resource_usage::copy_source,reshade::api::resource_usage::present);
    // The API signal flushes its immediate list first. No wait/map is done here.
    // Signal failure retains the bounded buffers; it is not proof of completion.
    if (!queue->signal({reinterpret_cast<std::uint64_t>(set.capture_fence)},++set.capture_fence_value)) {
        set.unsafe_tracking = true; fail("Final capture submission/fence failed; resources retained safely."); return;
    }
    if (phase == 1) {
        final_timing.start(copy_started,g_capture_skipped.load(),g_successful_evaluations.load());
        if (final_timing.expired(GetTickCount64())) { fail("ON recording exceeded 500 ms; NR was not disabled."); return; }
        g_capture_off_until.store(final_timing.deadline);
        final_phase.store(2); armed.store(false); status.store(2);
        log_message(reshade::log::level::info,"NR final ON recorded: %ux%u format=%u color-space=%u SDR-white=%.1f nits; deadline=500 ms.",
            width,height,fmt,static_cast<unsigned>(space),static_cast<double>(capture_white_nits));
    } else {
        g_capture_off_until.store(0); final_phase.store(3); pair_complete = true;
        const auto finished = GetTickCount64();
        if (final_timing.expired(finished)) { fail("OFF recording exceeded the 500 ms pair limit."); return; }
        log_message(reshade::log::level::info,"NR final OFF recorded: gap=%llu ms; NR restored without a preset transaction; waiting for GPU fences.",finished-final_timing.on_at);
    }
}
