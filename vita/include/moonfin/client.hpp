// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace moonfin {
using Json = nlohmann::json;
using Params = std::map<std::string, std::string>;
constexpr int64_t ticksPerSecond = 10000000;
constexpr const char *version = "0.1.2";
constexpr int maxVideoWidth = 960;
constexpr int maxVideoHeight = 544;

struct Response {
  long status = 0;
  std::string body;
};
class Transport {
public:
  virtual ~Transport() = default;
  virtual Response send(const std::string &method, const std::string &url,
                        const Params &headers, const std::string &body,
                        size_t limit = 8 * 1024 * 1024) = 0;
};
class CurlTransport : public Transport {
public:
  explicit CurlTransport(std::string caFile = "") : caFile(std::move(caFile)) {}
  Response send(const std::string &, const std::string &, const Params &,
                const std::string &, size_t) override;

private:
  std::string caFile;
};

struct Account {
  std::string url, serverName, userId, userName, token;
};
void to_json(Json &, const Account &);
void from_json(const Json &, Account &);
std::string stringValue(const Json &, const std::string &key,
                        const std::string &fallback = "");
int64_t integerValue(const Json &, const std::string &key,
                     int64_t fallback = 0);
std::string encode(const std::string &);
std::string normalizeServerUrl(std::string);
std::string newDeviceId();
Json vitaProfile(int maxBitrate = 4000000, int maxHeight = 544);

struct PlaybackOptions {
  int bitrate = 4000000;
  int height = 544;
  int audioIndex = -1;
  int subtitleIndex = -1;
  bool forceTranscode = false;
  std::string sourceId;
};
struct Playback {
  std::string itemId, sourceId, playSessionId, liveStreamId, url, method;
  Json streams = Json::array();
  int64_t startTicks = 0;
  int audioIndex = -1, subtitleIndex = -1;
};

class Client {
public:
  Client(std::shared_ptr<Transport>, Account, std::string deviceId);
  const Account &account() const { return current; }
  const std::string &deviceId() const { return device; }
  Json request(const std::string &method, const std::string &path,
               Params query = {}, Json body = nullptr) const;
  Json serverInfo() const;
  Account login(const std::string &username, const std::string &password);
  Account authenticateQuickConnect(const std::string &secret);
  Json views() const;
  Json items(Params query) const;
  Json item(const std::string &id) const;
  Json resume() const;
  Json nextUp() const;
  Json latest(const std::string &parentId) const;
  Json seasons(const std::string &seriesId) const;
  Json episodes(const std::string &seriesId,
                const std::string &seasonId = "") const;
  void favorite(const std::string &id, bool value) const;
  void watched(const std::string &id, bool value) const;
  Playback resolve(const Json &item, const PlaybackOptions &,
                   int64_t startTicks) const;
  void report(const Playback &, const std::string &event, double seconds,
              bool paused) const;
  void closePlayback(const Playback &) const;
  std::string imageUrl(const Json &, int width = 160) const;
  std::string image(const Json &, int width = 160) const;
  Json segments(const std::string &itemId) const;
  std::string authenticatedUrl(const std::string &) const;
  std::string authorization() const;

private:
  std::shared_ptr<Transport> http;
  Account current;
  std::string device;
  std::string url(const std::string &, const Params &) const;
  Account acceptLogin(const Json &);
};

// The config stores tokens, never passwords. Writes use a temporary file.
struct Settings {
  std::string deviceId = newDeviceId();
  std::vector<Account> accounts;
  int active = -1;
  int bitrate = 4000000, height = 544;
  bool forceTranscode = false;
  static Settings load(const std::string &path);
  void save(const std::string &path) const;
};
} // namespace moonfin
