// SPDX-License-Identifier: GPL-3.0-or-later
// GXM/mpv framebuffer integration adapted from Switchfin's mpv_core.cpp.
// Copyright 2022-2026 xfangfang and dragonflylee. Apache-2.0.
// See licenses/Switchfin-Apache-2.0.txt and THIRD_PARTY_NOTICES.md.
#include "moonfin/player.hpp"
#include "moonfin/runtime.hpp"
#include <algorithm>
#include <borealis/extern/nanovg/nanovg_gxm.h>
#include <borealis/platforms/psv/psv_video.hpp>
#include <cmath>
#include <stdexcept>

namespace moonfin {
namespace {
void check(int result, const char *what) {
  if (result < 0)
    throw std::runtime_error(std::string(what) + ": " +
                             mpv_error_string(result));
}
} // namespace
Player::Player() {
  mpv = mpv_create();
  if (!mpv)
    throw std::runtime_error("Cannot create the Vita media player.");
  auto option = [this](const char *n, const char *v) {
    check(mpv_set_option_string(mpv, n, v), n);
  };
  try {
    option("config", "no");
    option("vo", "libmpv");
    option("hwdec", "auto");
    option("audio-channels", "stereo");
    option("vd-lavc-threads", "3");
    option("fbo-format", "rgba8");
    option("video-latency-hacks", "yes");
    option("demuxer-max-bytes", "16MiB");
    option("demuxer-max-back-bytes", "4MiB");
    option("ytdl", "no");
    option("osd-level", "0");
    option("input-default-bindings", "no");
    option("tls-verify", "yes");
    option("tls-ca-file", "app0:resources/cacert.pem");
    option("sub-auto", "no");
    option("audio-file-auto", "no");
    option("keep-open", "no");
    check(mpv_initialize(mpv), "Initialize player");
    auto *video = dynamic_cast<brls::PsvVideoContext *>(
        brls::Application::getPlatform()->getVideoContext());
    if (!video)
      throw std::runtime_error("Vita GXM renderer unavailable.");
    auto *gxm = video->getWindow();
    auto *vg = brls::Application::getNVGContext();
    mpv_gxm_init_params init{};
    init.context = gxm->context;
    init.shader_patcher = gxm->shader_patcher;
    init.buffer_index = 0;
    init.msaa = SCE_GXM_MULTISAMPLE_4X;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE,
                                  const_cast<char *>(MPV_RENDER_API_TYPE_GXM)},
                                 {MPV_RENDER_PARAM_GXM_INIT_PARAMS, &init},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};
    check(mpv_render_context_create(&renderer, mpv, params),
          "Initialize GXM video output");
    image = nvgCreateImageRGBA(vg, 960, 544, 0, nullptr);
    if (!image)
      throw std::runtime_error("Cannot allocate the video texture.");
    NVGXMframebufferInitOptions opts{};
    opts.display_buffer_count = 1;
    opts.scenesPerFrame = 1;
    opts.render_target = nvgxmImageHandle(vg, image);
    opts.color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR;
    opts.color_surface_type = SCE_GXM_COLOR_SURFACE_LINEAR;
    opts.display_width = 960;
    opts.display_height = 544;
    opts.display_stride = 960;
    framebuffer = gxmCreateFramebuffer(&opts);
    if (!framebuffer)
      throw std::runtime_error("Cannot allocate the video framebuffer.");
    target.render_target = framebuffer->gxm_render_target;
    target.color_surface = &framebuffer->gxm_color_surfaces[0].surface;
    target.depth_stencil_surface = &framebuffer->gxm_depth_stencil_surface;
    target.w = 960;
    target.h = 544;
    target.format = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_RGBA;
    mpv_observe_property(mpv, 1, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 2, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 3, "pause", MPV_FORMAT_FLAG);
    mpv_render_context_set_update_callback(
        renderer, [](void *p) { static_cast<Player *>(p)->dirty = true; },
        this);
    // Errors are reported through MPV_EVENT_END_FILE. URLs/tokens are never
    // logged.
  } catch (...) {
    if (renderer)
      mpv_render_context_free(renderer);
    renderer = nullptr;
    if (mpv)
      mpv_terminate_destroy(mpv);
    mpv = nullptr;
    if (framebuffer)
      gxmDeleteFramebuffer(framebuffer);
    if (image)
      nvgDeleteImage(brls::Application::getNVGContext(), image);
    throw;
  }
}
Player::~Player() {
  lifetime.reset();
  stop();
  if (renderer) {
    mpv_render_context_set_update_callback(renderer, nullptr, nullptr);
    mpv_render_context_free(renderer);
  }
  if (mpv)
    mpv_terminate_destroy(mpv);
  if (framebuffer)
    gxmDeleteFramebuffer(framebuffer);
  if (image)
    nvgDeleteImage(brls::Application::getNVGContext(), image);
}
void Player::command(std::initializer_list<const char *> list) {
  std::vector<const char *> args(list);
  args.push_back(nullptr);
  check(mpv_command_async(mpv, 0, args.data()), "Player command");
}
void Player::open(std::shared_ptr<Client> c, const Json &item, Playback p) {
  stop();
  client = std::move(c);
  playback = std::move(p);
  segments = Json::array();
  failure.clear();
  title = stringValue(item, "Name");
  method = playback.method;
  pos = playback.startTicks / static_cast<double>(ticksPerSecond);
  length =
      integerValue(item, "RunTimeTicks") / static_cast<double>(ticksPerSecond);
  pause = false;
  reported = false;
  loaded = false;
  playing = true;
  brls::Application::getPlatform()->disableScreenDimming(true);
  lastReport = std::chrono::steady_clock::now();
  // Set start before loadfile: VOD URLs remain zero-based, so resume is applied
  // exactly once.
  const auto start = std::to_string(pos);
  check(mpv_set_property_string(mpv, "start", start.c_str()),
        "Resume position");
  check(mpv_set_property_string(mpv, "pause", "no"), "Resume player");
  command({"loadfile", playback.url.c_str(), "replace"});
  auto weak = std::weak_ptr<int>(lifetime);
  auto identity = playback.playSessionId;
  auto source = client;
  jobs->submit(
      [this, weak, identity, source, id = playback.itemId] {
        Json result;
        try {
          result = source->segments(id);
        } catch (...) {
          return;
        }
        jobs->complete([this, weak, identity, result = std::move(result)] {
          if (weak.expired() || playback.playSessionId != identity)
            return;
          segments =
              result.is_array() ? result : result.value("Items", Json::array());
        });
      },
      false);
}
void Player::report(const std::string &event) {
  if (!client || !reported)
    return;
  auto c = client;
  auto p = playback;
  const auto seconds = pos;
  const auto paused = pause;
  jobs->submit(
      [c, p, event, seconds, paused] {
        try {
          c->report(p, event, seconds, paused);
        } catch (...) {
        }
      },
      true);
}
void Player::stop() {
  if (!playing)
    return;
  // Capture position before issuing stop; mpv will reset time-pos afterward.
  auto c = client;
  auto p = playback;
  const auto seconds = pos;
  const auto wasPaused = pause;
  const bool shouldReport = reported;
  jobs->submit(
      [c, p, seconds, wasPaused, shouldReport] {
        if (shouldReport) {
          try {
            c->report(p, "Playing/Stopped", seconds, wasPaused);
          } catch (...) {
          }
        }
        try {
          c->closePlayback(p);
        } catch (...) {
        }
      },
      true);
  command({"stop"});
  playing = false;
  brls::Application::getPlatform()->disableScreenDimming(false);
  reported = false;
  loaded = false;
  onEnd = nullptr;
  onState = nullptr;
}
void Player::pump() {
  // Called before Application::mainLoop(), outside any active GXM scene.
  if (dirty.exchange(false) && renderer) {
    auto flags = mpv_render_context_update(renderer);
    if (flags & MPV_RENDER_UPDATE_FRAME) {
      int flip = 1;
      mpv_render_param params[] = {{MPV_RENDER_PARAM_FLIP_Y, &flip},
                                   {MPV_RENDER_PARAM_GXM_FBO, &target},
                                   {MPV_RENDER_PARAM_INVALID, nullptr}};
      mpv_render_context_render(renderer, params);
      mpv_render_context_report_swap(renderer);
    }
  }
  for (;;) {
    auto *e = mpv_wait_event(mpv, 0);
    if (e->event_id == MPV_EVENT_NONE)
      break;
    if (!playing)
      continue;
    if (e->event_id == MPV_EVENT_PROPERTY_CHANGE) {
      auto *p = static_cast<mpv_event_property *>(e->data);
      if (!p->data)
        continue;
      if (e->reply_userdata == 1)
        pos = *static_cast<double *>(p->data);
      else if (e->reply_userdata == 2)
        length = *static_cast<double *>(p->data);
      else if (e->reply_userdata == 3) {
        pause = *static_cast<int *>(p->data);
        if (loaded) {
          report("Playing/Progress");
          if (onState)
            onState();
        }
      }
    } else if (e->event_id == MPV_EVENT_FILE_LOADED) {
      loaded = true;
      reported = true;
      report("Playing");
    } else if (e->event_id == MPV_EVENT_END_FILE) {
      auto *end = static_cast<mpv_event_end_file *>(e->data);
      if (end->reason == MPV_END_FILE_REASON_STOP ||
          end->reason == MPV_END_FILE_REASON_REDIRECT)
        continue;
      const bool eof = end->reason == MPV_END_FILE_REASON_EOF;
      if (!eof)
        failure =
            "Playback failed: " + std::string(mpv_error_string(end->error)) +
            ". Try Force transcoding in settings.";
      auto cb = onEnd;
      stop();
      if (cb)
        cb(eof);
    }
  }
  if (playing && loaded &&
      std::chrono::steady_clock::now() - lastReport >
          std::chrono::seconds(10)) {
    lastReport = std::chrono::steady_clock::now();
    report("Playing/Progress");
  }
}
void Player::draw(NVGcontext *vg, float x, float y, float w, float h) {
  nvgBeginPath(vg);
  nvgRect(vg, x, y, w, h);
  nvgFillColor(vg, nvgRGB(0, 0, 0));
  nvgFill(vg);
  if (playing && loaded) {
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillPaint(vg, nvgImagePattern(vg, x, y, w, h, 0, image, 1));
    nvgFill(vg);
  }
}
void Player::togglePause() { command({"cycle", "pause"}); }
void Player::seek(double seconds, bool absolute) {
  auto s = std::to_string(seconds);
  command({"seek", s.c_str(), absolute ? "absolute" : "relative"});
}
void Player::volume(int delta) {
  auto v = std::to_string(delta);
  command({"add", "volume", v.c_str()});
}
std::string Player::activeSegment() const {
  for (const auto &s : segments)
    if (pos * ticksPerSecond >= integerValue(s, "StartTicks") &&
        pos * ticksPerSecond < integerValue(s, "EndTicks"))
      return stringValue(s, "Type", "segment");
  return "";
}
void Player::skipSegment() {
  for (const auto &s : segments)
    if (pos * ticksPerSecond >= integerValue(s, "StartTicks") &&
        pos * ticksPerSecond < integerValue(s, "EndTicks")) {
      seek(std::ceil(integerValue(s, "EndTicks") /
                     static_cast<double>(ticksPerSecond)),
           true);
      return;
    }
}
} // namespace moonfin
