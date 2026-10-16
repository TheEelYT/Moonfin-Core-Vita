// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "client.hpp"
#include <memory>
namespace moonfin {
class Player;
void runApp(Settings &, const std::string &configPath,
            std::shared_ptr<Transport>, Player &);
void tickApp();
void stopApp();
} // namespace moonfin
