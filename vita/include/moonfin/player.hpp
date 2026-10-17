// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "client.hpp"
#include <atomic>
#include <borealis.hpp>
#include <chrono>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gxm.h>
#include <nanovg_gxm_utils.h>

namespace moonfin {
class Player {
public:
  Player();
  ~Player();
  void open(std::shared_ptr<Client>, const Json &item, Playback);
  void stop();
  void pump();
  void draw(NVGcontext *, float x, float y, float w, float h);
  void togglePause();
  void seek(double seconds, bool absolute = false);
  void volume(int delta);
  void skipSegment();
  double position() const { return pos; }
  double duration() const { return length; }
  bool paused() const { return pause; }
  bool active() const { return playing; }
  const std::string &error() const { return failure; }
  std::function<void(bool)> onEnd;
  std::function<void()> onState;
  std::string title, method;
  std::string activeSegment() const;

private:
  mpv_handle *mpv = nullptr;
  mpv_render_context *renderer = nullptr;
  mpv_gxm_fbo target{};
  NVGXMframebuffer *framebuffer = nullptr;
  int image = 0;
  std::atomic<bool> dirty{false};
  std::shared_ptr<Client> client;
  Playback playback;
  Json segments = Json::array();
  std::shared_ptr<int> lifetime = std::make_shared<int>(0);
  double pos = 0, length = 0;
  bool pause = false, playing = false, reported = false, loaded = false;
  std::string failure;
  std::chrono::steady_clock::time_point lastReport;
  void report(const std::string &event);
  void command(std::initializer_list<const char *> args);
};
} // namespace moonfin
