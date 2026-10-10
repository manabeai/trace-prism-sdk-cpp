#include "traceprism.hpp"

#include <cassert>
#include <map>
#include <set>
#include <vector>

struct State {
    int node;
    std::vector<int> distance;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(State, node, distance)

int main() {
    State state{2, {0, 3, 8}};
    std::set<int> visited{0, 2};
    std::map<int, bool> active{{0, true}, {2, false}};
    const int step1 = 0;
    const int step2 = 1;
    auto first = record({step1, step2}, state, visited, active);
    assert(first.seq == 0);
    state.node = 3;
    auto second = record({"child", 1}, traceprism::from(first), state);
    assert(second.seq == 1);
    auto third = record({"other", 2},
                        traceprism::from(traceprism::span(step1, step2)),
                        TP_FIELD(state));
    assert(third.seq == 2);
    auto fourth = record(3);
    assert(fourth.seq == 3);
}
