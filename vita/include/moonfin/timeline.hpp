// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace moonfin {
// Shared logical coordinates for rendering and touch hit testing. Borealis
// already scales Vita front-touch coordinates to this coordinate system.
struct TimelineBounds {
  double left, centerY, width;
  static TimelineBounds fromFrame(double x, double y, double w, double h) {
    return {x + 28, y + h - 68, w - 56};
  }
  bool valid() const {
    return std::isfinite(left) && std::isfinite(centerY) &&
           std::isfinite(width) && width > 0;
  }
  bool contains(double x, double y) const {
    return valid() && x >= left - 12 && x <= left + width + 12 &&
           std::abs(y - centerY) <= 28;
  }
};

// One captured finger, with a preview while dragging and one seek on release.
// It never pauses/resumes playback for a timeline gesture.
class TimelineTouch {
public:
  enum class Phase { Begin, Move, End };
  struct Result {
    bool accepted = false;
    bool reveal = false;
    bool togglePause = false;
    std::optional<double> seek;
  };
  Result update(Phase phase, int id, double x, double y,
                TimelineBounds bounds, double duration, bool controlsVisible) {
    if (phase == Phase::Begin) {
      if (active() && id != finger)
        return {};
      cancel();
      if (!bounds.valid() || !std::isfinite(x) || !std::isfinite(y))
        return {};
      finger = id;
      track = bounds;
      length = duration;
      startX = x;
      startY = y;
      moved = false;
      if (!controlsVisible)
        mode = Mode::Reveal;
      else if (track.contains(x, y))
        mode = std::isfinite(length) && length > 0 ? Mode::Scrub : Mode::Reveal;
      else
        mode = Mode::Tap;
    } else if (!active() || id != finger) {
      return {};
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
      cancel();
      return {true, false, false, std::nullopt};
    }
    Result result{true, true, false, std::nullopt};
    moved = moved || std::hypot(x - startX, y - startY) > 12;
    if (mode == Mode::Scrub)
      target = std::clamp((x - track.left) / track.width, 0.0, 1.0) * length;
    if (phase == Phase::End) {
      if (mode == Mode::Scrub)
        result.seek = target;
      else if (mode == Mode::Tap && !moved)
        result.togglePause = true;
      cancel();
    }
    return result;
  }
  void cancel() { mode = Mode::Idle; }
  bool active() const { return mode != Mode::Idle; }
  bool scrubbing() const { return mode == Mode::Scrub; }
  double preview() const { return target; }

private:
  enum class Mode { Idle, Reveal, Tap, Scrub };
  Mode mode = Mode::Idle;
  int finger = 0;
  TimelineBounds track{};
  double length = 0, target = 0, startX = 0, startY = 0;
  bool moved = false;
};
} // namespace moonfin
