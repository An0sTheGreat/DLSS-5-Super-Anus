// DX12-only resource, command-recording, feature lifetime and evaluation backend.
// Included once inside the addon namespace after shared settings/logging helpers.
// No DLL loads or GPU allocations until a verified DX12 callback/evaluation arrives.
namespace dx12
{
struct ResourceSet
{
    bool active = false;
    bool capture = false;
    bool pooled = false;
    ID3D12Fence *capture_fence = nullptr;
    std::uint64_t capture_fence_value = 0, capture_queue = 0;
    bool valid = false;
    bool inputs_are_shader_resources = false;
    reshade::api::device *device = nullptr;
    reshade::api::resource source_color = {};
    reshade::api::resource source_output = {};
    reshade::api::resource source_motion = {};
    reshade::api::resource source_depth = {};
    reshade::api::resource source_ui = {};
    reshade::api::resource source_ui_alpha = {};
    std::uint32_t display_width = 0;
    std::uint32_t display_height = 0;
    std::uint32_t work_width = 0;
    std::uint32_t work_height = 0;
    reshade::api::format color_format = reshade::api::format::unknown;
    reshade::api::format output_format = reshade::api::format::unknown;
    reshade::api::format motion_format = reshade::api::format::unknown;
    reshade::api::format depth_format = reshade::api::format::unknown;
    reshade::api::format ui_format = reshade::api::format::unknown;
    reshade::api::format ui_alpha_format = reshade::api::format::unknown;
    unsigned reset_generation = 0;
    unsigned allocation_generation = 0;
    std::uint64_t allocated_bytes = 0;
    ULONGLONG last_use = 0;
    bool retiring = false;
    bool unsafe_tracking = false;
    std::uint32_t queue_mask = 0;
    std::array<std::uint64_t, kMaximumQueues> retire_fences = {};
    bool native_feature = false;
    void *native_handle = nullptr;
    void *native_parameters = nullptr;

    reshade::api::resource_view source_color_srv = {};
    reshade::api::resource_view source_output_uav = {};
    reshade::api::resource_view source_motion_srv = {};
    reshade::api::resource_view source_depth_srv = {};
    reshade::api::resource_view source_ui_srv = {};
    reshade::api::resource_view source_ui_alpha_srv = {};

    reshade::api::resource work_color = {};
    reshade::api::resource native_color = {};
    reshade::api::resource_view native_color_srv = {}, native_color_uav = {};
    reshade::api::resource work_motion = {};
    reshade::api::resource work_depth = {};
    reshade::api::resource work_output = {};
    reshade::api::resource work_ui = {};
    reshade::api::resource work_ui_alpha = {};

    reshade::api::resource_view work_color_srv = {};
    reshade::api::resource_view work_motion_srv = {};
    reshade::api::resource_view work_depth_srv = {};
    reshade::api::resource_view work_color_uav = {};
    reshade::api::resource_view work_motion_uav = {};
    reshade::api::resource_view work_depth_uav = {};
    reshade::api::resource_view work_output_srv = {};
    reshade::api::resource_view work_output_uav = {};
    reshade::api::resource_view work_ui_srv = {};
    reshade::api::resource_view work_ui_uav = {};
    reshade::api::resource_view work_ui_alpha_srv = {};
    reshade::api::resource_view work_ui_alpha_uav = {};
};

struct TrackedCommandList
{
    bool active = false;
    reshade::api::command_list *command_list = nullptr;
    reshade::api::device *device = nullptr;
    std::uint64_t native_command_list = 0;
    std::uint64_t native_device = 0;
    RecordingReferences references;
};

struct TrackedQueue
{
    reshade::api::device *device = nullptr;
    ID3D12CommandQueue *native = nullptr;
    ID3D12Fence *fence = nullptr;
    std::uint64_t serial = 0;
};

CRITICAL_SECTION g_render_mutex = {};
bool g_render_mutex_initialized = false;
reshade::api::device *g_pipeline_device = nullptr;
reshade::api::pipeline_layout g_pipeline_layout = {};
reshade::api::pipeline g_pipeline = {};
std::array<ResourceSet, kMaximumResourceSets> g_resource_sets = {};
std::array<TrackedCommandList, kMaximumTrackedCommandLists> g_tracked_command_lists = {};
std::array<TrackedQueue, kMaximumQueues> g_tracked_queues = {};
std::size_t g_command_slot_cursor = 0;
std::atomic_uint g_command_slots_recycled = 0, g_command_slots_exhausted = 0;
nr::ScaleHistory g_scale_history;


void collect_resources_locked(ULONGLONG now);
std::uint64_t invoke_native_evaluation(void *input);

void log_fallback_once(unsigned generation, const char *reason)
{
    unsigned expected = g_logged_failure_generation.load(std::memory_order_relaxed);
    while (expected != generation)
    {
        if (g_logged_failure_generation.compare_exchange_weak(
                expected, generation, std::memory_order_relaxed))
        {
            log_message(reshade::log::level::warning,
                "RenoDX Neural Resolution: safe 100%% fallback: %s", reason);
            return;
        }
    }
}

TrackedCommandList *find_command_list_locked(reshade::api::command_list *intercepted)
{
    const std::uint64_t raw = reinterpret_cast<std::uint64_t>(intercepted);
    for (auto &tracked : g_tracked_command_lists)
        if (tracked.active &&
            (tracked.command_list == intercepted || tracked.native_command_list == raw))
            return &tracked;
    return nullptr;
}

void clear_device_tracking_locked(reshade::api::device *device)
{
    for (auto &tracked : g_tracked_command_lists)
        if (tracked.active && tracked.device == device)
            tracked = {};
}

reshade::api::format shader_view_format(reshade::api::format fmt)
{
    switch (fmt)
    {
    case reshade::api::format::d16_unorm:
    case reshade::api::format::r16_typeless:
        return reshade::api::format::r16_unorm;
    case reshade::api::format::d32_float:
    case reshade::api::format::r32_typeless:
        return reshade::api::format::r32_float;
    case reshade::api::format::d24_unorm_s8_uint:
    case reshade::api::format::r24_g8_typeless:
        return reshade::api::format::r24_unorm_x8_uint;
    case reshade::api::format::d32_float_s8_uint:
    case reshade::api::format::r32_g8_typeless:
        return reshade::api::format::r32_float_x8_uint;
    default:
        return reshade::api::format_to_default_typed(fmt, 0);
    }
}

reshade::api::format writable_format(reshade::api::format fmt)
{
    switch (shader_view_format(fmt))
    {
    case reshade::api::format::r24_unorm_x8_uint:
    case reshade::api::format::r32_float_x8_uint:
        return reshade::api::format::r32_float;
    default:
        return shader_view_format(fmt);
    }
}

void on_init_command_list(reshade::api::command_list *command_list);
// The verified 5D880 NGX wrapper receives a native ID3D12GraphicsCommandList,
// not an arbitrary ReShade object. The upstream init/destroy callbacks set and
// clear this private-data mapping. Recover it only while that command is live.
TrackedCommandList *resolve_evaluation_command_locked(void *native)
{
    auto *record = find_command_list_locked(static_cast<reshade::api::command_list *>(native));
    if (record || !native || !g_target_module) return record;
    reshade::api::command_list *api = nullptr;
    UINT bytes = sizeof(api);
    const auto *guid = reinterpret_cast<const GUID *>(reinterpret_cast<std::uintptr_t>(g_target_module)+0x22AA30);
    if (FAILED(static_cast<ID3D12GraphicsCommandList *>(native)->GetPrivateData(*guid, &bytes, &api)) ||
        bytes != sizeof(api) || !api || api->get_native() != reinterpret_cast<std::uint64_t>(native)) return nullptr;
    on_init_command_list(api);
    return find_command_list_locked(api);
}
#include "screenshot_capture.inl"

bool create_pipeline(reshade::api::device *device)
{
    if (g_pipeline.handle != 0 && g_pipeline_device == device)
        return true;
    if (g_pipeline.handle != 0 || g_pipeline_layout.handle != 0)
        return false;

    const reshade::api::descriptor_range srv_range = {
        0, 0, 0, 1, reshade::api::shader_stage::all_compute, 1,
        reshade::api::descriptor_type::texture_shader_resource_view
    };
    const reshade::api::descriptor_range uav_range = {
        0, 0, 0, 1, reshade::api::shader_stage::all_compute, 1,
        reshade::api::descriptor_type::texture_unordered_access_view
    };
    const reshade::api::constant_range constants = {
        0, 0, 0, 12, reshade::api::shader_stage::all_compute
    };
    auto input_range = srv_range; input_range.dx_register_index = 1;
    auto native_range = srv_range; native_range.dx_register_index = 2;
    const reshade::api::pipeline_layout_param params[] = {
        reshade::api::pipeline_layout_param(1, &srv_range),
        reshade::api::pipeline_layout_param(1, &uav_range),
        reshade::api::pipeline_layout_param(constants),
        reshade::api::pipeline_layout_param(1, &input_range),
        reshade::api::pipeline_layout_param(1, &native_range),
    };
    if (!device->create_pipeline_layout(static_cast<std::uint32_t>(std::size(params)), params, &g_pipeline_layout))
        return false;

    reshade::api::shader_desc shader = {
        g_neural_resample_shader, g_neural_resample_shader_size, "Resample"
    };
    const reshade::api::pipeline_subobject subobject = {
        reshade::api::pipeline_subobject_type::compute_shader, 1, &shader
    };
    if (!device->create_pipeline(g_pipeline_layout, 1, &subobject, &g_pipeline))
    {
        device->destroy_pipeline_layout(g_pipeline_layout);
        g_pipeline_layout = {};
        return false;
    }
    g_pipeline_device = device;
    return true;
}

bool create_texture_and_views(
    reshade::api::device *device,
    std::uint32_t width,
    std::uint32_t height,
    reshade::api::format format,
    reshade::api::resource &resource,
    reshade::api::resource_view &srv,
    reshade::api::resource_view &uav)
{
    if (format == reshade::api::format::unknown ||
        !device->check_format_support(format, reshade::api::resource_usage::shader_resource) ||
        !device->check_format_support(format, reshade::api::resource_usage::unordered_access))
        return false;
    const reshade::api::resource_desc desc(
        width, height, 1, 1, format, 1, reshade::api::memory_heap::gpu_only,
        reshade::api::resource_usage::shader_resource | reshade::api::resource_usage::unordered_access);
    if (!device->create_resource(desc, nullptr, reshade::api::resource_usage::unordered_access, &resource))
        return false;
    if (!device->create_resource_view(resource, reshade::api::resource_usage::shader_resource,
            reshade::api::resource_view_desc(format), &srv) ||
        !device->create_resource_view(resource, reshade::api::resource_usage::unordered_access,
            reshade::api::resource_view_desc(format), &uav))
        return false;
    return true;
}

void destroy_resource_set(ResourceSet &set)
{
    if (!set.active || set.device == nullptr)
        return;
    if (set.capture) capture::discard(set);
    const ResourceSet owned = set;
    set = {};
    auto *device = owned.device;
    if (owned.capture_fence) owned.capture_fence->Release();
    for (const auto view : {
            owned.source_color_srv, owned.source_output_uav,
            owned.source_motion_srv, owned.source_depth_srv,
            owned.source_ui_srv, owned.source_ui_alpha_srv,
            owned.work_color_srv, owned.work_motion_srv, owned.work_depth_srv,
            owned.work_color_uav, owned.work_motion_uav, owned.work_depth_uav,
            owned.work_output_srv, owned.work_output_uav,
            owned.work_ui_srv, owned.work_ui_uav,
            owned.work_ui_alpha_srv, owned.work_ui_alpha_uav })
    {
        if (view.handle != 0)
            device->destroy_resource_view(view);
    }
    if (owned.native_color_srv.handle) device->destroy_resource_view(owned.native_color_srv);
    if (owned.native_color_uav.handle) device->destroy_resource_view(owned.native_color_uav);
    if (owned.native_color.handle) device->destroy_resource(owned.native_color);
    for (const auto resource : {
            owned.work_color, owned.work_motion, owned.work_depth, owned.work_output,
            owned.work_ui, owned.work_ui_alpha })
    {
        if (resource.handle != 0)
            device->destroy_resource(resource);
    }
}

void release_source_bindings(ResourceSet &set)
{
    if (set.device != nullptr)
        for (const auto view : {
                set.source_color_srv, set.source_output_uav,
                set.source_motion_srv, set.source_depth_srv,
                set.source_ui_srv, set.source_ui_alpha_srv })
            if (view.handle != 0)
                set.device->destroy_resource_view(view);
    set.source_color_srv = {};
    set.source_output_uav = {};
    set.source_motion_srv = {};
    set.source_depth_srv = {};
    set.source_ui_srv = {};
    set.source_ui_alpha_srv = {};
    set.source_color = {};
    set.source_output = {};
    set.source_motion = {};
    set.source_depth = {};
    set.source_ui = {};
    set.source_ui_alpha = {};
}

void pool_resource_set(ResourceSet &set, ULONGLONG now)
{
    release_source_bindings(set);
    set.pooled = true;
    set.valid = true;
    set.retiring = false;
    set.unsafe_tracking = false;
    set.queue_mask = 0;
    set.retire_fences = {};
    set.last_use = now;
}

std::uint64_t texture_allocation_size(reshade::api::device *device, std::uint32_t width,
                                     std::uint32_t height, reshade::api::format format)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = static_cast<DXGI_FORMAT>(format);
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto info = reinterpret_cast<ID3D12Device *>(device->get_native())->
        GetResourceAllocationInfo(0, 1, &desc);
    return info.SizeInBytes;
}

