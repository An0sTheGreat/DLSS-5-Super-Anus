#define NOMINMAX
#include <Windows.h>
#include <cassert>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>
#include "diagnostic_format.hpp"
#include "frame_trace.hpp"
#include "backends/backend_support.hpp"

static nr::FrameTrace trace;

int main()
{
    char text[512];
    diagnostic_format(text, "last=%llu repeated=%u backward=%u", 0x100000001ull, 17u, 3u);
    assert(std::strcmp(text, "last=4294967297 repeated=17 backward=3") == 0);
    diagnostic_format(text, "usage=%llu budget=%llu", 12345ull, 23456ull);
    assert(std::strcmp(text, "usage=12345 budget=23456") == 0);
    diagnostic_format(text, "%llx %u %d %lu %s %%", 0xfedcba9876543210ull, 42u, -13, 55ul, "ok");
    assert(std::strcmp(text, "fedcba9876543210 42 -13 55 ok %") == 0);
    diagnostic_format(text, "%llu", std::numeric_limits<unsigned long long>::max());
    assert(std::strcmp(text, "18446744073709551615") == 0);
    struct { char text[16]; char canary[8]; } bounded = {};
    std::memset(bounded.canary, 'X', sizeof(bounded.canary));
    diagnostic_format(bounded.text, "%s", "longer than sixteen characters");
    assert(bounded.text[15] == 0);
    assert(std::strstr(bounded.text, "[truncated]") != nullptr);
    for (char c : bounded.canary) assert(c == 'X');
    diagnostic_format(bounded.text, "%d", -2147483647 - 1);
    assert(std::strcmp(bounded.text, "-2147483648") == 0);

    using reshade::api::device_api;
    assert(nr::backends::support(device_api::d3d12).evaluator_available);
    for (auto api : {device_api::d3d11, device_api::vulkan, device_api::d3d9,
                    device_api::opengl, device_api::d3d10, static_cast<device_api>(0)})
        assert(!nr::backends::support(api).evaluator_available);
    assert(!nr::backends::handles(nullptr));

    assert(trace.start(100));
    assert(!trace.start(101));
    nr::FrameTraceEvent event;
    event.tick = 101;
    event.frame = 0xfedcba9876543210ull;
    event.kind = nr::TraceKind::framegen_exit;
    event.callback = 0x100000001ull;
    event.gap = 17;
    event.hook = 3;
    event.pass = 2;
    event.evaluations = event.successes = 1;
    event.original_called = true;
    for (unsigned i = 0; i < nr::FrameTrace::capacity + 17; ++i)
    {
        event.thread = i;
        trace.push(event);
    }
    assert(trace.dropped() == 17);
    assert(!trace.pop(102, &event));
    for (unsigned i = 0; i < nr::FrameTrace::capacity; ++i)
    {
        assert(trace.pop(10100, &event));
        assert(event.thread == i && event.frame == 0xfedcba9876543210ull);
        assert(event.kind == nr::TraceKind::framegen_exit && event.callback == 0x100000001ull);
        assert(event.gap == 17 && event.hook == 3 && event.pass == 2);
        assert(event.evaluations == 1 && event.successes == 1 && event.original_called);
        if (i + 1 < nr::FrameTrace::capacity) assert(!trace.start(10101));
    }
    assert(trace.start(10101)); // A new capture needs complete drainage.
    assert(trace.recording(10102));
    assert(!trace.pop(20101, &event));
    assert(trace.start(30000));
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < 4; ++worker)
        workers.emplace_back([worker] {
            for (unsigned i = 0; i < 1000; ++i)
            {
                nr::FrameTraceEvent record;
                record.tick = 30001;
                record.thread = worker;
                record.frame = i;
                trace.push(record);
            }
        });
    for (auto &worker : workers) worker.join();
    unsigned count = 0;
    while (trace.pop(40000, &event))
    {
        assert(event.thread < 4 && event.frame < 1000);
        ++count;
    }
    assert(count + trace.dropped() == 4000);
    std::puts("V6.5: bounded 64-bit formatting, API rejection, trace bounds/drain/concurrency passed.");
}
