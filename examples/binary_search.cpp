#include <array>
#include <iostream>

#ifdef DEBUG
#include "traceprism.hpp"
#else
#define record(...) ((void)0)
#endif

int main() {
    const std::array values{2, 5, 8, 12, 16, 21, 30, 42, 57, 68, 74, 88};
    const int target = 42;
    int left = 0;
    int right = static_cast<int>(values.size());
    int step = 0;

    while (left < right) {
        const int mid = (left + right) / 2;
        const int selected = values[mid];
        const bool predicate = selected == target;
        record(step, values, target, left, right, mid, selected, predicate);
        if (selected < target) left = mid + 1;
        else right = mid;
        ++step;
    }
    const int mid = left;
    const bool predicate = left < static_cast<int>(values.size()) && values[left] == target;
    record(step, values, target, left, right, mid, predicate);
    std::cout << "index: " << left << '\n';
}