struct NativeSlotVector
{
    NativeFeatureSlot *begin;
    NativeFeatureSlot *end;
    NativeFeatureSlot *capacity;
    std::size_t count() const
    {
        return feature_slot_count(reinterpret_cast<std::uintptr_t>(begin),
            reinterpret_cast<std::uintptr_t>(end), reinterpret_cast<std::uintptr_t>(capacity));
    }
};

NativeSlotVector &native_slots(std::uintptr_t rva)
{
    return *reinterpret_cast<NativeSlotVector *>(reinterpret_cast<std::uintptr_t>(g_target_module) + rva);
}

NativeFeatureSlot native_primary_slot()
{
    NativeFeatureSlot slot;
    slot.parameters = field<void *>(g_target_module, 0x26D820);
    slot.handle = field<void *>(g_target_module, 0x26D828);
    slot.width = field<unsigned>(g_target_module, 0x26D878);
    slot.height = field<unsigned>(g_target_module, 0x26D87C);
    return slot;
}

// All accesses to native slots occur under the host's NR mutex. Evaluation
// call sites already own it; maintenance acquires it BEFORE g_render_mutex.
bool release_native_feature(ResourceSet &set)
{
    NativeFeatureSlot *owner = nullptr;
    NativeSlotVector *retained_owner = nullptr;
    const auto primary = native_primary_slot();
    const bool primary_owner = primary.handle == set.native_handle &&
        primary.parameters == set.native_parameters;
    for (const auto rva : {0x26D8D0u, 0x26D8E8u})
    {
        auto &slots = native_slots(rva);
        const auto count = slots.count();
        if (count == SIZE_MAX) { set.unsafe_tracking = true; return false; }
        for (std::size_t i = 0; i < count; ++i)
            if (slots.begin[i].handle == set.native_handle &&
                slots.begin[i].parameters == set.native_parameters)
            {
                if (owner != nullptr || primary_owner) { set.unsafe_tracking = true; return false; }
                owner = slots.begin + i;
                if (rva == 0x26D8E8u) retained_owner = &slots;
            }
    }
    if (!primary_owner && owner == nullptr)
    {
        // The upstream owner already removed it (for example during teardown).
        // Do not call ReleaseFeature on a stale handle.
        return true;
    }
    using Release = unsigned (__cdecl *)(void *);
    const auto release = field<Release>(g_target_module, 0x26D710);
    const auto destroy_parameters = field<Release>(g_target_module, 0x26D778);
    if (release == nullptr || destroy_parameters == nullptr || release(set.native_handle) != 1)
    {
        set.unsafe_tracking = true;
        return false;
    }
    // Remove the now-invalid handle before it can be selected again. Neither
    // ReleaseFeature nor DestroyParameters is applied to game SR/RR/FG handles.
    if (primary_owner)
    {
        field<void *>(g_target_module, 0x26D820) = nullptr;
        field<void *>(g_target_module, 0x26D828) = nullptr;
        field<std::uint64_t>(g_target_module, 0x26D878) = 0;
        field<std::uint64_t>(g_target_module, 0x26D898) = 0;
        field<std::uint8_t>(g_target_module, 0x26D900) = 1;
        field<std::uint8_t>(g_target_module, 0x26D903) = 0;
    }
    else if (retained_owner != nullptr)
    {
        const auto remaining = static_cast<std::size_t>(retained_owner->end - owner - 1);
        std::memmove(owner, owner + 1, remaining * sizeof(NativeFeatureSlot));
        --retained_owner->end;
        *retained_owner->end = {};
    }
    else *owner = {};
    g_native_features_retired.fetch_add(1, std::memory_order_relaxed);
    if (destroy_parameters(set.native_parameters) != 1)
        log_text(reshade::log::level::warning,
            "NR V6.6: feature released but NGX parameter destruction failed; no retry on an ambiguous pointer.");
    return true;
}

