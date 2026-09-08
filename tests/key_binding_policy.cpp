#include "../src/key_binding_policy.hpp"
#include "../src/pass_count_policy.hpp"
#include <cassert>
#include <cstdio>
int main()
{
    assert(nr::default_keys == (std::array<int,5>{0x75,0x76,0x74,0xBB,0xBD}));
    assert(nr::resolve_bindings({}) == nr::default_keys);
    assert((nr::resolve_bindings({0x76,0,0,0,0}) == std::array<int,5>{0x76,0x6a,0x74,0xBB,0xBD}));
    assert((nr::resolve_bindings({0,0x6a,0,0,0}) == std::array<int,5>{0x75,0x6a,0x74,0xBB,0xBD}));
    assert((nr::resolve_bindings({0x77,0x78,0x79,0x7A,0x7B}) == std::array<int,5>{0x77,0x78,0x79,0x7A,0x7B}));
    assert(!nr::valid_binding(27) && !nr::valid_binding(0) && !nr::valid_binding(255));
    for (int key = 8; key < 255; ++key) assert(nr::valid_binding(key) == (key != 27));
    for (unsigned i = 0; i < 5; ++i)
        for (unsigned j = 0; j < 5; ++j)
            assert(nr::binding_conflict(nr::default_keys, i, nr::default_keys[j]) == (i != j));
    for (unsigned action = 0; action < 5; ++action) {
        nr::KeyEdges edges;
        std::array<bool, 5> down = {}; down[action] = true;
        assert(!edges.update(down, true)[action]); // held at startup cannot fire
        edges.update({}, true);
        assert(edges.update(down, true)[action]);
        for (int repeat = 0; repeat < 100; ++repeat) assert(!edges.update(down, true)[action]);
        edges.update({}, true);
        assert(edges.update(down, true)[action]);
        assert(!edges.update(down, false)[action]); // focus loss / rebinding / typing
        assert(!edges.update(down, true)[action]); // release required after suppression
        edges.update({}, true);
        assert(edges.update(down, true)[action]);
        edges.release_required = true; // binding changed while selected key held
        assert(!edges.update(down, true)[action]);
        edges.update({}, true);
        assert(edges.update(down, true)[action]);
    }
    assert(nr::clamp_pass_count(0) == 1 && nr::clamp_pass_count(99) == 10);
    assert(nr::adjust_pass_count(1,-1) == 1 && nr::adjust_pass_count(10,1) == 10);
    assert(nr::adjust_pass_count(1,1) == 2 && nr::adjust_pass_count(10,-1) == 9);
    std::puts("Key binding policy: F6/F7/F5/=/-, saved-key priority, conflicts, edges and 1-10 pass bounds passed.");
}
