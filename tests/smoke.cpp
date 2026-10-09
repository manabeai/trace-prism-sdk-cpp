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
    auto first = record(traceprism::span("root", 0),
                        TP_FIELD(state), TP_FIELD(visited), TP_FIELD(active));
    assert(first.seq == 0);
    state.node = 3;
    auto second = record(traceprism::span("child", 1), traceprism::from(first),
                         TP_FIELD(state));
    assert(second.seq == 1);
    auto third = record(traceprism::span("other", 2),
                        traceprism::from(traceprism::span("root", 0)),
                        TP_FIELD(state));
    assert(third.seq == 2);
}