std::uint64_t invoke_native_evaluation(void *input)
{
    ScopedLock lock(g_render_mutex);
    const auto original = reinterpret_cast<EvaluateFunction>(
        reinterpret_cast<std::uintptr_t>(g_target_module) + kEvaluateWrapperRva);
    auto *record = resolve_evaluation_command_locked(field<void *>(input, 0));
    if (record != nullptr) record->references.recording();
    const auto result = original(input);
    if ((result & 255) == 1) g_successful_evaluations.fetch_add(1, std::memory_order_relaxed);
    if (record != nullptr)
        g_evaluation_device.observe(reinterpret_cast<std::uintptr_t>(record->device), result);
    const auto now = GetTickCount64();
    if (g_frame_trace.recording(now))
    {
        nr::FrameTraceEvent event;
        event.kind = nr::TraceKind::evaluation;
        event.tick = now;
        event.thread = GetCurrentThreadId();
        event.command = reinterpret_cast<std::uint64_t>(field<void *>(input, 0));
        event.color = reinterpret_cast<std::uint64_t>(field<void *>(input, 0x10));
        event.output = reinterpret_cast<std::uint64_t>(field<void *>(input, 0x18));
        event.width = field<unsigned>(input, 0x44);
        event.height = field<unsigned>(input, 0x48);
        event.pass = field<unsigned>(input, 8);
        event.result = result;
        g_frame_trace.push(event);
    }
    const unsigned pass = field<unsigned>(input, 8);
    NativeFeatureSlot slot;
    if (pass == 0) slot = native_primary_slot();
    else
    {
        const auto &slots = native_slots(0x26D8D0);
        const auto count = slots.count();
        if (count == SIZE_MAX || pass > count) return result;
        slot = slots.begin[pass - 1];
    }
    if (slot.handle == nullptr || slot.parameters == nullptr) return result;
    ResourceSet *feature = nullptr;
    for (auto &set : g_resource_sets)
        if (set.active && set.native_feature && set.native_handle == slot.handle &&
            set.native_parameters == slot.parameters) { feature = &set; break; }
    if (feature == nullptr)
        for (auto &set : g_resource_sets)
            if (!set.active)
            {
                feature = &set;
                set.active = true;
                set.valid = true;
                set.native_feature = true;
                set.native_handle = slot.handle;
                set.native_parameters = slot.parameters;
                break;
            }
    if (feature == nullptr) return result; // Host retains ownership if registry is full.
    feature->retiring = false; // A still-live cached handle may be reused while fences drain.
    feature->retire_fences = {};
    feature->last_use = GetTickCount64();
    feature->allocation_generation = g_scale_generation.load();
    if (record == nullptr || !g_lifetime_events_registered) feature->unsafe_tracking = true;
    else
    {
        feature->device = record->device;
        record->references.sets |= 1ull << (feature - g_resource_sets.data());
    }
    return result;
}

void on_reset_recording(reshade::api::command_list *cmd)
{
    if (cmd == nullptr || !nr::backends::handles(cmd->get_device())) return;
    ScopedLock lock(g_render_mutex);
    capture::reset_command(cmd);
    if (auto *record = find_command_list_locked(cmd))
    {
        record->references.resetting();
        cmd->set_private_data(kPendingRecordingGuid, 1);
    }
}

void on_recording_pipeline(reshade::api::command_list *cmd, reshade::api::pipeline_stage,
                           reshade::api::pipeline)
{
    if (cmd == nullptr || !nr::backends::handles(cmd->get_device())) return;
    // Pipeline binds are frequent. Only the first bind following Reset takes
    // our render lock; normal binds do not contend with NR recording threads.
    std::uint64_t pending = 0;
    cmd->get_private_data(kPendingRecordingGuid, &pending);
    if (pending == 0) return;
    ScopedLock lock(g_render_mutex);
    // A valid pipeline bind after Reset belongs to the new recording. Merely
    // receiving the PRE-Reset notification is deliberately insufficient.
    if (auto *record = find_command_list_locked(cmd)) record->references.recording();
    cmd->set_private_data(kPendingRecordingGuid, 0);
}

void on_secondary_recording(reshade::api::command_list *primary, reshade::api::command_list *secondary)
{
    if (primary == nullptr || secondary == nullptr ||
        !nr::backends::handles(primary->get_device()) ||
        primary->get_device() != secondary->get_device()) return;
    ScopedLock lock(g_render_mutex);
    auto *child = find_command_list_locked(secondary);
    if (child == nullptr) return;
    if (auto *parent = find_command_list_locked(primary))
    {
        parent->references.recording();
        parent->references.sets |= child->references.sets;
    }
    else
        for (std::size_t i = 0; i < g_resource_sets.size(); ++i)
            if ((child->references.sets & (1ull << i)) != 0)
                g_resource_sets[i].unsafe_tracking = true;
}

void on_execute_recording(reshade::api::command_queue *queue, reshade::api::command_list *cmd)
{
    if (queue == nullptr || cmd == nullptr || !nr::backends::handles(queue->get_device()) ||
        queue->get_device() != cmd->get_device()) return;
    ScopedLock lock(g_render_mutex);
    const auto *record = find_command_list_locked(cmd);
    if (record == nullptr || record->references.sets == 0) return;
    auto *native = reinterpret_cast<ID3D12CommandQueue *>(queue->get_native());
    std::size_t index = g_tracked_queues.size();
    for (std::size_t i = 0; i < g_tracked_queues.size(); ++i)
        if (g_tracked_queues[i].native == native) { index = i; break; }
    if (index == g_tracked_queues.size() && native != nullptr)
        for (std::size_t i = 0; i < g_tracked_queues.size(); ++i)
        {
            auto &tracked = g_tracked_queues[i];
            if (tracked.native != nullptr) continue;
            ID3D12Fence *fence = nullptr;
            auto *device = reinterpret_cast<ID3D12Device *>(record->device->get_native());
            if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                    __uuidof(ID3D12Fence), reinterpret_cast<void **>(&fence)))) break;
            native->AddRef();
            tracked = {record->device, native, fence, 0};
            index = i;
            break;
        }
    for (std::size_t i = 0; i < g_resource_sets.size(); ++i)
    {
        if ((record->references.sets & (1ull << i)) == 0) continue;
        auto &set = g_resource_sets[i];
        if (index == g_tracked_queues.size()) set.unsafe_tracking = true;
        else set.queue_mask |= 1u << index;
    }
    // This event precedes ExecuteCommandLists. Signaling HERE would not prove
    // that this submission completed. Signals are deferred until all recordings
    // that could submit these resources have been replaced or destroyed.
}

void collect_resources_locked(ULONGLONG now)
{
    std::uint64_t recording_mask = 0;
    for (const auto &record : g_tracked_command_lists)
        if (record.active) recording_mask |= record.references.sets;
    unsigned active = 0, pinned = 0, retiring = 0, unsafe = 0, features = 0, pooled = 0;
    std::uint64_t bytes = 0;
    for (std::size_t i = 0; i < g_resource_sets.size(); ++i)
    {
        auto &set = g_resource_sets[i];
        if (!set.active) continue;
        // Pooled sets own only private working textures. Their previous source
        // descriptors have already drained behind real queue fences and can be
        // rebound without reallocating the large textures.
        if (set.pooled)
        {
            if (!nr_enabled() || g_scale_percent.load() >= 100 ||
                set.allocation_generation != g_scale_generation.load())
            {
                destroy_resource_set(set);
                g_retired_sets.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            ++active;
            ++pooled;
            bytes += set.allocated_bytes;
            continue;
        }
        // The second final-screen image has not yet been recorded. Keep the
        // one bounded pair alive until completion or its 500 ms deadline.
        if (set.capture && capture::hold_final_pair(now)) {
            ++active; bytes += set.allocated_bytes; continue;
        }
        // Final-screen copies use ReShade's immediate command list. These do
        // not produce application recording/reset events. The dedicated fence
        // is signaled by queue->signal AFTER it submits that immediate list.
        if (set.capture_fence) {
            if (!set.unsafe_tracking && set.capture_fence_value &&
                fence_completed(set.capture_fence->GetCompletedValue(),set.capture_fence_value)) {
                capture::finish(set); destroy_resource_set(set); g_retired_sets.fetch_add(1); continue;
            }
            ++active; ++pinned; bytes += set.allocated_bytes;
            if (set.unsafe_tracking) ++unsafe;
            continue;
        }
        const bool referenced = (recording_mask & (1ull << i)) != 0;
        // No observed queue means completion cannot be established. This also
        // deliberately holds discarded-only recordings rather than assuming an
        // unobserved native submission never happened.
        if (!referenced && set.queue_mask == 0) set.unsafe_tracking = true;
        if (!set.retiring && !referenced && !set.unsafe_tracking &&
            retirement_candidate(set.valid, nr_enabled(), set.native_feature ? 99 : g_scale_percent.load(),
                set.allocation_generation, g_scale_generation.load(), set.last_use, now))
        {
            set.retiring = true;
            for (std::size_t q = 0; q < g_tracked_queues.size(); ++q)
            {
                if ((set.queue_mask & (1u << q)) == 0) continue;
                auto &queue = g_tracked_queues[q];
                if (queue.native == nullptr || queue.fence == nullptr ||
                    FAILED(queue.native->Signal(queue.fence, ++queue.serial)))
                {
                    set.unsafe_tracking = true;
                    break;
                }
                set.retire_fences[q] = queue.serial;
            }
        }
        if (set.retiring && !set.unsafe_tracking && !referenced)
        {
            bool complete = true;
            for (std::size_t q = 0; q < g_tracked_queues.size(); ++q)
                if (set.retire_fences[q] != 0 &&
                    !fence_completed(g_tracked_queues[q].fence->GetCompletedValue(), set.retire_fences[q]))
                    complete = false;
            if (complete)
            {
                if (set.native_feature)
                {
                    if (release_native_feature(set))
                    {
                        set = {};
                        g_retired_sets.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                }
                else
                {
                    if (set.capture)
                    {
                        capture::finish(set);
                        destroy_resource_set(set);
                        g_retired_sets.fetch_add(1, std::memory_order_relaxed);
                    }
                    else if (nr_enabled() && g_scale_percent.load() < 100 &&
                        set.allocation_generation == g_scale_generation.load())
                    {
                        pool_resource_set(set, now);
                        ++active;
                        ++pooled;
                        bytes += set.allocated_bytes;
                    }
                    else
                    {
                        destroy_resource_set(set);
                        g_retired_sets.fetch_add(1, std::memory_order_relaxed);
                    }
                    continue;
                }
            }
        }
        ++active;
        bytes += set.allocated_bytes;
        if (referenced) ++pinned;
        if (set.retiring) ++retiring;
        if (set.unsafe_tracking) ++unsafe;
        if (set.native_feature) ++features;
    }
    g_cached_sets.store(active);
    g_cached_mib.store(static_cast<unsigned>((bytes + (1u << 20) - 1) >> 20));
    g_pinned_sets.store(pinned);
    g_retiring_sets.store(retiring);
    g_unsafe_sets.store(unsafe);
    g_native_features.store(features);
    g_pooled_sets.store(pooled);
}

void log_video_memory()
{
    reshade::api::device *device = g_pipeline_device;
    if (device == nullptr)
        for (const auto &queue : g_tracked_queues)
            if (queue.device != nullptr) { device = queue.device; break; }
    if (device == nullptr) return;
    using CreateFactory = HRESULT (WINAPI *)(UINT, REFIID, void **);
    const auto create = reinterpret_cast<CreateFactory>(GetProcAddress(
        GetModuleHandleW(L"dxgi.dll"), "CreateDXGIFactory2"));
    if (create == nullptr) return;
    IDXGIFactory4 *factory = nullptr;
    if (FAILED(create(0, __uuidof(IDXGIFactory4), reinterpret_cast<void **>(&factory)))) return;
    IDXGIAdapter3 *adapter = nullptr;
    const LUID luid = reinterpret_cast<ID3D12Device *>(device->get_native())->GetAdapterLuid();
    if (SUCCEEDED(factory->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter3), reinterpret_cast<void **>(&adapter))))
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
        if (SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
            log_message(reshade::log::level::info,
                "NR V6.6 process GPU memory: local usage=%llu MiB budget=%llu MiB (includes game and other mods).",
                static_cast<unsigned long long>(info.CurrentUsage >> 20),
                static_cast<unsigned long long>(info.Budget >> 20));
        adapter->Release();
    }
    factory->Release();
}

