#pragma once
#include <cstdint>
namespace nr {
// A screenshot pair must come from one uninterrupted, ordered NR pass chain.
// Tickets prevent a failed/aborted evaluation from completing a later request.
struct CaptureChain {
    std::uint64_t serial = 0, ticket = 0;
    std::uintptr_t command = 0;
    unsigned thread = 0, count = 0, next = 0;
    bool active = false, in_call = false, complete = false;
    void abort() { active = in_call = complete = false; ticket = 0; }
    void start(std::uintptr_t cmd, unsigned owner, unsigned passes) {
        abort(); command = cmd; thread = owner; count = passes; next = 0;
        active = cmd && owner && passes >= 1 && passes <= 10;
    }
    std::uint64_t enter(std::uintptr_t cmd, unsigned owner, unsigned pass) {
        if (!active) return 0;
        if (cmd != command || owner != thread || pass != next || in_call) { abort(); return 0; }
        in_call = true; ticket = ++serial; if (!ticket) ticket = ++serial;
        return ticket;
    }
    bool leave(std::uint64_t value, bool success) {
        if (!active || !in_call || !value || value != ticket) return false;
        in_call = false;
        if (!success) { abort(); return false; }
        if (++next == count) { active = false; complete = true; return true; }
        return false;
    }
};
}
