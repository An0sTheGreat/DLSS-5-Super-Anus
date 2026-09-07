#include "../src/nr_activity_monitor.hpp"
#include <cassert>
#include <cstdio>
int main()
{
    using nr::ActivityChange;
    nr::ActivityMonitor monitor;
    assert(monitor.observe(1000, true, 7) == ActivityChange::none);
    assert(monitor.observe(3999, true, 7) == ActivityChange::none);
    assert(monitor.observe(4000, true, 7) == ActivityChange::stalled);
    assert(monitor.observe(9000, true, 7) == ActivityChange::none);
    assert(monitor.observe(9001, true, 8) == ActivityChange::resumed);
    assert(monitor.observe(9002, false, 8) == ActivityChange::none);
    assert(monitor.observe(20000, false, 8) == ActivityChange::none);
    assert(monitor.observe(20001, true, 8) == ActivityChange::none);
    assert(monitor.observe(23000, true, 8) == ActivityChange::stalled);
    assert(monitor.observe(23001, true, 0xffffffff) == ActivityChange::resumed);
    assert(monitor.observe(23002, true, 0) == ActivityChange::none); // counter wrap
    assert(monitor.observe(10, true, 0) == ActivityChange::none); // clock rollback
    std::puts("NR activity monitor: stall/resume, no log spam, disabled interval, counter/clock wrap passed.");
}