bool try_lock_native_nr()
{
    // Verified MSVC mutex layout in this exact host image: SRW at +0x10,
    // owner thread at +0x48, count at +0x4c. Try directly rather than calling
    // _Mtx_lock from a Present/overlay callback that may own a queue lock.
    auto *mutex = reinterpret_cast<std::uint8_t *>(g_target_module) + 0x26D650;
    auto *srw = reinterpret_cast<PSRWLOCK>(mutex + 0x10);
    if (!TryAcquireSRWLockExclusive(srw)) return false;
    if (field<unsigned>(mutex, 0x4C) != 0)
    {
        ReleaseSRWLockExclusive(srw);
        return false;
    }
    field<DWORD>(mutex, 0x48) = GetCurrentThreadId();
    field<unsigned>(mutex, 0x4C) = 1;
    return true;
}

void unlock_native_nr()
{
    auto *mutex = reinterpret_cast<std::uint8_t *>(g_target_module) + 0x26D650;
    field<unsigned>(mutex, 0x4C) = 0;
    field<DWORD>(mutex, 0x48) = MAXDWORD;
    ReleaseSRWLockExclusive(reinterpret_cast<PSRWLOCK>(mutex + 0x10));
}

#if defined(NR_EXPERIMENTAL_DX11) || defined(NR_LIFETIME_TEST)
// Only a quiesced backend may request this. It does not manufacture completion,
// erase recording pins, touch untracked host output caches, or destroy a device.
// Returning complete means OUR tracked sets drained, not that NGX can shut down.
bool retire_device_resources(reshade::api::device *device, unsigned &remaining)
{
    remaining = UINT_MAX;
    if (!device || !try_lock_native_nr()) return false;
    struct UnlockNative { ~UnlockNative() { unlock_native_nr(); } } native_lock;
    if (!TryEnterCriticalSection(&g_render_mutex)) return false;
    struct UnlockRender { ~UnlockRender() { LeaveCriticalSection(&g_render_mutex); } } render_lock;
    for (auto &set : g_resource_sets)
        if (set.active && set.device == device) set.valid = false;
    // Keep the production fence/recording/error policy. Other devices receive
    // ordinary maintenance only; their settings and validity are not changed.
    collect_resources_locked(GetTickCount64());
    remaining = 0;
    for (const auto &set : g_resource_sets)
        if (set.active && set.device == device) ++remaining;
    return remaining == 0;
}
#include "consumer_session.inl"

// Private backend only: caller has stopped submissions, destroyed transport
// recordings, detached NR and shut down its core. Never call this for a game
// device. Validate everything before detaching; release outside our lock so
// ReShade destruction callbacks cannot observe stale ownership or deadlock.
bool close_quiescent_private_caches(reshade::api::device *device)
{
    if (!device || !TryEnterCriticalSection(&g_render_mutex)) return false;
    for (const auto &set : g_resource_sets)
        if (set.active && set.device == device)
        { LeaveCriticalSection(&g_render_mutex); return false; }
    for (const auto &cmd : g_tracked_command_lists)
        if (cmd.active && cmd.device == device)
        { LeaveCriticalSection(&g_render_mutex); return false; }
    for (const auto &tracked : g_tracked_queues)
        if (tracked.device == device)
        {
            const auto completed = tracked.fence ? tracked.fence->GetCompletedValue() : UINT64_MAX;
            if (!tracked.native || completed == UINT64_MAX || completed < tracked.serial)
            { LeaveCriticalSection(&g_render_mutex); return false; }
        }
    std::array<TrackedQueue, kMaximumQueues> owned = {};
    for (unsigned i = 0; i < kMaximumQueues; ++i)
        if (g_tracked_queues[i].device == device)
        { owned[i] = g_tracked_queues[i]; g_tracked_queues[i] = {}; }
    reshade::api::pipeline pipeline = {};
    reshade::api::pipeline_layout layout = {};
    reshade::api::pipeline capture_pipeline = {};
    reshade::api::pipeline_layout capture_layout = {};
    if (capture::pipeline_device == device)
    {
        capture_pipeline = capture::copy_pipeline; capture_layout = capture::copy_layout;
        capture::copy_pipeline = {}; capture::copy_layout = {}; capture::pipeline_device = nullptr;
    }
    if (g_pipeline_device == device)
    {
        pipeline = g_pipeline; layout = g_pipeline_layout;
        g_pipeline = {}; g_pipeline_layout = {}; g_pipeline_device = nullptr;
    }
#ifdef NR_EXPERIMENTAL_DX11
    g_scale_history.forget(reinterpret_cast<std::uintptr_t>(device));
#endif
    g_evaluation_device.forget(reinterpret_cast<std::uintptr_t>(device));
    LeaveCriticalSection(&g_render_mutex);
    if (pipeline.handle) device->destroy_pipeline(pipeline);
    if (layout.handle) device->destroy_pipeline_layout(layout);
    if (capture_pipeline.handle) device->destroy_pipeline(capture_pipeline);
    if (capture_layout.handle) device->destroy_pipeline_layout(capture_layout);
    for (const auto &tracked : owned)
    {
        if (tracked.fence) tracked.fence->Release();
        if (tracked.native) tracked.native->Release();
    }
    return true;
}
#endif

void maintain_resources()
{
    const ULONGLONG now = GetTickCount64();
    static ULONGLONG next_collect = 0, next_report = 0;
    if (now < next_collect) return;
    next_collect = now + 250;
    // The verified host call sites lock this mutex before entering our evaluate
    // wrappers. Match their lock order; never take it while holding our mutex.
    if (!try_lock_native_nr()) return;
    struct UnlockNative
    {
        ~UnlockNative() { unlock_native_nr(); }
    } native_lock;
    if (!TryEnterCriticalSection(&g_render_mutex)) return;
    struct UnlockRender
    {
        ~UnlockRender() { LeaveCriticalSection(&g_render_mutex); }
    } render_lock;
    collect_resources_locked(now);
    if (now < next_report) return;
    next_report = now + 5000;
    log_message(reshade::log::level::info,
        "NR V6.6 cache: MiB=%u/512 sets=%u pinned=%u retiring=%u pooled=%u rebound=%u safety-held=%u retired=%u budget-fallbacks=%u.",
        g_cached_mib.load(), g_cached_sets.load(), g_pinned_sets.load(), g_retiring_sets.load(),
        g_pooled_sets.load(), g_rebound_sets.load(), g_unsafe_sets.load(),
        g_retired_sets.load(), g_budget_fallbacks.load());
    log_message(reshade::log::level::info,
        "NR V6.6 activity: enabled=%d scale=%d passes=%u eval=%u scaled=%u suppressed-while-off=%u gate=%u rejected=%u zero-frame=%u (cumulative; not GPU timings).",
        nr_enabled() ? 1 : 0, g_scale_percent.load(), *reinterpret_cast<volatile unsigned *>(
            reinterpret_cast<std::uintptr_t>(g_target_module) + 0x266FA4),
        g_evaluation_calls.load(), g_scaled_calls.load(), g_off_evaluation_calls.load(),
        g_native_gate_calls.load(), g_native_gate_rejected.load(), g_native_zero_frame.load());
    log_message(reshade::log::level::info,
        "NR V6.6 features: tracked=%u released=%u upstream-retained=%llu per-pass-slots=%llu; feature VRAM is not included in texture budget.",
        g_native_features.load(), g_native_features_retired.load(),
        static_cast<unsigned long long>(native_slots(0x26D8E8).count()),
        static_cast<unsigned long long>(native_slots(0x26D8D0).count()));
    log_message(reshade::log::level::info,
        "NR V6.6 native frame-ID observations: last=%llu repeated=%u backward=%u (observer IDs, not proven real-frame counts).",
        g_native_last_frame.load(), g_native_repeated_frame.load(), g_native_backward_frame.load());
    log_video_memory();
}

