#include "../source/rumble_core.hpp"
#include <cassert>
#include <limits>
using namespace ryz::rumble;
constexpr std::uint32_t Left = 0x2004, Right = 0x12004, Pro = 3;
const Value on{.4f, 160, .2f, 320}, off{};
int main() {
    for (const auto handle : {Left, Right, Pro}) {
        State state;
        assert(state.Apply(1, &handle, &on, 1, 100));
        assert(state.Apply(1, &handle, &off, 1, 101));
        assert(state.Actual(1, handle).amp_low == 0);
        assert(state.Snapshot(110, 1000, 2).left == 0);
        const auto pulse = state.RenderSnapshot(110, 1000, 2);
        assert((Side(handle) ? pulse.right : pulse.left) > .79f);
        const auto next = state.RenderSnapshot(120, 1000, 2);
        assert(next.left == 0 && next.right == 0);
    }
    State state;
    assert(state.Apply(1, &Left, &on, 1, 100));
    assert(state.Apply(1, &Pro, &off, 1, 101));
    assert(state.RenderSnapshot(110,1000,2).left > .79f);
    assert(state.Apply(1, &Left, &off, 1, 111));
    assert(state.RenderSnapshot(120,1000,2).left == 0);
    // Expired pulses and a previous process cannot leak into a new render.
    assert(state.Apply(1, &Left, &on, 1, 200));
    assert(state.Apply(1, &Left, &off, 1, 201));
    assert(state.RenderSnapshot(1201,1000,2).left == 0);
    const Value weaker{.1f,160,.05f,320};
    assert(state.Apply(1,&Left,&on,1,100));
    assert(state.Apply(1,&Left,&weaker,1,1200));
    assert(state.RenderSnapshot(1200,1000,2).left < .21f); // weak updates cannot revive an old strong peak
    assert(state.Apply(1, &Left, &on, 1, 1300));
    assert(state.Apply(2, &Right, &off, 1, 1301));
    assert(state.RenderSnapshot(1310,1000,2).left == 0);
    // Reject a malformed batch before it can leave pending audio behind.
    const std::uint32_t handles[]{Left, Right};
    const Value invalid[]{on, {std::numeric_limits<float>::quiet_NaN(),160,0,320}};
    assert(!state.Apply(2,handles,invalid,2,1400));
    assert(state.RenderSnapshot(1410,1000,2).left == 0);
    assert(state.Apply(2,&Left,&on,1,1500));
    state.StopOwner(2);
    assert(state.RenderSnapshot(1510,1000,2).left == 0);
    // A held effect stays held; peak capture changes neither its level nor Stop.
    assert(state.Apply(3,&Left,&on,1,1600));
    assert(state.RenderSnapshot(1610,1000,2).left > .79f);
    assert(state.RenderSnapshot(1620,1000,2).left > .79f);
    assert(state.Apply(3,&Left,&off,1,1621));
    assert(state.RenderSnapshot(1630,1000,2).left == 0);
}
