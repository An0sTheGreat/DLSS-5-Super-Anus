#include "../src/capture_chain.hpp"
#include <cassert>
#include <cstdio>
int main() {
    nr::CaptureChain c;
    for (unsigned passes = 1; passes <= 10; ++passes) {
        c.start(123, 42, passes);
        for (unsigned pass = 0; pass < passes; ++pass) {
            const auto t = c.enter(123,42,pass); assert(t);
            assert(!c.leave(t+1,true));
            assert(c.leave(t,true) == (pass+1 == passes));
        }
        assert(c.complete && !c.active);
        assert(!c.enter(123,42,0));
    }
    c.start(123,42,2); auto old = c.enter(123,42,0);
    assert(!c.leave(old,false)); assert(!c.enter(123,42,1)); assert(!c.complete);
    c.start(123,42,2); auto t = c.enter(123,42,0);
    assert(!c.leave(old,true)); assert(!c.leave(t,true));
    assert(!c.enter(123,42,0)); assert(!c.complete); // another frame
    c.start(123,42,2); assert(!c.enter(124,42,0));
    c.start(123,42,2); assert(!c.enter(123,43,0));
    c.start(123,42,2); t = c.enter(123,42,0); c.abort(); assert(!c.leave(t,true));
    c.start(123,42,2); assert(c.enter(123,42,0)); assert(!c.enter(123,42,0));
    assert(!c.complete); // reentrant call cannot complete a pair
    c.start(123,42,11); assert(!c.active);
    std::puts("Capture chain: 1-10 passes, failure, reset, cross-frame/thread/command and stale tickets passed.");
}
