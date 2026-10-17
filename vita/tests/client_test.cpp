// SPDX-License-Identifier: GPL-3.0-or-later
#include "moonfin/client.hpp"
#include "moonfin/runtime.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace moonfin;
namespace {
int checks = 0;
void require(bool b, const std::string &msg) {
  ++checks;
  if (!b)
    throw std::runtime_error(msg);
}
template <class F> void throws(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  require(failed, "Expected an exception");
}
struct Fake : Transport {
  struct Call {
    std::string method, url;
    Params headers;
    Json body;
    size_t limit;
  };
  std::vector<Call> calls;
  Response next{200, "{}"};
  Response send(const std::string &m, const std::string &u, const Params &h,
                const std::string &b, size_t l) override {
    calls.push_back({m, u, h, b.empty() ? Json() : Json::parse(b), l});
    return next;
  }
  void reply(const Json &j) { next = {200, j.dump()}; }
  Call &last() { return calls.back(); }
};
Json source(
    bool direct = false,
    std::string url =
        "/Videos/movie/master.m3u8?StartTimeTicks=500000000&VideoCodec=h264") {
  return {
      {"PlaySessionId", "session"},
      {"MediaSources",
       Json::array({{{"Id", "source"},
                     {"SupportsDirectPlay", direct},
                     {"TranscodingUrl", url},
                     {"MediaStreams", Json::array({{{"Index", 0},
                                                    {"Type", "Video"},
                                                    {"Codec", "h264"},
                                                    {"Width", 640},
                                                    {"Height", 360}},
                                                   {{"Index", 2},
                                                    {"Type", "Subtitle"},
                                                    {"Codec", "ass"}}})}}})}};
}
void protocol() {
  require(normalizeServerUrl(" https://example.org/jellyfin/// ") ==
              "https://example.org/jellyfin",
          "base path normalization");
  require(normalizeServerUrl("192.168.1.4:8096") == "http://192.168.1.4:8096",
          "LAN URL");
  throws([] { normalizeServerUrl("ftp://example.org"); });
  throws([] { normalizeServerUrl("https://user:password@example.org"); });
  throws([] { normalizeServerUrl("https://example.org?api_key=secret"); });
  throws([] { normalizeServerUrl("https://"); });
  require(encode("A+B & /") == "A%2BB%20%26%20%2F", "query escaping");
  auto f = std::make_shared<Fake>();
  Client c(
      f, {"https://server.test/jellyfin", "Server", "user", "Gene", "token+&"},
      "device");
  c.items({{"ParentId", "a/b"}, {"SearchTerm", "A+B"}, {"StartIndex", "30"}});
  require(f->last().url.find("/jellyfin/Users/user/Items?") !=
              std::string::npos,
          "library endpoint");
  require(f->last().url.find("ParentId=a%2Fb") != std::string::npos,
          "ID encoding");
  require(f->last().url.find("StartIndex=30") != std::string::npos,
          "pagination");
  require(f->last().headers.at("X-Emby-Token") == "token+&", "header auth");
  require(f->last().headers.at("Authorization").find("DeviceId=\"device\"") !=
              std::string::npos,
          "device identity");
  require(
      c.authenticatedUrl("/jellyfin/Videos/1/stream") ==
          "https://server.test/jellyfin/Videos/1/stream?api_key=token%2B%26",
      "base path not duplicated");
  require(
      c.authenticatedUrl("/Videos/1/stream") ==
          "https://server.test/jellyfin/Videos/1/stream?api_key=token%2B%26",
      "relative stream path");
  throws([&] { c.authenticatedUrl("https://attacker.test/stream"); });
  throws([&] { c.authenticatedUrl("//attacker.test/stream"); });
  f->reply({{"AccessToken", "secret"},
            {"User", {{"Id", "user2"}, {"Name", "Gene"}}}});
  auto account = c.login("Gene", "pass");
  require(f->last().body["Pw"] == "pass" &&
              f->last().body["Username"] == "Gene",
          "login JSON");
  require(account.token == "secret" && account.userId == "user2",
          "login result");
  f->reply(Json::object());
  throws([&] { c.login("Gene", "pass"); });
  // Keep a fresh authenticated client after the deliberately malformed
  // response.
  Client player(
      f, {"https://server.test/jellyfin", "Server", "user", "Gene", "token"},
      "device");
  auto item = Json{{"Id", "movie"}, {"Type", "Movie"}, {"MediaType", "Video"}};
  PlaybackOptions options;
  f->reply(source(true));
  auto direct = player.resolve(item, options, 500000000);
  require(direct.method == "DirectPlay" &&
              direct.url.find("Static=true") != std::string::npos,
          "compatible file direct play");
  require(direct.startTicks == 500000000, "resume retained");
  require(f->last().body["StartTimeTicks"] == 0,
          "resume only applied by player");
  require(f->last().body["SubtitleStreamIndex"] == -1,
          "subtitles off is explicit");
  f->reply(source(false));
  auto transcode = player.resolve(item, options, 500000000);
  require(transcode.method == "Transcode" &&
              transcode.url.find("StartTimeTicks") == std::string::npos,
          "transcoded timeline is zero based");
  require(transcode.url.find("api_key=token") != std::string::npos,
          "transcode authenticated");
  options.subtitleIndex = 2;
  f->reply(source(false));
  player.resolve(item, options, 0);
  require(f->last().body["SubtitleStreamIndex"] == 2 &&
              !f->last().body["EnableDirectPlay"].get<bool>(),
          "subtitle selection forces renegotiation");
  options.subtitleIndex = -1;
  options.audioIndex = 3;
  player.resolve(item, options, 0);
  require(f->last().body["AudioStreamIndex"] == 3 &&
              !f->last().body["EnableDirectStream"].get<bool>(),
          "audio track selection");
  options.audioIndex = -1;
  options.forceTranscode = true;
  player.resolve(item, options, 0);
  require(!f->last().body["EnableDirectPlay"].get<bool>(), "force transcode");
  f->reply(source(false, ""));
  throws([&] { player.resolve(item, options, 0); });
  f->reply({{"MediaSources", Json::array()}});
  throws([&] { player.resolve(item, options, 0); });
  f->reply({{"ErrorCode", "NotAllowed"}});
  throws([&] { player.resolve(item, options, 0); });
  f->reply(Json::object());
  player.report(transcode, "Playing/Progress", 75.25, true);
  require(f->last().body["PositionTicks"] == 752500000 &&
              f->last().body["IsPaused"].get<bool>(),
          "playback progress in ticks");
  require(f->last().body["PlayMethod"] == "Transcode",
          "server playback method");
  player.report(direct, "Playing/Stopped", 99, false);
  require(f->last().url.find("/Sessions/Playing/Stopped") != std::string::npos,
          "stop report endpoint");
  player.closePlayback(transcode);
  require(f->last().method == "DELETE" &&
              f->last().url.find("PlaySessionId=session") != std::string::npos,
          "transcode cleanup");
  auto live = transcode;
  live.liveStreamId = "live";
  player.closePlayback(live);
  require(f->last().url.find("/LiveStreams/Close?") != std::string::npos,
          "live stream cleanup");
  player.favorite("a/b", true);
  require(f->last().method == "POST" &&
              f->last().url.find("FavoriteItems/a%2Fb") != std::string::npos,
          "favorite endpoint");
  player.watched("movie", false);
  require(f->last().method == "DELETE", "mark unwatched");
  f->next = {401, "secret server body"};
  throws([&] { player.views(); });
  f->next = {200, "not json"};
  throws([&] { player.views(); });
  f->next = {204, ""};
  require(player.request("POST", "/Sessions/Logout").is_object(),
          "empty success response");
  f->reply({{"FileContents", "eyJyZXN1bHRzIjpbXX0="}});
  require(
      player.request("GET", "/Moonfin/Seerr/Api/search")["results"].is_array(),
      "Moonbase proxy envelope");
  auto profile = vitaProfile();
  require(profile["DirectPlayProfiles"][0]["VideoCodec"] == "h264",
          "only hardware video codec advertised");
  require(profile["CodecProfiles"][0]["Conditions"][1]["Value"] == "544",
          "height cap");
  require(profile["SubtitleProfiles"][0]["Method"] == "Encode",
          "server subtitles");
  require(
      vitaProfile(6000000, 720)["CodecProfiles"][0]["Conditions"][0]["Value"] ==
          "960" &&
      vitaProfile(6000000, 720)["CodecProfiles"][0]["Conditions"][1]["Value"] ==
          "544",
      "legacy 720p requests stay within the display cap");
  Settings settings;
  settings.accounts.push_back(account);
  settings.active = 0;
  const auto path = "moonfin-test-settings.json";
  settings.save(path);
  auto loaded = Settings::load(path);
  std::remove(path);
  require(loaded.accounts[0].token == "secret" &&
              loaded.deviceId == settings.deviceId,
          "account persistence");
  require(loaded.active == 0 && loaded.height == 544, "settings persistence");
  require(Json(account).dump().find("pass") == std::string::npos,
          "password is never serialized");
  {
    std::ofstream out(path);
    out << Json{{"height", 9000}, {"bitrate", -1}, {"active", 99}};
  }
  auto bounded = Settings::load(path);
  std::remove(path);
  require(bounded.height == 544 && bounded.bitrate == 500000 &&
              bounded.active == -1,
          "untrusted settings bounds");
}
void resolutionPolicy() {
  auto f = std::make_shared<Fake>();
  Client client(f, {"http://server.test", "Server", "user", "Gene", "token"},
                "device");
  const auto item = Json{{"Id", "movie"}, {"Type", "Movie"}, {"MediaType", "Video"}};
  auto replyWithSize = [](int w, int h) {
    auto reply = source(true);
    auto &video = reply["MediaSources"][0]["MediaStreams"][0];
    video["Width"] = w;
    video["Height"] = h;
    return reply;
  };
  for (const auto &size : std::vector<std::pair<int, int>>{
           {1920, 1080}, {3840, 2160}, {961, 544}, {960, 545}, {544, 960}}) {
    auto reply = replyWithSize(size.first, size.second);
    auto known = item;
    known["MediaSources"] = reply["MediaSources"];
    f->reply(reply);
    const auto before = f->calls.size();
    auto play = client.resolve(known, {}, 120000000);
    require(play.method == "Transcode" && f->calls.size() == before + 1,
            "known oversized source requests transcoding immediately");
    require(!f->last().body["EnableDirectPlay"].get<bool>() &&
                !f->last().body["EnableDirectStream"].get<bool>() &&
                !f->last().body["AllowVideoStreamCopy"].get<bool>(),
            "oversized video cannot bypass encoding through direct play or remux");
  }
  f->reply(replyWithSize(960, 544));
  require(client.resolve(item, {}, 0).method == "DirectPlay",
          "exact native resolution remains directly playable");
  f->reply(replyWithSize(1920, 1080));
  auto before = f->calls.size();
  auto play = client.resolve(item, {}, 120000000);
  require(f->calls.size() == before + 2 && play.method == "Transcode",
          "oversized PlaybackInfo overrides an incorrect direct-play flag");
  require(f->last().body["MediaSourceId"] == "source" &&
              play.startTicks == 120000000,
          "renegotiation retains the selected source and resume position");
  auto unknown = source(true);
  unknown["MediaSources"][0]["MediaStreams"][0].erase("Width");
  unknown["MediaSources"][0]["MediaStreams"][0].erase("Height");
  f->reply(unknown);
  require(client.resolve(item, {}, 0).method == "Transcode",
          "unknown video dimensions are not directly played");
  PlaybackOptions low;
  low.height = 360;
  low.bitrate = 1000000;
  f->reply(replyWithSize(960, 544));
  require(client.resolve(item, low, 0).method == "Transcode",
          "native-sized video respects the lower quality preset");
  require(vitaProfile(1000000, 360)["CodecProfiles"][0]["Conditions"][0]["Value"] == "640",
          "360p width cap is 640 pixels");
  f->reply(replyWithSize(640, 360));
  require(client.resolve(item, low, 0).method == "DirectPlay",
          "compatible 360p video remains directly playable");
  PlaybackOptions selected;
  selected.sourceId = "small";
  auto versions = replyWithSize(3840, 2160);
  auto small = replyWithSize(640, 360)["MediaSources"][0];
  small["Id"] = "small";
  versions["MediaSources"].push_back(small);
  auto known = item;
  known["MediaSources"] = versions["MediaSources"];
  f->reply(versions);
  play = client.resolve(known, selected, 0);
  require(play.method == "DirectPlay" && play.sourceId == "small",
          "selected low-resolution version is evaluated independently");
  selected.sourceId = "missing";
  throws([&] { client.resolve(known, selected, 0); });
  auto audio = Json{{"Id", "song"}, {"Type", "Audio"}, {"MediaType", "Audio"}};
  f->reply(source(true));
  require(client.resolve(audio, {}, 0).method == "DirectPlay",
          "audio direct play does not require video dimensions");
}
void queue() {
  Jobs q;
  std::mutex m;
  std::condition_variable cv;
  bool release = false, started = false;
  std::vector<int> order;
  q.submit([&] {
    std::unique_lock<std::mutex> lock(m);
    started = true;
    cv.notify_one();
    cv.wait(lock, [&] { return release; });
  });
  {
    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&] { return started; });
  }
  q.submit([&] { order.push_back(3); }, false);
  q.submit([&] { order.push_back(1); });
  q.submit([&] { order.push_back(2); });
  {
    std::lock_guard<std::mutex> lock(m);
    release = true;
  }
  cv.notify_one();
  q.shutdown();
  require(order.size() >= 2 && order[0] == 1 && order[1] == 2,
          "priority jobs run FIFO ahead of artwork");
  require(!q.submit([] {}), "no jobs accepted after shutdown");
}
} // namespace
int main(int argc, char **argv) {
  try {
    protocol();
    resolutionPolicy();
    queue();
    if (argc > 1) {
      auto http = std::make_shared<CurlTransport>();
      Client c(http, {argv[1], "", "", "", ""}, "test-device");
      require(c.serverInfo()["ServerName"] == "Test server",
              "real HTTP transport");
      auto a = c.login("Gene", "test-password");
      require(a.userId == "test-user", "HTTP JSON post");
      throws([&] { c.request("GET", "/too-large"); });
      throws([&] { c.request("GET", "/redirect"); });
    }
    std::cout << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
