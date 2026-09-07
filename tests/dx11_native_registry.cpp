// Real WARP device identities; native NGX entry points are mocks. No NR or GPU work.
#define NR_LIFETIME_TEST
#define NR_EXPERIMENTAL_DX11
#include "../src/neural_resolution_addon.cpp"
#include <cassert>
#include <cstdio>

// Opaque test tokens only: no code dereferences a handle or calls the driver.
struct NVSDK_NGX_Handle { unsigned test_token; };

using namespace dx11_native;
static Shape test_shape{32, 24, 64, 48, 0, 0};
static ID3D11DeviceContext *replacement_context = nullptr;
static bool replace_during_release = false;
static Result native_result = NVSDK_NGX_Result_Success;
static unsigned release_calls = 0, shutdown_calls = 0;
static unsigned driver_output = 0;

static bool capture(ID3D11DeviceContext *context, const Handle *handle)
{
    AcquireSRWLockExclusive(&mutex);
    const bool result = capture_feature(context, handle, test_shape, true, capture_epoch);
    ReleaseSRWLockExclusive(&mutex);
    return result;
}

static Result mock_release(Handle *handle)
{
    ++release_calls;
    // Native callbacks run without our registry lock. Model handle-address reuse
    // before an older ReleaseFeature wrapper has returned to its caller.
    if (replace_during_release) assert(capture(replacement_context, handle));
    return native_result;
}

static Result mock_shutdown(ID3D11Device *closing, unsigned *output)
{
    assert(output == &driver_output); // exact caller pointer survives the hook
    *output = 0x12345678;
    ++shutdown_calls;
    assert(TryAcquireSRWLockExclusive(&mutex));
    assert(shutdowns_in_flight == 1);
    for (const auto &item : features)
        assert(!item.owner || !nr::backends::dx11::same_object(item.owner, closing));
    Handle temporary{};
    assert(!capture_feature(replacement_context, &temporary, test_shape, true, capture_epoch));
    ReleaseSRWLockExclusive(&mutex);
    return native_result;
}
static unsigned sdk_shutdown_calls = 0;
static Result mock_sdk_shutdown(ID3D11Device *closing)
{
    assert(closing && shutdowns_in_flight == 1); ++sdk_shutdown_calls;
    return NVSDK_NGX_Result_Success;
}

