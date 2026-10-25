#pragma once
#include <array>
#include <algorithm>
namespace mdx {
// Logical operator order: OP1=M1, OP2=C1, OP3=M2, OP4=C2.
struct AlgorithmLayout {
    std::array<std::array<int, 2>, 3> edges{};
    int edgeCount = 0, carriers = 0, maxLevel = 0;
    std::array<int, 4> level{}, row{}, rows{};
};
inline AlgorithmLayout algorithmLayout(int algorithm) {
    static constexpr int edges[8][3][2] = {{{0, 1}, {1, 2}, {2, 3}},     {{0, 2}, {1, 2}, {2, 3}},
                                           {{1, 2}, {0, 3}, {2, 3}},     {{0, 1}, {1, 3}, {2, 3}},
                                           {{0, 1}, {2, 3}, {-1, -1}},   {{0, 1}, {0, 2}, {0, 3}},
                                           {{0, 1}, {-1, -1}, {-1, -1}}, {{-1, -1}, {-1, -1}, {-1, -1}}};
    static constexpr int carriers[] = {8, 8, 8, 8, 10, 14, 14, 15};
    AlgorithmLayout l;
    algorithm = std::clamp(algorithm, 0, 7);
    l.carriers = carriers[algorithm];
    for (auto &e : edges[algorithm])
        if (e[0] >= 0)
            l.edges[l.edgeCount++] = {e[0], e[1]};
    for (int pass = 0; pass < 4; ++pass)
        for (int i = 0; i < l.edgeCount; ++i)
            l.level[l.edges[i][1]] = std::max(l.level[l.edges[i][1]], l.level[l.edges[i][0]] + 1);
    for (int v : l.level)
        l.maxLevel = std::max(l.maxLevel, v);
    for (int op = 0; op < 4; ++op)
        if (l.carriers & (1 << op))
            l.level[op] = l.maxLevel;
    std::array<int, 4> counts{};
    for (int op = 0; op < 4; ++op)
        l.row[op] = counts[l.level[op]]++;
    for (int op = 0; op < 4; ++op)
        l.rows[op] = counts[l.level[op]];
    return l;
}
} // namespace mdx
