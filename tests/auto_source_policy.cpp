#include "../src/auto_source_policy.hpp"
#include <cassert>
#include <thread>
#include <cstdio>
int main() {
    nr::AutoSourcePolicy p;
    assert(!p.select_native(1,100));
    assert(p.enter_other(2));
    assert(!p.select_native(3,1000)); // in-flight FG owns it
    p.leave_other(1000,true);
    assert(!p.select_native(3,1749));
    assert(!p.select_native(2,1750)); // same application frame
    assert(!p.select_native(1,1750)); // backwards frame
    assert(p.select_native(3,1750));
    assert(!p.enter_other(4)); // late FG cannot double-process
    assert(p.select_native(4,1751)); // stable, no route oscillation
    nr::AutoSourcePolicy nested;
    assert(!nested.select_native(1,100));
    assert(nested.enter_other(2)); assert(nested.enter_other(2));
    nested.leave_other(1000,false);
    assert(!nested.select_native(3,1000));
    nested.leave_other(1000,false);
    assert(nested.select_native(3,1000));
    for (unsigned i=0;i<2000;++i) {
        nr::AutoSourcePolicy race;
        assert(!race.select_native(1,100));
        std::atomic_bool go=false;
        bool entered=false, selected=false;
        std::thread fg([&]{ while(!go.load()) {} entered=race.enter_other(9); if(entered) race.leave_other(900,true); });
        std::thread sr([&]{ while(!go.load()) {} selected=race.select_native(9,900); });
        go=true; fg.join(); sr.join();
        assert(!(entered && selected));
    }
    puts("Auto source: startup, timeout, successful-work grace, nested/in-flight protection, same/backwards frames, sticky fallback and 2000 FG/native races passed.");
}
