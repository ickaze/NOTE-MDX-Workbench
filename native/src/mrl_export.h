#pragma once
#include "player.h"
namespace mdx {
void exportRegisterLog(const std::filesystem::path &input,
                       const std::filesystem::path &target,
                       const std::filesystem::path &source,
                       const std::filesystem::path &bankPath,
                       unsigned loops, const std::function<void(int)> &checkpoint);
}
