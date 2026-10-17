// SPDX-License-Identifier: GPL-3.0-or-later
#include "moonfin/timeline.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace moonfin;
using Phase = TimelineTouch::Phase;
namespace {
int checks = 0;
void require(bool ok, const char *text) {
  ++checks;
  if (!ok)
    throw std::runtime_error(text);
}
bool near(double a, double b) { return std::abs(a - b) < 0.0001; }
void test() {
  const auto bar = TimelineBounds::fromFrame(0, 0, 1280, 720);
  TimelineTouch touch;
  auto event = [&](Phase phase, double fraction, bool visible = true,
                   int finger = 7, double duration = 3600) {
    return touch.update(phase, finger, bar.left + bar.width * fraction,
                        bar.centerY, bar, duration, visible);
  };
  auto result = event(Phase::Begin, 0.25);
  require(result.accepted && result.reveal && !result.seek && !result.togglePause,
          "bar press reveals preview without seeking or pausing");
  require(touch.scrubbing() && near(touch.preview(), 900), "quarter timeline preview");
  result = event(Phase::Move, 0.75);
  require(!result.seek && !result.togglePause && near(touch.preview(), 2700),
          "drag only updates preview");
  result = event(Phase::End, 0.75);
  require(result.seek && near(*result.seek, 2700) && !result.togglePause,
          "release seeks once without changing play/pause state");
  require(!touch.active(), "release clears capture");
  require(!event(Phase::End, 0.75).seek, "duplicate release cannot seek again");
  event(Phase::Begin, 0.5);
  event(Phase::Move, -1);
  require(near(touch.preview(), 0), "drag clamps at the beginning");
  result = event(Phase::End, 2);
  require(result.seek && near(*result.seek, 3600), "drag clamps at the end");
  event(Phase::Begin, 0.1);
  result = event(Phase::End, 0.1);
  require(result.seek && near(*result.seek, 360), "tapping the visible bar seeks on release");
  event(Phase::Begin, 0.2);
  touch.cancel();
  result = event(Phase::End, 0.8);
  require(!result.accepted && !result.seek && !result.togglePause,
          "interrupted gesture cannot seek or pause");
  event(Phase::Begin, 0.2);
  result = event(Phase::Begin, 0.9, true, 9);
  require(!result.accepted && near(touch.preview(), 720), "second finger cannot replace capture");
  require(!event(Phase::End, 0.9, true, 9).seek, "second finger cannot commit");
  result = event(Phase::End, 0.4);
  require(result.seek && near(*result.seek, 1440), "original finger still commits");
  result = event(Phase::Begin, 0.3, false);
  require(result.reveal && !touch.scrubbing(), "hidden controls only reveal on first touch");
  event(Phase::Move, 0.7);
  result = event(Phase::End, 0.7);
  require(!result.seek && !result.togglePause, "reveal gesture cannot become a blind seek or pause");
  for (double duration : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()}) {
    event(Phase::Begin, 0.5, true, 7, duration);
    result = event(Phase::End, 0.6, true, 7, duration);
    require(!result.seek && !result.togglePause, "unknown/live duration cannot seek");
  }
  auto outside = [&](Phase phase, double x = 200, double y = 200) {
    return touch.update(phase, 3, x, y, bar, 3600, true);
  };
  outside(Phase::Begin);
  result = outside(Phase::End);
  require(result.togglePause && !result.seek, "visible video-area tap retains pause toggle");
  outside(Phase::Begin);
  outside(Phase::Move, 250, 250);
  result = outside(Phase::End, 200, 200);
  require(!result.togglePause && !result.seek, "swipe outside bar does not pause even after returning");
  event(Phase::Begin, 0.4);
  result = event(Phase::End, 0.5, true, 7, 7200);
  require(result.seek && near(*result.seek, 1800), "duration updates do not move the captured target");
  const auto offset = TimelineBounds::fromFrame(100, 40, 960, 544);
  touch.update(Phase::Begin, 1, offset.left + offset.width / 2,
               offset.centerY + 20, offset, 120, true);
  result = touch.update(Phase::End, 1, offset.left + offset.width / 2,
                        offset.centerY, offset, 120, true);
  require(result.seek && near(*result.seek, 60), "hit area uses shared offset/scaled frame coordinates");
  const auto invalid = TimelineBounds::fromFrame(0, 0, 40, 544);
  require(!touch.update(Phase::Begin, 1, 0, 0, invalid, 120, true).accepted,
          "invalid layout cannot start a seek");
  event(Phase::Begin, 0.5);
  result = touch.update(Phase::Move, 7, std::numeric_limits<double>::quiet_NaN(),
                        bar.centerY, bar, 120, true);
  require(!touch.active() && !result.seek, "invalid touch cancels capture");
}
} // namespace
int main() {
  try {
    test();
    std::cout << checks << " timeline checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
