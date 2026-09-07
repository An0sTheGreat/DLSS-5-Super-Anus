// Link-only check for the consolidated addon's no-CRT-initializer component.
// Not installed or grafted: these exports are test entry points, not game hooks.
#define NOMINMAX
#include "../src/backends/dx11_transport.hpp"
constinit nr::backends::dx11::Transport transport_embedding_probe;
extern "C" __declspec(dllexport) HRESULT transport_probe_initialize(
    ID3D11DeviceContext *context, ID3D12Device *device, ID3D12CommandQueue *queue)
{ return transport_embedding_probe.initialize(context, device, queue); }
extern "C" __declspec(dllexport) nr::backends::dx11::Result transport_probe_submit(
    const nr::backends::dx11::Frame *frame, nr::backends::dx11::Record record, void *user)
{ return frame ? transport_embedding_probe.submit(*frame, record, user) : nr::backends::dx11::Result::invalid_input; }
extern "C" __declspec(dllexport) bool transport_probe_close()
{ return transport_embedding_probe.try_close(); }