bool create_output_view(
    reshade::api::device *device,
    reshade::api::resource resource,
    reshade::api::format format,
    reshade::api::resource_view &view)
{
    const auto view_format = writable_format(format);
    return view_format != reshade::api::format::unknown &&
        device->check_format_support(view_format, reshade::api::resource_usage::unordered_access) &&
        device->create_resource_view(resource, reshade::api::resource_usage::unordered_access,
            reshade::api::resource_view_desc(view_format), &view);
}

bool create_source_view(
    reshade::api::device *device,
    reshade::api::resource resource,
    reshade::api::format format,
    reshade::api::resource_view &view)
{
    const auto view_format = shader_view_format(format);
    if (view_format == reshade::api::format::unknown ||
        !device->check_format_support(view_format, reshade::api::resource_usage::shader_resource))
        return false;
    return device->create_resource_view(
        resource, reshade::api::resource_usage::shader_resource,
        reshade::api::resource_view_desc(view_format), &view);
}

bool bind_source_resources(
    ResourceSet &set,
    reshade::api::resource color,
    reshade::api::resource output,
    reshade::api::resource motion,
    reshade::api::resource depth,
    reshade::api::resource ui,
    reshade::api::resource ui_alpha,
    const reshade::api::resource_desc &color_desc,
    const reshade::api::resource_desc &output_desc,
    const reshade::api::resource_desc &motion_desc,
    const reshade::api::resource_desc &depth_desc,
    const reshade::api::resource_desc &ui_desc,
    const reshade::api::resource_desc &ui_alpha_desc)
{
    reshade::api::resource_view color_srv = {}, output_uav = {};
    reshade::api::resource_view motion_srv = {}, depth_srv = {};
    reshade::api::resource_view ui_srv = {}, ui_alpha_srv = {};
    const bool ok =
        create_source_view(set.device, color, color_desc.texture.format, color_srv) &&
        create_output_view(set.device, output, output_desc.texture.format, output_uav) &&
        create_source_view(set.device, motion, motion_desc.texture.format, motion_srv) &&
        create_source_view(set.device, depth, depth_desc.texture.format, depth_srv) &&
        (!ui.handle || create_source_view(set.device, ui, ui_desc.texture.format, ui_srv)) &&
        (!ui_alpha.handle || create_source_view(set.device, ui_alpha,
            ui_alpha_desc.texture.format, ui_alpha_srv));
    if (!ok)
    {
        for (const auto view : {color_srv, output_uav, motion_srv, depth_srv, ui_srv, ui_alpha_srv})
            if (view.handle)
                set.device->destroy_resource_view(view);
        return false;
    }
    set.source_color = color;
    set.source_output = output;
    set.source_motion = motion;
    set.source_depth = depth;
    set.source_ui = ui;
    set.source_ui_alpha = ui_alpha;
    set.source_color_srv = color_srv;
    set.source_output_uav = output_uav;
    set.source_motion_srv = motion_srv;
    set.source_depth_srv = depth_srv;
    set.source_ui_srv = ui_srv;
    set.source_ui_alpha_srv = ui_alpha_srv;
    set.pooled = false;
    set.valid = true;
    return true;
}

bool valid_source_texture(
    const reshade::api::resource_desc &desc,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height)
{
    return desc.type == reshade::api::resource_type::texture_2d &&
        desc.texture.samples == 1 && width != 0 && height != 0 &&
        x <= desc.texture.width && y <= desc.texture.height &&
        width <= desc.texture.width - x && height <= desc.texture.height - y;
}