int main()
{
    evaluation_tls = TlsAlloc(); assert(evaluation_tls != TLS_OUT_OF_INDEXES);
    {
        EvaluationScope outer; assert(outer.outer);
        { EvaluationScope nested; assert(!nested.outer); }
        assert(TlsGetValue(evaluation_tls) != nullptr);
    }
    assert(TlsGetValue(evaluation_tls) == nullptr);
    TlsFree(evaluation_tls); evaluation_tls = TLS_OUT_OF_INDEXES;
    ID3D11Device *a = nullptr, *b = nullptr;
    ID3D11DeviceContext *ca = nullptr, *cb = nullptr;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &a, nullptr, &ca)));
    assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &b, nullptr, &cb)));
    assert(!nr::backends::dx11::same_object(a, b));
    replacement_context = cb;
    original_release = &mock_release;
    original_shutdown = &mock_shutdown;
    Handle handles[40] = {};
    Shape actual;
    for (unsigned i = 0; i < 32; ++i) assert(capture(ca, &handles[i]));
    assert(!capture(cb, &handles[32])); // full; do not evict a live device's entry
    assert(evaluation_ticket(&handles[31]) == feature_ticket(&handles[31]));

    AcquireSRWLockExclusive(&mutex);
    assert(!evaluation_ticket(&handles[31])); // busy snapshots skip private work
    assert(find_feature(ca, &handles[31], actual) && actual == test_shape);
    assert(!find_feature(cb, &handles[31], actual));
    const auto original_ticket = feature_ticket(&handles[31]);
    assert(forget_released_feature(&handles[0], feature_ticket(&handles[0])));
    // Earlier hole must not cause a duplicate record of an existing handle.
    assert(capture_feature(cb, &handles[31], test_shape, true, capture_epoch));
    assert(!forget_released_feature(&handles[31], original_ticket));
    unsigned duplicates = 0;
    for (const auto &item : features) if (item.handle == &handles[31]) ++duplicates;
    assert(duplicates == 1);
    assert(!find_feature(ca, &handles[31], actual));
    assert(find_feature(cb, &handles[31], actual));
    const auto pending_epoch = capture_epoch;
    invalidate_device_features(a);
    assert(!capture_feature(ca, &handles[33], test_shape, true, pending_epoch));
    assert(find_feature(cb, &handles[31], actual)); // foreign shutdown leaves B tracked
    ReleaseSRWLockExclusive(&mutex);

    native_result = NVSDK_NGX_Result_Fail;
    assert(dx11_release_dispatch(&handles[31]) == native_result);
    assert(feature_ticket(&handles[31])); // failed native release cannot erase ownership
    native_result = NVSDK_NGX_Result_Success;
    assert(capture(ca, &handles[31]));
    active_native_feature = &handles[31];
    active_native_generation = feature_ticket(&handles[31]);
    const auto old_active_generation = active_native_generation;
    replace_during_release = true;
    assert(dx11_release_dispatch(&handles[31]) == NVSDK_NGX_Result_Success);
    assert(feature_ticket(&handles[31]) != old_active_generation);
    assert(evaluation_ticket(&handles[31]) != old_active_generation);
    assert(active_native_generation == old_active_generation); // new capture isn't erased
    replace_during_release = false;
    active_native_generation = feature_ticket(&handles[31]);
    reset_history = false;
    assert(dx11_release_dispatch(&handles[31]) == NVSDK_NGX_Result_Success);
    assert(!feature_ticket(&handles[31]) && !active_native_feature && reset_history);

    for (unsigned cycle = 0; cycle < 32; ++cycle)
    {
        assert(capture(ca, &handles[0]));
        assert(capture(cb, &handles[1]));
        const auto pending = capture_epoch;
        native_result = cycle % 2 ? NVSDK_NGX_Result_Success : NVSDK_NGX_Result_Fail;
        driver_output = 0;
        assert(dx11_shutdown_dispatch(a, &driver_output) == native_result);
        assert(driver_output == 0x12345678);
        assert(!shutdowns_in_flight && !feature_ticket(&handles[0]));
        assert(feature_ticket(&handles[1]));
        AcquireSRWLockExclusive(&mutex);
        assert(!capture_feature(ca, &handles[2], test_shape, true, pending));
        ReleaseSRWLockExclusive(&mutex);
        assert(dx11_shutdown_dispatch(b, &driver_output) == native_result);
        assert(!shutdowns_in_flight && !feature_ticket(&handles[1]));
    }
    AcquireSRWLockExclusive(&mutex);
    // Unsupported recapture invalidates an old matching handle, with no owner leak.
    assert(capture_feature(ca, &handles[0], test_shape, true, capture_epoch));
    assert(!capture_feature(cb, &handles[0], test_shape, false, capture_epoch));
    assert(!feature_ticket(&handles[0]));
    feature_generation = UINT64_MAX;
    assert(!capture_feature(ca, &handles[0], test_shape, true, capture_epoch));
    feature_generation = 0;
    capture_epoch = UINT64_MAX;
    invalidate_device_features(a);
    assert(capture_epoch == UINT64_MAX);
    assert(!capture_feature(ca, &handles[0], test_shape, true, capture_epoch));
    for (const auto &item : features) assert(!item.handle && !item.owner);
    ReleaseSRWLockExclusive(&mutex);
    assert(release_calls == 3 && shutdown_calls == 64);
    sdk_frontend = true; original_sdk_shutdown = mock_sdk_shutdown;
    assert(dx11_sdk_shutdown_dispatch(a) == NVSDK_NGX_Result_Success);
    assert(sdk_shutdown_calls == 1 && shutdown_calls == 64 && !shutdowns_in_flight);
    sdk_frontend = false;
    release(cb); release(ca); release(b); release(a);
    std::puts("DX11 native registry: device identity, 32-slot bound, handle reuse/generations, stale creates, failed releases, 64 shutdown forwards and epoch saturation passed. Native calls mocked; no NR.");
}
