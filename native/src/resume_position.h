// SPDX-License-Identifier: 0BSD
#pragma once
namespace mdx {
inline double resumePosition(bool preparing, double retained, double current, double seek) {
    return seek >= 0 ? seek : preparing ? retained : current;
}
}
