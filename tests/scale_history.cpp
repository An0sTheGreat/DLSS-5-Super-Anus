#include "../src/scale_history.hpp"
#include <cassert>
#include <cstdio>

static constinit nr::ScaleHistory embedded_initialization;
int main()
{
    auto &history = embedded_initialization;
    assert(!history.find(0, 0));
    unsigned resets = 0;
    for (unsigned generation = 1; generation <= 200; ++generation)
    {
        for (unsigned frame = 0; frame < 100; ++frame)
        {
            // Two passes alternate arbitrarily many texture slots. Slot identity
            // must never become a history key; exactly one reset per pass/scale.
            for (unsigned pass = 0; pass < 2; ++pass)
            {
                auto *entry = history.find(1, pass);
                assert(entry);
                const bool reset = entry->generation != generation;
                if (frame == 0) {
                    // A failed first record does not consume the reset request.
                    assert(reset);
                    assert(history.find(1, pass)->generation != generation);
                }
                resets += reset;
                entry->generation = generation;
            }
        }
    }
    assert(resets == 400);
    assert(history.find(2, 0)->generation == 0);
    history.forget(1);
    assert(history.find(1, 0)->generation == 0);
    assert(history.find(2, 0));
    nr::ScaleHistory bounded;
    for (unsigned i = 0; i < nr::ScaleHistory::capacity; ++i) assert(bounded.find(1, i));
    assert(!bounded.find(2, 0));
    bounded.forget(1);
    assert(bounded.find(2, 0));
    puts("Scale history: 200 generations, 40,000 successful pass records; reset-once, failure retry, independent devices/passes, capacity/reuse passed.");
}
