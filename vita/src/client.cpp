// SPDX-License-Identifier: GPL-3.0-or-later
#include "moonfin/client.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace moonfin {
std::string stringValue(const Json &j, const std::string &k,
                        const std::string &fallback) {
  auto i = j.find(k);
  return i != j.end() && i->is_string() ? i->get<std::string>() : fallback;
}
int64_t integerValue(const Json &j, const std::string &k, int64_t fallback) {
  auto i = j.find(k);
  return i != j.end() && i->is_number_integer() ? i->get<int64_t>() : fallback;
}
void to_json(Json &j, const Account &a) {
  j = {{"url", a.url},
       {"serverName", a.serverName},
       {"userId", a.userId},
       {"userName", a.userName},
       {"token", a.token}};
}
void from_json(const Json &j, Account &a) {
  a = {normalizeServerUrl(stringValue(j, "url")), stringValue(j, "serverName"),
       stringValue(j, "userId"), stringValue(j, "userName"),
       stringValue(j, "token")};
}
std::string encode(const std::string &s) {
  const char hex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      out += c;
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}
std::string normalizeServerUrl(std::string u) {
  auto first = u.find_first_not_of(" \t\r\n"),
       last = u.find_last_not_of(" \t\r\n");
  if (first == std::string::npos)
    throw std::runtime_error("Enter a server URL.");
  u = u.substr(first, last - first + 1);
  if (u.find("://") == std::string::npos)
    u = "http://" + u;
  if (u.rfind("http://", 0) != 0 && u.rfind("https://", 0) != 0)
    throw std::runtime_error("Use an http:// or https:// server URL.");
  const auto host = u.find("://") + 3;
  const auto end = u.find('/', host);
  if (host == u.size() || u[host] == '/' ||
      u.substr(host, end - host).find('@') != std::string::npos ||
      u.find_first_of("?#\r\n\t ") != std::string::npos)
    throw std::runtime_error(
        "Invalid server URL. Include a hostname and optional base path.");
  while (u.back() == '/')
    u.pop_back();
  return u;
}
std::string newDeviceId() {
  // No hardware identifier is exposed to the server.
  std::mt19937 rng(static_cast<uint32_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count()));
  std::ostringstream out;
  out << "moonfin-vita-" << std::hex << rng() << rng() << rng() << rng();
  return out.str();
}
static std::string origin(const std::string &url) {
  auto end = url.find('/', url.find("://") + 3);
  std::string o = url.substr(0, end);
  std::transform(o.begin(), o.end(), o.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return o;
}
static std::string withoutStartTime(std::string u) {
  auto q = u.find('?');
  if (q == std::string::npos)
    return u;
  std::string result = u.substr(0, q);
  std::istringstream parts(u.substr(q + 1));
  std::string p;
  bool first = true;
  while (std::getline(parts, p, '&')) {
    std::string key = p.substr(0, p.find('='));
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (key == "starttimeticks")
      continue;
    result += (first ? '?' : '&');
    result += p;
    first = false;
  }
  return result;
}
static Json unwrapProxy(const Json &wrapper) {
  const auto text = stringValue(wrapper, "FileContents");
  if (text.empty())
    return wrapper;
  static const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint32_t value = 0;
  int bits = -8;
  std::string decoded;
  for (unsigned char c : text) {
    if (c == '=')
      break;
    if (std::isspace(c))
      continue;
    const auto n = alphabet.find(c);
    if (n == std::string::npos)
      throw std::runtime_error("The Moonbase proxy returned invalid data.");
    value = (value << 6) | static_cast<uint32_t>(n);
    bits += 6;
    if (bits >= 0) {
      decoded += static_cast<char>((value >> bits) & 255);
      bits -= 8;
    }
  }
  auto result = Json::parse(decoded, nullptr, false);
  if (result.is_discarded())
    throw std::runtime_error("The Moonbase proxy returned invalid JSON.");
  return result;
}
Client::Client(std::shared_ptr<Transport> h, Account a, std::string d)
    : http(std::move(h)), current(std::move(a)), device(std::move(d)) {
  current.url = normalizeServerUrl(current.url);
}
std::string Client::url(const std::string &path, const Params &query) const {
  if (path.empty() || path[0] != '/' || path.rfind("//", 0) == 0)
    throw std::runtime_error("Invalid API path.");
  std::string u = current.url + path;
  for (const auto &kv : query) {
    u += (u.find('?') == std::string::npos ? '?' : '&');
    u += encode(kv.first) + "=" + encode(kv.second);
  }
  return u;
}
std::string Client::authorization() const {
  return "MediaBrowser Client=\"Moonfin Vita\", Device=\"PlayStation Vita\", "
         "DeviceId=\"" +
         device + "\", Version=\"" + version + "\"" +
         (current.token.empty() ? "" : ", Token=\"" + current.token + "\"");
}
Json Client::request(const std::string &method, const std::string &path,
                     Params query, Json body) const {
  Params headers = {{"Authorization", authorization()},
                    {"Content-Type", "application/json"}};
  if (!current.token.empty())
    headers["X-Emby-Token"] = current.token;
  auto r = http->send(method, url(path, query), headers,
                      body.is_null() ? "" : body.dump());
  if (r.status < 200 || r.status >= 300) {
    if (r.status == 401 || r.status == 403)
      throw std::runtime_error("Sign-in was rejected or your session expired. "
                               "Check your account permissions.");
    throw std::runtime_error("Server returned HTTP " +
                             std::to_string(r.status) + " for " + path + ".");
  }
  if (r.body.empty())
    return Json::object();
  auto j = Json::parse(r.body, nullptr, false);
  if (j.is_discarded())
    throw std::runtime_error("The server returned invalid JSON.");
  return path.rfind("/Moonfin/Seerr/Api/", 0) == 0 ? unwrapProxy(j) : j;
}
Json Client::serverInfo() const {
  return request("GET", "/System/Info/Public");
}
Account Client::acceptLogin(const Json &j) {
  Account authenticated = current;
  authenticated.token = stringValue(j, "AccessToken");
  auto user = j.value("User", Json::object());
  authenticated.userId = stringValue(user, "Id");
  authenticated.userName = stringValue(user, "Name");
  if (authenticated.token.empty() || authenticated.userId.empty())
    throw std::runtime_error(
        "The server returned an incomplete sign-in response.");
  current = std::move(authenticated);
  return current;
}
Account Client::login(const std::string &username,
                      const std::string &password) {
  return acceptLogin(request("POST", "/Users/AuthenticateByName", {},
                             {{"Username", username}, {"Pw", password}}));
}
Account Client::authenticateQuickConnect(const std::string &secret) {
  return acceptLogin(request("POST", "/Users/AuthenticateWithQuickConnect", {},
                             {{"Secret", secret}}));
}
Json Client::views() const {
  return request("GET", "/Users/" + encode(current.userId) + "/Views");
}
Json Client::items(Params p) const {
  if (!p.count("Limit"))
    p["Limit"] = "30";
  p["Fields"] = "Overview,PrimaryImageAspectRatio,MediaSources,DateCreated";
  p["EnableImageTypes"] = "Primary,Thumb,Backdrop";
  p["ImageTypeLimit"] = "1";
  return request("GET", "/Users/" + encode(current.userId) + "/Items",
                 std::move(p));
}
Json Client::item(const std::string &id) const {
  return request("GET",
                 "/Users/" + encode(current.userId) + "/Items/" + encode(id));
}
Json Client::resume() const {
  return request(
      "GET", "/Users/" + encode(current.userId) + "/Items/Resume",
      {{"Limit", "12"}, {"MediaTypes", "Video"}, {"Fields", "Overview"}});
}
Json Client::nextUp() const {
  return request(
      "GET", "/Shows/NextUp",
      {{"UserId", current.userId}, {"Limit", "12"}, {"Fields", "Overview"}});
}
Json Client::latest(const std::string &id) const {
  return request("GET", "/Users/" + encode(current.userId) + "/Items/Latest",
                 {{"ParentId", id}, {"Limit", "12"}, {"Fields", "Overview"}});
}
Json Client::seasons(const std::string &id) const {
  return request("GET", "/Shows/" + encode(id) + "/Seasons",
                 {{"UserId", current.userId}, {"Fields", "Overview"}});
}
Json Client::episodes(const std::string &series,
                      const std::string &season) const {
  Params p = {{"UserId", current.userId},
              {"Limit", "100"},
              {"Fields", "Overview,MediaSources"}};
  if (!season.empty())
    p["SeasonId"] = season;
  return request("GET", "/Shows/" + encode(series) + "/Episodes", p);
}
void Client::favorite(const std::string &id, bool v) const {
  request(v ? "POST" : "DELETE",
          "/Users/" + encode(current.userId) + "/FavoriteItems/" + encode(id));
}
void Client::watched(const std::string &id, bool v) const {
  request(v ? "POST" : "DELETE",
          "/Users/" + encode(current.userId) + "/PlayedItems/" + encode(id));
}

Json vitaProfile(int bitrate, int height) {
  height = std::clamp(height, 360, 720);
  const int width = height <= 544 ? 960 : 1280;
  Json conditions =
      Json::array({{{"Condition", "LessThanEqual"},
                    {"Property", "Width"},
                    {"Value", std::to_string(width)},
                    {"IsRequired", true}},
                   {{"Condition", "LessThanEqual"},
                    {"Property", "Height"},
                    {"Value", std::to_string(height)},
                    {"IsRequired", true}},
                   {{"Condition", "LessThanEqual"},
                    {"Property", "VideoFramerate"},
                    {"Value", "30"},
                    {"IsRequired", true}},
                   {{"Condition", "LessThanEqual"},
                    {"Property", "VideoLevel"},
                    {"Value", "41"},
                    {"IsRequired", false}},
                   {{"Condition", "LessThanEqual"},
                    {"Property", "VideoBitDepth"},
                    {"Value", "8"},
                    {"IsRequired", false}},
                   {{"Condition", "EqualsAny"},
                    {"Property", "VideoProfile"},
                    {"Value", "baseline|constrained baseline|main|high"},
                    {"IsRequired", false}}});
  return {
      {"Name", "PlayStation Vita"},
      {"MaxStreamingBitrate", bitrate},
      {"MaxStaticBitrate", bitrate},
      {"MusicStreamingTranscodingBitrate", 192000},
      {"DirectPlayProfiles",
       Json::array({{{"Type", "Video"},
                     {"Container", "mp4,m4v"},
                     {"VideoCodec", "h264"},
                     {"AudioCodec", "aac,mp3"}},
                    {{"Type", "Audio"},
                     {"Container", "mp3,aac,m4a,flac,ogg,opus,wav"}}})},
      {"TranscodingProfiles", Json::array({{{"Type", "Video"},
                                            {"Context", "Streaming"},
                                            {"Protocol", "hls"},
                                            {"Container", "ts"},
                                            {"VideoCodec", "h264"},
                                            {"AudioCodec", "aac"},
                                            {"MaxAudioChannels", "2"},
                                            {"MinSegments", 2},
                                            {"SegmentLength", 3},
                                            {"BreakOnNonKeyFrames", false},
                                            {"CopyTimestamps", false}},
                                           {{"Type", "Audio"},
                                            {"Context", "Streaming"},
                                            {"Protocol", "http"},
                                            {"Container", "mp3"},
                                            {"AudioCodec", "mp3"},
                                            {"MaxAudioChannels", "2"}}})},
      {"CodecProfiles",
       Json::array(
           {{{"Type", "Video"}, {"Codec", "h264"}, {"Conditions", conditions}},
            {{"Type", "VideoAudio"},
             {"Codec", "aac,mp3"},
             {"Conditions", Json::array({{{"Condition", "LessThanEqual"},
                                          {"Property", "AudioChannels"},
                                          {"Value", "2"},
                                          {"IsRequired", true}}})}}})},
      {"SubtitleProfiles",
       Json::array({{{"Format", "srt"}, {"Method", "Encode"}},
                    {{"Format", "ass"}, {"Method", "Encode"}},
                    {{"Format", "ssa"}, {"Method", "Encode"}},
                    {{"Format", "sub"}, {"Method", "Encode"}},
                    {{"Format", "pgssub"}, {"Method", "Encode"}},
                    {{"Format", "dvdsub"}, {"Method", "Encode"}},
                    {{"Format", "vtt"}, {"Method", "Encode"}}})}};
}
std::string Client::authenticatedUrl(const std::string &relative) const {
  if (relative.empty())
    throw std::runtime_error("The server did not provide a stream URL.");
  std::string u;
  if (relative.find("://") != std::string::npos)
    u = relative;
  else {
    if (relative.rfind("//", 0) == 0)
      throw std::runtime_error("Invalid stream URL.");
    // Returned paths may already contain the server's reverse-proxy base path.
    const auto basePath = current.url.substr(origin(current.url).size());
    if (!basePath.empty() && relative.rfind(basePath + "/", 0) == 0)
      u = origin(current.url) + relative;
    else
      u = current.url + (relative[0] == '/' ? "" : "/") + relative;
  }
  if (origin(u) != origin(current.url))
    throw std::runtime_error("The stream points to a different server. "
                             "Refusing to send your access token.");
  if (u.find_first_of("\r\n") != std::string::npos)
    throw std::runtime_error("Invalid stream URL.");
  if (!current.token.empty() && u.find("api_key=") == std::string::npos)
    u += (u.find('?') == std::string::npos ? "?" : "&") +
         std::string("api_key=") + encode(current.token);
  return u;
}
Playback Client::resolve(const Json &item, const PlaybackOptions &options,
                         int64_t start) const {
  const std::string id = stringValue(item, "Id");
  if (id.empty())
    throw std::runtime_error("This item has no identifier.");
  Json body = {{"UserId", current.userId},
               {"DeviceProfile", vitaProfile(options.bitrate, options.height)},
               {"MaxStreamingBitrate", options.bitrate},
               {"StartTimeTicks", 0},
               {"IsPlayback", true},
               {"AutoOpenLiveStream", true},
               {"EnableDirectPlay", !options.forceTranscode &&
                                        options.subtitleIndex < 0 &&
                                        options.audioIndex < 0},
               {"EnableDirectStream", !options.forceTranscode &&
                                          options.subtitleIndex < 0 &&
                                          options.audioIndex < 0},
               {"EnableTranscoding", true},
               {"SubtitleStreamIndex", options.subtitleIndex}};
  if (options.audioIndex >= 0)
    body["AudioStreamIndex"] = options.audioIndex;
  if (!options.sourceId.empty())
    body["MediaSourceId"] = options.sourceId;
  auto info = request("POST", "/Items/" + encode(id) + "/PlaybackInfo",
                      {{"UserId", current.userId}}, body);
  if (!stringValue(info, "ErrorCode").empty())
    throw std::runtime_error("The server cannot play this item: " +
                             stringValue(info, "ErrorCode"));
  auto sources = info.value("MediaSources", Json::array());
  if (!sources.is_array() || sources.empty())
    throw std::runtime_error("No playable media source was returned.");
  Json source = sources.front();
  for (auto &s : sources)
    if (stringValue(s, "Id") == options.sourceId)
      source = s;
  Playback p;
  p.itemId = id;
  p.sourceId = stringValue(source, "Id");
  p.playSessionId = stringValue(info, "PlaySessionId");
  p.liveStreamId = stringValue(source, "LiveStreamId");
  p.startTicks = std::max<int64_t>(0, start);
  p.streams = source.value("MediaStreams", Json::array());
  p.audioIndex = options.audioIndex;
  p.subtitleIndex = options.subtitleIndex;
  const bool canDirect = body["EnableDirectPlay"].get<bool>() &&
                         source.value("SupportsDirectPlay", false);
  if (canDirect && stringValue(item, "Type") != "TvChannel") {
    const bool audio = stringValue(item, "MediaType") == "Audio" ||
                       stringValue(item, "Type") == "Audio";
    p.url = url((audio ? "/Audio/" : "/Videos/") + encode(id) + "/stream",
                {{"Static", "true"},
                 {"MediaSourceId", p.sourceId},
                 {"DeviceId", device}});
    p.method = "DirectPlay";
  } else {
    p.url = stringValue(source, "TranscodingUrl");
    if (p.url.empty())
      throw std::runtime_error("This file needs server transcoding. Enable "
                               "transcoding for your account.");
    p.url = withoutStartTime(p.url);
    p.method = "Transcode";
  }
  p.url = authenticatedUrl(p.url);
  return p;
}
void Client::report(const Playback &p, const std::string &event, double seconds,
                    bool paused) const {
  if (event != "Playing" && event != "Playing/Progress" &&
      event != "Playing/Stopped")
    throw std::runtime_error("Invalid playback event.");
  Json b = {{"ItemId", p.itemId},
            {"MediaSourceId", p.sourceId},
            {"PlaySessionId", p.playSessionId},
            {"PositionTicks",
             static_cast<int64_t>(std::max(0.0, seconds) * ticksPerSecond)},
            {"IsPaused", paused},
            {"IsMuted", false},
            {"CanSeek", true},
            {"PlayMethod", p.method},
            {"RepeatMode", "RepeatNone"}};
  if (p.audioIndex >= 0)
    b["AudioStreamIndex"] = p.audioIndex;
  b["SubtitleStreamIndex"] = p.subtitleIndex;
  request("POST", "/Sessions/" + event, {}, b);
}
void Client::closePlayback(const Playback &p) const {
  if (p.method == "Transcode")
    request("DELETE", "/Videos/ActiveEncodings",
            {{"DeviceId", device}, {"PlaySessionId", p.playSessionId}});
  if (!p.liveStreamId.empty())
    request("POST", "/LiveStreams/Close", {{"LiveStreamId", p.liveStreamId}});
}
std::string Client::imageUrl(const Json &item, int width) const {
  auto tags = item.value("ImageTags", Json::object());
  const auto id = tags.contains("Primary") ? stringValue(item, "Id")
                                           : stringValue(item, "SeriesId");
  if (id.empty())
    return "";
  return authenticatedUrl(url("/Items/" + encode(id) + "/Images/Primary",
                              {{"MaxWidth", std::to_string(width)},
                               {"Quality", "80"},
                               {"Format", "jpg"}}));
}
std::string Client::image(const Json &item, int width) const {
  auto u = imageUrl(item, width);
  if (u.empty())
    return "";
  auto r = http->send("GET", u, {}, "", 1024 * 1024);
  return r.status == 200 ? r.body : "";
}
Json Client::segments(const std::string &id) const {
  return request("GET", "/MediaSegments/" + encode(id));
}

Settings Settings::load(const std::string &path) {
  Settings s;
  std::ifstream in(path);
  if (!in)
    return s;
  Json j;
  in >> j;
  s.deviceId = stringValue(j, "deviceId", s.deviceId);
  // Config is local, but still validate the values used in headers and
  // filenames.
  if (s.deviceId.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
      std::string::npos)
    s.deviceId = newDeviceId();
  if (j.contains("accounts") && j["accounts"].is_array())
    s.accounts = j["accounts"].get<std::vector<Account>>();
  s.active = static_cast<int>(integerValue(j, "active", -1));
  if (s.active < 0 || s.active >= static_cast<int>(s.accounts.size()))
    s.active = -1;
  s.bitrate = std::clamp<int64_t>(integerValue(j, "bitrate", 4000000), 500000,
                                  10000000);
  s.height = std::clamp<int64_t>(integerValue(j, "height", 544), 360, 720);
  s.forceTranscode = j.value("forceTranscode", false);
  return s;
}
void Settings::save(const std::string &path) const {
  const auto temp = path + ".tmp";
  {
    std::ofstream out(temp, std::ios::trunc);
    if (!out)
      throw std::runtime_error("Cannot save settings.");
    out << Json{{"deviceId", deviceId}, {"accounts", accounts},
                {"active", active},     {"bitrate", bitrate},
                {"height", height},     {"forceTranscode", forceTranscode}}
               .dump(2);
    out.flush();
    if (!out)
      throw std::runtime_error("Cannot finish saving settings.");
  }
  if (std::rename(temp.c_str(), path.c_str()) != 0)
    throw std::runtime_error("Cannot replace settings file.");
}
} // namespace moonfin