ResourceSet *find_or_create_resource_set(
    reshade::api::device *device,
    reshade::api::resource color,
    reshade::api::resource output,
    reshade::api::resource motion,
    reshade::api::resource depth,
    reshade::api::resource ui,
    reshade::api::resource ui_alpha,
    const reshade::api::resource_desc &color_desc,
    const reshade::api::resource_desc &output_desc,
    const reshade::api::resource_desc &motion_desc,
    const reshade::api::resource_desc &depth_desc,
    const reshade::api::resource_desc &ui_desc,
    const reshade::api::resource_desc &ui_alpha_desc,
    std::uint32_t display_width,
    std::uint32_t display_height,
    std::uint32_t work_width,
    std::uint32_t work_height)
{
    if (color_desc.type != reshade::api::resource_type::texture_2d ||
        output_desc.type != reshade::api::resource_type::texture_2d ||
        motion_desc.type != reshade::api::resource_type::texture_2d ||
        depth_desc.type != reshade::api::resource_type::texture_2d ||
        color_desc.texture.samples != 1 || output_desc.texture.samples != 1 ||
        motion_desc.texture.samples != 1 || depth_desc.texture.samples != 1 ||
        (ui.handle && (ui_desc.type != reshade::api::resource_type::texture_2d || ui_desc.texture.samples != 1)) ||
        (ui_alpha.handle && (ui_alpha_desc.type != reshade::api::resource_type::texture_2d || ui_alpha_desc.texture.samples != 1)))
        return nullptr;

    const auto color_format = writable_format(color_desc.texture.format);
    const auto output_format = writable_format(output_desc.texture.format);
    const auto motion_format = writable_format(motion_desc.texture.format);
    const auto depth_format = writable_format(depth_desc.texture.format);
    const auto ui_format = ui.handle ? writable_format(ui_desc.texture.format) : reshade::api::format::unknown;
    const auto ui_alpha_format = ui_alpha.handle ? writable_format(ui_alpha_desc.texture.format) : reshade::api::format::unknown;
    if (color_format == reshade::api::format::unknown ||
        output_format == reshade::api::format::unknown ||
        motion_format == reshade::api::format::unknown ||
        depth_format == reshade::api::format::unknown ||
        (ui.handle && ui_format == reshade::api::format::unknown) ||
        (ui_alpha.handle && ui_alpha_format == reshade::api::format::unknown))
        return nullptr;

    const unsigned generation = g_scale_generation.load();
    const auto compatible_working_set = [&](const ResourceSet &set)
    {
        return set.active && !set.capture && !set.native_feature && set.device == device &&
            set.allocation_generation == generation &&
            set.display_width == display_width && set.display_height == display_height &&
            set.work_width == work_width && set.work_height == work_height &&
            set.color_format == color_format && set.output_format == output_format &&
            set.motion_format == motion_format && set.depth_format == depth_format &&
            set.ui_format == ui_format && set.ui_alpha_format == ui_alpha_format;
    };

    for (auto &set : g_resource_sets)
    {
        if (compatible_working_set(set) && set.valid && !set.pooled && !set.retiring &&
            set.source_color == color && set.source_output == output &&
            set.source_motion == motion && set.source_depth == depth &&
            set.source_ui == ui && set.source_ui_alpha == ui_alpha)
        {
            set.last_use = GetTickCount64();
            return &set;
        }
    }

    const auto now = GetTickCount64();
    collect_resources_locked(now);
    // Rebind only a set whose previous command recordings and real queue fences
    // have fully drained. The private working textures and their state survive;
    // only the lightweight descriptors for the new game resources are replaced.
    for (auto &set : g_resource_sets)
        if (set.pooled && compatible_working_set(set) &&
            bind_source_resources(set, color, output, motion, depth, ui, ui_alpha,
                color_desc, output_desc, motion_desc, depth_desc, ui_desc, ui_alpha_desc))
        {
            set.last_use = now;
            g_rebound_sets.fetch_add(1, std::memory_order_relaxed);
            return &set;
        }

    // Bound one evaluation stream to four in-flight working sets. Dawnwalker
    // rotates source handles under FrameGen; a fifth identity uses the original
    // native path for that call instead of creating an allocation spiral.
    constexpr unsigned kMaximumWorkingSetsPerStream = 4;
    unsigned stream_sets = 0;
    for (const auto &set : g_resource_sets)
        if (compatible_working_set(set)) ++stream_sets;
    if (stream_sets >= kMaximumWorkingSetsPerStream)
    {
        g_budget_fallbacks.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    std::uint64_t requested_bytes = 0, used_bytes = 0;
    for (const auto &set : g_resource_sets)
        if (set.active) used_bytes += set.allocated_bytes;
    for (const auto format : {color_format, output_format, motion_format, depth_format,
            ui_format, ui_alpha_format})
    {
        if (format == reshade::api::format::unknown) continue;
        const auto size = texture_allocation_size(device, work_width, work_height, format);
        if (size == UINT64_MAX || size > kWorkingTextureBudget) return nullptr;
        requested_bytes += size;
    }
    const auto native_bytes = texture_allocation_size(device, display_width, display_height,
        writable_format(color_desc.texture.format));
    if (native_bytes == UINT64_MAX || native_bytes > kWorkingTextureBudget) return nullptr;
    requested_bytes += native_bytes;
    // Incompatible pooled shapes are already fence-safe. Evict them before
    // rejecting a current frame for budget pressure.
    if (!allocation_fits(used_bytes, requested_bytes, kWorkingTextureBudget))
        for (auto &candidate : g_resource_sets)
        {
            if (!candidate.pooled || compatible_working_set(candidate)) continue;
            const auto released = candidate.allocated_bytes;
            destroy_resource_set(candidate);
            g_retired_sets.fetch_add(1, std::memory_order_relaxed);
            used_bytes = released <= used_bytes ? used_bytes - released : 0;
            if (allocation_fits(used_bytes, requested_bytes, kWorkingTextureBudget)) break;
        }
    if (!allocation_fits(used_bytes, requested_bytes, kWorkingTextureBudget))
    {
        g_budget_fallbacks.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    auto iterator = std::find_if(g_resource_sets.begin(), g_resource_sets.end(),
        [](const ResourceSet &set) { return !set.active; });
    if (iterator == g_resource_sets.end())
        return nullptr;

    ResourceSet &set = *iterator;
    set.active = true;
    set.valid = true;
    set.device = device;
    set.display_width = display_width;
    set.display_height = display_height;
    set.work_width = work_width;
    set.work_height = work_height;
    set.color_format = color_format;
    set.output_format = output_format;
    set.motion_format = motion_format;
    set.depth_format = depth_format;
    set.ui_format = ui_format;
    set.ui_alpha_format = ui_alpha_format;
    set.allocated_bytes = requested_bytes;
    set.allocation_generation = generation;
    set.last_use = now;

    if (!create_texture_and_views(device, display_width, display_height, color_format,
            set.native_color, set.native_color_srv, set.native_color_uav) ||
        !create_texture_and_views(device, work_width, work_height, color_format,
            set.work_color, set.work_color_srv, set.work_color_uav) ||
        !create_texture_and_views(device, work_width, work_height, motion_format,
            set.work_motion, set.work_motion_srv, set.work_motion_uav) ||
        !create_texture_and_views(device, work_width, work_height, depth_format,
            set.work_depth, set.work_depth_srv, set.work_depth_uav) ||
        !create_texture_and_views(device, work_width, work_height, output_format,
            set.work_output, set.work_output_srv, set.work_output_uav))
        goto fail;

    if (ui.handle != 0)
    {
        if (!create_texture_and_views(device, work_width, work_height, ui_format,
                set.work_ui, set.work_ui_srv, set.work_ui_uav))
            goto fail;
    }
    if (ui_alpha.handle != 0)
    {
        if (!create_texture_and_views(device, work_width, work_height, ui_alpha_format,
                set.work_ui_alpha, set.work_ui_alpha_srv, set.work_ui_alpha_uav))
            goto fail;
    }
    if (!bind_source_resources(set, color, output, motion, depth, ui, ui_alpha,
            color_desc, output_desc, motion_desc, depth_desc, ui_desc, ui_alpha_desc))
        goto fail;

    return &set;

fail:
    destroy_resource_set(set);
    return nullptr;
}

void dispatch_resample(
    reshade::api::command_list *cmd_list,
    reshade::api::resource_view source,
    reshade::api::resource_view destination,
    std::uint32_t source_x,
    std::uint32_t source_y,
    std::uint32_t source_width,
    std::uint32_t source_height,
    std::uint32_t destination_width,
    std::uint32_t destination_height,
    std::uint32_t filter_mode = 0,
    float sharpness = 0.0f,
    reshade::api::resource_view small_input = {},
    reshade::api::resource_view native_color = {},
    std::uint32_t destination_x = 0, std::uint32_t destination_y = 0,
    float transfer = 1.0f, float color_strength = 1.0f)
{
    const reshade::api::descriptor_table_update source_update = {
        {}, 0, 0, 1, reshade::api::descriptor_type::texture_shader_resource_view, &source
    };
    const reshade::api::descriptor_table_update destination_update = {
        {}, 0, 0, 1, reshade::api::descriptor_type::texture_unordered_access_view, &destination
    };
    const std::uint32_t constants[] = {
        source_x, source_y, source_width, source_height,
        destination_width, destination_height,
        filter_mode, std::bit_cast<std::uint32_t>(sharpness), destination_x, destination_y,
        std::bit_cast<std::uint32_t>(transfer), std::bit_cast<std::uint32_t>(color_strength)
    };

    cmd_list->bind_pipeline(reshade::api::pipeline_stage::all_compute, g_pipeline);
    cmd_list->push_descriptors(reshade::api::shader_stage::all_compute, g_pipeline_layout, 0, source_update);
    cmd_list->push_descriptors(reshade::api::shader_stage::all_compute, g_pipeline_layout, 1, destination_update);
    cmd_list->push_constants(reshade::api::shader_stage::all_compute, g_pipeline_layout, 2, 0, 12, constants);
    // Bind valid descriptors even in branches which do not read these textures.
    if (!small_input.handle) small_input = source;
    if (!native_color.handle) native_color = source;
    auto extra_update = source_update;
    extra_update.descriptors = &small_input;
    cmd_list->push_descriptors(reshade::api::shader_stage::all_compute, g_pipeline_layout, 3, extra_update);
    extra_update.descriptors = &native_color;
    cmd_list->push_descriptors(reshade::api::shader_stage::all_compute, g_pipeline_layout, 4, extra_update);
    cmd_list->dispatch((destination_width + 7) / 8, (destination_height + 7) / 8, 1);
}

std::uint64_t __fastcall scaled_evaluate_body(void *input, unsigned call_site)
{
    auto original = &invoke_native_evaluation;
    if (input == nullptr) return 0;
    const auto capture_deadline = g_capture_off_until.load();
    if (capture_deadline && GetTickCount64() < capture_deadline) {
        g_capture_skipped.fetch_add(1);
        return 0; // Existing caller failure path leaves the original image intact.
    }
    if (!nr_enabled())
    {
        g_off_evaluation_calls.fetch_add(1, std::memory_order_relaxed);
        return 0; // The caller's existing failure path preserves the game image.
    }
    g_evaluation_calls.fetch_add(1, std::memory_order_relaxed);
    const int scale_percent = g_scale_percent.load(std::memory_order_relaxed);
    if (scale_percent >= 100 || !g_lifetime_events_registered)
    {
        g_effective_scale.store(100, std::memory_order_relaxed);
        return original(input);
    }

    const unsigned generation = g_scale_generation.load(std::memory_order_relaxed);
    if (transition_uses_native(generation, g_native_last_frame.load(std::memory_order_relaxed)))
        return original(input);
    auto &site_generation = call_site == 1 ?
        g_logged_create_site_generation : g_logged_existing_site_generation;
    const bool trace_this_call =
        site_generation.exchange(generation, std::memory_order_relaxed) != generation;
    if (trace_this_call)
        log_message(reshade::log::level::info,
            "RenoDX Neural Resolution: [1/9 hook] entered scaled hook site=%u (%s); input=%p.",
            call_site, call_site == 1 ? "create/evaluate" : "existing-handle evaluate", input);
    auto native_fallback = [&](const char *reason) {
        log_fallback_once(generation, reason);
        g_effective_scale.store(100, std::memory_order_relaxed);
        return original(input);
    };

    auto *cmd_list = reinterpret_cast<reshade::api::command_list *>(field<void *>(input, 0x00));
    const auto color = reshade::api::resource {
        reinterpret_cast<std::uint64_t>(field<void *>(input, 0x10)) };
    const auto output = reshade::api::resource {
        reinterpret_cast<std::uint64_t>(field<void *>(input, 0x18)) };
    const auto motion = reshade::api::resource {
        reinterpret_cast<std::uint64_t>(field<void *>(input, 0x20)) };
    const auto depth = reshade::api::resource {
        reinterpret_cast<std::uint64_t>(field<void *>(input, 0x28)) };
    const auto ui = reshade::api::resource {
        reinterpret_cast<std::uint64_t>(field<void *>(input, 0x30)) };
    const auto ui_alpha = reshade::api::resource {
        reinterpret_cast<std::uint64_t>(field<void *>(input, 0x38)) };
    const std::uint32_t display_width = field<std::uint32_t>(input, 0x44);
    const std::uint32_t display_height = field<std::uint32_t>(input, 0x48);
    if (trace_this_call)
        log_message(reshade::log::level::info,
            "RenoDX Neural Resolution: [2/9 raw] command_list=%p color=0x%llx output=0x%llx motion=0x%llx depth=0x%llx extent=%ux%u.",
            static_cast<void *>(cmd_list),
            static_cast<unsigned long long>(color.handle),
            static_cast<unsigned long long>(output.handle),
            static_cast<unsigned long long>(motion.handle),
            static_cast<unsigned long long>(depth.handle),
            display_width, display_height);
    if (color.handle == 0 || output.handle == 0 || motion.handle == 0 || depth.handle == 0 ||
        display_width == 0 || display_height == 0 ||
        display_width > 16384 || display_height > 16384)
        return native_fallback("the intercepted resources or display extent are invalid");

    const std::uint32_t color_x = field<std::uint32_t>(input, 0x60);
    const std::uint32_t color_y = field<std::uint32_t>(input, 0x64);
    const std::uint32_t color_width_value = field<std::uint32_t>(input, 0x68);
    const std::uint32_t color_height_value = field<std::uint32_t>(input, 0x6C);
    const std::uint32_t color_width = color_width_value != 0 ? color_width_value : display_width;
    const std::uint32_t color_height = color_height_value != 0 ? color_height_value : display_height;
    const auto output_x = field<std::uint32_t>(input, 0x70);
    const auto output_y = field<std::uint32_t>(input, 0x74);
    // A 1:1 native anchor cannot be promised for mismatched input/output extents.
    // Preserve the original NR route rather than silently resizing the base image.
    if (color_width != display_width || color_height != display_height ||
        field<unsigned>(input, 0x40) != static_cast<unsigned>(reshade::api::resource_usage::unordered_access))
        return native_fallback("native anchor extent or output state is unsupported");
    const std::uint32_t motion_x = field<std::uint32_t>(input, 0x80);
    const std::uint32_t motion_y = field<std::uint32_t>(input, 0x84);
    const std::uint32_t motion_width_value = field<std::uint32_t>(input, 0x88);
    const std::uint32_t motion_height_value = field<std::uint32_t>(input, 0x8C);
    const std::uint32_t motion_width = motion_width_value != 0 ? motion_width_value : display_width;
    const std::uint32_t motion_height = motion_height_value != 0 ? motion_height_value : display_height;
    const std::uint32_t depth_x = field<std::uint32_t>(input, 0x90);
    const std::uint32_t depth_y = field<std::uint32_t>(input, 0x94);
    const std::uint32_t depth_width_value = field<std::uint32_t>(input, 0x98);
    const std::uint32_t depth_height_value = field<std::uint32_t>(input, 0x9C);
    const std::uint32_t depth_width = depth_width_value != 0 ? depth_width_value : display_width;
    const std::uint32_t depth_height = depth_height_value != 0 ? depth_height_value : display_height;

    ScopedLock lock(g_render_mutex);
    TrackedCommandList *command_list_record = resolve_evaluation_command_locked(cmd_list);
    if (command_list_record == nullptr)
        return native_fallback("the intercepted command list did not match a ReShade wrapper or native D3D12 handle");
    const bool matched_native_command_list = cmd_list != command_list_record->command_list;
    reshade::api::device *device = command_list_record->device;
    cmd_list = command_list_record->command_list;
    command_list_record->references.recording();
    if (trace_this_call)
        log_message(reshade::log::level::info,
            "RenoDX Neural Resolution: [3/9 command] matched %s command list; querying input descriptions through its verified ReShade device.",
            matched_native_command_list ? "native" : "ReShade wrapper");
    const auto color_desc = device->get_resource_desc(color);
    const auto output_desc = device->get_resource_desc(output);
    const auto motion_desc = device->get_resource_desc(motion);
    const auto depth_desc = device->get_resource_desc(depth);
    const auto ui_desc = ui.handle != 0 ? device->get_resource_desc(ui) : reshade::api::resource_desc{};
    const auto ui_alpha_desc = ui_alpha.handle != 0 ? device->get_resource_desc(ui_alpha) : reshade::api::resource_desc{};
    if (!valid_source_texture(color_desc, color_x, color_y, color_width, color_height) ||
        !valid_source_texture(output_desc, output_x, output_y, display_width, display_height) ||
        !valid_source_texture(motion_desc, motion_x, motion_y, motion_width, motion_height) ||
        !valid_source_texture(depth_desc, depth_x, depth_y, depth_width, depth_height) ||
        (ui.handle != 0 && !valid_source_texture(ui_desc, 0, 0, display_width, display_height)) ||
        (ui_alpha.handle != 0 && !valid_source_texture(ui_alpha_desc, 0, 0, display_width, display_height)))
        return native_fallback("an input texture or subrect is unsupported");

    // Keep dimensions even: feature 18 and FP16 paths are most reliable on 2-pixel alignment.
    const std::uint32_t work_width = std::max(2u,
        ((display_width * static_cast<std::uint32_t>(scale_percent) + 50u) / 100u) & ~1u);
    const std::uint32_t work_height = std::max(2u,
        ((display_height * static_cast<std::uint32_t>(scale_percent) + 50u) / 100u) & ~1u);
    if (work_width >= display_width && work_height >= display_height)
        return original(input);

    if (trace_this_call)
    {
        log_message(reshade::log::level::info,
            "RenoDX Neural Resolution: [4/9 input] generation=%u, scale=%d%%, native=%ux%u, internal=%ux%u, formats=%u/%u/%u.",
            generation, scale_percent, display_width, display_height, work_width, work_height,
            static_cast<unsigned>(color_desc.texture.format),
            static_cast<unsigned>(motion_desc.texture.format),
            static_cast<unsigned>(depth_desc.texture.format));
    }

    if (!create_pipeline(device))
        return native_fallback("compute pipeline creation failed");
    auto *history = g_scale_history.find(reinterpret_cast<std::uintptr_t>(device), field<unsigned>(input, 8));
    if (!history) return native_fallback("scale-history registry is full");
    if (trace_this_call)
        log_text(reshade::log::level::info,
            "RenoDX Neural Resolution: [5/9 pipeline] compute pipeline is ready.");
    ResourceSet *set = find_or_create_resource_set(
        device, color, output, motion, depth, ui, ui_alpha,
        color_desc, output_desc, motion_desc, depth_desc, ui_desc, ui_alpha_desc,
        display_width, display_height, work_width, work_height);
    if (set == nullptr)
        return native_fallback("compatible working textures could not be created or the cache is full");
    command_list_record->references.sets |= 1ull << (set - g_resource_sets.data());
    g_scaled_calls.fetch_add(1, std::memory_order_relaxed);
    if (trace_this_call)
        log_text(reshade::log::level::info,
            "RenoDX Neural Resolution: [6/9 resources] working textures and persistent views are ready.");

    if (set->inputs_are_shader_resources)
    {
        const reshade::api::resource resources[] = {
            set->work_color, set->work_motion, set->work_depth };
        constexpr reshade::api::resource_usage old_states[] = {
            reshade::api::resource_usage::shader_resource_non_pixel,
            reshade::api::resource_usage::shader_resource_non_pixel,
            reshade::api::resource_usage::shader_resource_non_pixel };
        constexpr reshade::api::resource_usage new_states[] = {
            reshade::api::resource_usage::unordered_access,
            reshade::api::resource_usage::unordered_access,
            reshade::api::resource_usage::unordered_access };
        cmd_list->barrier(3, resources, old_states, new_states);
        if (ui.handle != 0)
            cmd_list->barrier(set->work_ui,
                reshade::api::resource_usage::shader_resource_non_pixel,
                reshade::api::resource_usage::unordered_access);
        if (ui_alpha.handle != 0)
            cmd_list->barrier(set->work_ui_alpha,
                reshade::api::resource_usage::shader_resource_non_pixel,
                reshade::api::resource_usage::unordered_access);
    }
    // Snapshot each pass's input before NR or resolve can modify it. This also
    // makes in-place input/output safe: the final UAV write never reads itself.
    if (color == output) cmd_list->barrier(color,
        reshade::api::resource_usage::unordered_access, reshade::api::resource_usage::shader_resource_non_pixel);
    dispatch_resample(cmd_list, set->source_color_srv, set->native_color_uav,
        color_x, color_y, display_width, display_height, display_width, display_height, 2);
    if (color == output) cmd_list->barrier(color,
        reshade::api::resource_usage::shader_resource_non_pixel, reshade::api::resource_usage::unordered_access);
    cmd_list->barrier(set->native_color, reshade::api::resource_usage::unordered_access,
        reshade::api::resource_usage::shader_resource_non_pixel);
    dispatch_resample(cmd_list, set->native_color_srv, set->work_color_uav,
        0, 0, display_width, display_height, work_width, work_height);
    dispatch_resample(cmd_list, set->source_motion_srv, set->work_motion_uav,
        motion_x, motion_y, motion_width, motion_height, work_width, work_height, 3);
    dispatch_resample(cmd_list, set->source_depth_srv, set->work_depth_uav,
        depth_x, depth_y, depth_width, depth_height, work_width, work_height, 3);
    if (ui.handle != 0)
        dispatch_resample(cmd_list, set->source_ui_srv, set->work_ui_uav,
            0, 0, display_width, display_height, work_width, work_height);
    if (ui_alpha.handle != 0)
        dispatch_resample(cmd_list, set->source_ui_alpha_srv, set->work_ui_alpha_uav,
            0, 0, display_width, display_height, work_width, work_height);
    if (trace_this_call)
        log_text(reshade::log::level::info,
            "RenoDX Neural Resolution: [7/9 downsample] input resampling commands recorded.");

    {
        const reshade::api::resource resources[] = {
            set->work_color, set->work_motion, set->work_depth };
        constexpr reshade::api::resource_usage old_states[] = {
            reshade::api::resource_usage::unordered_access,
            reshade::api::resource_usage::unordered_access,
            reshade::api::resource_usage::unordered_access };
        constexpr reshade::api::resource_usage new_states[] = {
            reshade::api::resource_usage::shader_resource_non_pixel,
            reshade::api::resource_usage::shader_resource_non_pixel,
            reshade::api::resource_usage::shader_resource_non_pixel };
        cmd_list->barrier(3, resources, old_states, new_states);
        if (ui.handle != 0)
            cmd_list->barrier(set->work_ui,
                reshade::api::resource_usage::unordered_access,
                reshade::api::resource_usage::shader_resource_non_pixel);
        if (ui_alpha.handle != 0)
            cmd_list->barrier(set->work_ui_alpha,
                reshade::api::resource_usage::unordered_access,
                reshade::api::resource_usage::shader_resource_non_pixel);
    }
    set->inputs_are_shader_resources = true;

    alignas(16) std::array<std::uint8_t, 0xB0> scaled_input = {};
    std::memcpy(scaled_input.data(), input, kInputSize);
    field<void *>(scaled_input.data(), 0x10) = reinterpret_cast<void *>(set->work_color.handle);
    field<void *>(scaled_input.data(), 0x18) = reinterpret_cast<void *>(set->work_output.handle);
    field<void *>(scaled_input.data(), 0x20) = reinterpret_cast<void *>(set->work_motion.handle);
    field<void *>(scaled_input.data(), 0x28) = reinterpret_cast<void *>(set->work_depth.handle);
    if (ui.handle != 0)
        field<void *>(scaled_input.data(), 0x30) = reinterpret_cast<void *>(set->work_ui.handle);
    if (ui_alpha.handle != 0)
        field<void *>(scaled_input.data(), 0x38) = reinterpret_cast<void *>(set->work_ui_alpha.handle);
    field<std::uint32_t>(scaled_input.data(), 0x44) = work_width;
    field<std::uint32_t>(scaled_input.data(), 0x48) = work_height;

    const float ratio_x = static_cast<float>(work_width) / static_cast<float>(display_width);
    const float ratio_y = static_cast<float>(work_height) / static_cast<float>(display_height);
    field<float>(scaled_input.data(), 0x4C) *= ratio_x;
    field<float>(scaled_input.data(), 0x50) *= ratio_y;
    field<float>(scaled_input.data(), 0x54) *= ratio_x;
    field<float>(scaled_input.data(), 0x58) *= ratio_y;

    // Replace all explicit color/motion/depth subrects with the full reduced extent.
    for (const std::size_t rect : { 0x60u, 0x70u, 0x80u, 0x90u })
    {
        field<std::uint64_t>(scaled_input.data(), rect) = 0;
        field<std::uint32_t>(scaled_input.data(), rect + 8) = work_width;
        field<std::uint32_t>(scaled_input.data(), rect + 12) = work_height;
    }

    // Experimental backend: rotating working textures share one NR history per
    // device/pass. Creating another texture slot must not reset that history.
    if (history->generation != generation)
    {
        static unsigned reset_messages = 0;
        if (reset_messages++ < 32)
        {
            char diagnostic[256];
            diagnostic_format(diagnostic, "NR stream history reset: frame=%llu pass=%u generation=%u resource-slot=%u incoming-reset=%u.",
                static_cast<unsigned long long>(g_native_last_frame.load()), field<unsigned>(input, 8), generation,
                static_cast<unsigned>(set - g_resource_sets.data()), field<unsigned char>(input, 0x5D));
            log_text(reshade::log::level::info, diagnostic);
        }
        field<std::uint8_t>(scaled_input.data(), 0x5D) = 1;
        set->reset_generation = generation;
    }

    const std::uint64_t result = original(scaled_input.data());
    if ((result & 0xFFu) == 0)
    {
        cmd_list->barrier(set->native_color, reshade::api::resource_usage::shader_resource_non_pixel,
            reshade::api::resource_usage::unordered_access);
        return native_fallback("the reduced Neural Rendering evaluation failed");
    }
    history->generation = generation; // Failed records never consume the reset.
    g_effective_scale.store(scale_percent, std::memory_order_relaxed);
    if (trace_this_call)
        log_text(reshade::log::level::info,
            "RenoDX Neural Resolution: [8/9 evaluation] reduced Neural Rendering evaluation returned successfully.");

    cmd_list->barrier(set->work_output,
        reshade::api::resource_usage::unordered_access,
        reshade::api::resource_usage::shader_resource_non_pixel);
    dispatch_resample(cmd_list, set->work_output_srv, set->source_output_uav,
        0, 0, work_width, work_height, display_width, display_height,
        g_resolve_mode.load() == 1 ? 1u : 4u,
        static_cast<float>(g_sharpness_percent.load()) / 100.0f,
        set->work_color_srv, set->native_color_srv, output_x, output_y,
        static_cast<float>(g_transfer_percent.load()) / 100.0f,
        static_cast<float>(g_color_percent.load()) / 100.0f);
    cmd_list->barrier(output, reshade::api::resource_usage::unordered_access,
        reshade::api::resource_usage::unordered_access);
    cmd_list->barrier(set->native_color, reshade::api::resource_usage::shader_resource_non_pixel,
        reshade::api::resource_usage::unordered_access);
    cmd_list->barrier(set->work_output,
        reshade::api::resource_usage::shader_resource_non_pixel,
        reshade::api::resource_usage::unordered_access);
    if (trace_this_call)
        log_message(reshade::log::level::info,
            "NR COST SCALER 1: resolve=%s sharpness=%d%% transfer=%d%% color=%d%%; native anchor preserved; output origin=%u/%u.",
            g_resolve_mode.load() == 1 ? "matched-residual" : "direct", g_sharpness_percent.load(),
            g_transfer_percent.load(), g_color_percent.load(), output_x, output_y);

    if (g_logged_success_generation.exchange(generation, std::memory_order_relaxed) != generation)
    {
        log_message(reshade::log::level::info,
            "RenoDX Neural Resolution: generation %u completed its first scaled frame successfully.",
            generation);
    }
    return result;
}

std::uint64_t __fastcall scaled_evaluate_impl(void *input, unsigned call_site)
{
    const auto ticket = capture::before_bypass(input);
    const auto result = scaled_evaluate_body(input,call_site);
    if (ticket) capture::after(input,result,ticket);
    return result;
}

// Caller holds g_render_mutex. Never evict a recording that still references
// our resources, including PRE-Reset recordings that may still execute.
TrackedCommandList *claim_command_list_locked(reshade::api::command_list *command_list,
    reshade::api::device *device, std::uint64_t native_command_list, std::uint64_t native_device)
{
    for (auto &tracked : g_tracked_command_lists)
        if (tracked.active && tracked.command_list == command_list)
            return &tracked;
    for (unsigned sweep = 0; sweep < 2; ++sweep)
        for (std::size_t n = 0; n < g_tracked_command_lists.size(); ++n)
        {
            const auto index = (g_command_slot_cursor + n) % g_tracked_command_lists.size();
            auto &tracked = g_tracked_command_lists[index];
            if (tracked.active && (sweep == 0 || tracked.references.sets != 0)) continue;
            if (tracked.active) g_command_slots_recycled.fetch_add(1, std::memory_order_relaxed);
            tracked = {};
            tracked.active = true;
            tracked.command_list = command_list;
            tracked.device = device;
            tracked.native_command_list = native_command_list;
            tracked.native_device = native_device;
            g_command_slot_cursor = (index + 1) % g_tracked_command_lists.size();
            return &tracked;
        }
    g_command_slots_exhausted.fetch_add(1, std::memory_order_relaxed);
    return nullptr;
}

void on_init_command_list(reshade::api::command_list *command_list)
{
    if (command_list == nullptr)
        return;
    reshade::api::device *device = command_list->get_device();
    if (device == nullptr || device->get_api() != reshade::api::device_api::d3d12)
        return;
    const std::uint64_t native_command_list = command_list->get_native();
    const std::uint64_t native_device = device->get_native();
    if (native_command_list == 0 || native_device == 0)
        return;

    ScopedLock lock(g_render_mutex);
    if (claim_command_list_locked(command_list, device, native_command_list, native_device)) return;
    if (!g_command_registry_warning_logged.exchange(true, std::memory_order_relaxed))
        log_text(reshade::log::level::warning,
            "RenoDX Neural Resolution: command-list registry is fully pinned; preserving every outstanding reference; untracked lists use the original path.");
}

void on_destroy_command_list(reshade::api::command_list *command_list)
{
    ScopedLock lock(g_render_mutex);
    capture::reset_command(command_list);
    for (auto &tracked : g_tracked_command_lists)
    {
        if (tracked.active && tracked.command_list == command_list)
        {
            tracked = {};
            return;
        }
    }
}

void on_destroy_device(reshade::api::device *device)
{
    ScopedLock lock(g_render_mutex);
    g_scale_history.forget(reinterpret_cast<std::uintptr_t>(device));
    g_evaluation_device.forget(reinterpret_cast<std::uintptr_t>(device));
    for (auto &set : g_resource_sets)
        if (set.active && set.device == device)
            destroy_resource_set(set);
    capture::destroy_pipeline(device);
    if (g_pipeline_device == device)
    {
        if (g_pipeline.handle != 0)
            device->destroy_pipeline(g_pipeline);
        if (g_pipeline_layout.handle != 0)
            device->destroy_pipeline_layout(g_pipeline_layout);
        g_pipeline = {};
        g_pipeline_layout = {};
        g_pipeline_device = nullptr;
    }
    clear_device_tracking_locked(device);
    for (auto &queue : g_tracked_queues)
        if (queue.device == device)
        {
            if (queue.fence) queue.fence->Release();
            if (queue.native) queue.native->Release();
            queue = {};
        }
}

void on_destroy_resource(reshade::api::device *device, reshade::api::resource resource)
{
    ScopedLock lock(g_render_mutex);
    capture::forget_resource(device, resource);
    for (auto &set : g_resource_sets)
    {
        if (!set.active || set.device != device)
            continue;

        // Invalidate here; the collector handles recording references and GPU
        // fences. Never destroy owned objects inside a source destruction event.
        if (set.source_color == resource || set.source_output == resource ||
            set.source_motion == resource || set.source_depth == resource ||
            set.source_ui == resource || set.source_ui_alpha == resource)
            set.valid = false;
    }
}

} // namespace dx12
