// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include "player.h"
namespace mdx {
// YM2151 register log + stereo mixed PCM carried by direct signed 12-bit PWM L/R writes.
Bytes makeVgm(Song song, std::shared_ptr<Pdx> bank, const std::function<void(int)> &progress = {});
}
