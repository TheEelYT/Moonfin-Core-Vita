// SPDX-License-Identifier: GPL-3.0-or-later
#include "moonfin/client.hpp"
#include <curl/curl.h>
#include <mutex>
#include <stdexcept>

namespace moonfin {
namespace {
struct Buffer {
  std::string data;
  size_t limit;
};
size_t receive(char *p, size_t size, size_t count, void *user) {
  auto &b = *static_cast<Buffer *>(user);
  if (size && count > SIZE_MAX / size)
    return 0;
  auto n = size * count;
  if (n > b.limit - b.data.size())
    return 0;
  try {
    b.data.append(p, n);
  } catch (...) {
    return 0;
  }
  return n;
}
struct Easy {
  CURL *p = curl_easy_init();
  ~Easy() {
    if (p)
      curl_easy_cleanup(p);
  }
};
struct Headers {
  curl_slist *p = nullptr;
  ~Headers() { curl_slist_free_all(p); }
};
} // namespace
Response CurlTransport::send(const std::string &method, const std::string &url,
                             const Params &headers, const std::string &body,
                             size_t limit) {
  static std::once_flag init;
  std::call_once(init, [] {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0)
      throw std::runtime_error("Cannot initialize networking.");
  });
  Easy e;
  if (!e.p)
    throw std::runtime_error("Out of memory while opening a connection.");
  Headers list;
  for (auto &kv : headers)
    list.p = curl_slist_append(list.p, (kv.first + ": " + kv.second).c_str());
  Buffer b{{}, limit};
  curl_easy_setopt(e.p, CURLOPT_URL, url.c_str());
  curl_easy_setopt(e.p, CURLOPT_HTTPHEADER, list.p);
  curl_easy_setopt(e.p, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(e.p, CURLOPT_CONNECTTIMEOUT, 8L);
  curl_easy_setopt(e.p, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(e.p, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(e.p, CURLOPT_WRITEFUNCTION, receive);
  curl_easy_setopt(e.p, CURLOPT_WRITEDATA, &b);
  const auto userAgent = std::string("Moonfin-Vita/") + version;
  curl_easy_setopt(e.p, CURLOPT_USERAGENT, userAgent.c_str());
  curl_easy_setopt(e.p, CURLOPT_ACCEPT_ENCODING, "");
  // API redirects are not followed with credentials. Use the final server URL
  // at setup.
  curl_easy_setopt(e.p, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(e.p, CURLOPT_PROTOCOLS_STR, "http,https");
  if (!caFile.empty())
    curl_easy_setopt(e.p, CURLOPT_CAINFO, caFile.c_str());
  if (!body.empty()) {
    curl_easy_setopt(e.p, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(e.p, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(body.size()));
  }
  auto code = curl_easy_perform(e.p);
  if (code != CURLE_OK)
    throw std::runtime_error(
        code == CURLE_WRITE_ERROR
            ? "The server response exceeded the Vita memory limit."
            : "Connection failed: " + std::string(curl_easy_strerror(code)) +
                  ". Check Wi-Fi, server URL and certificates.");
  long status = 0;
  curl_easy_getinfo(e.p, CURLINFO_RESPONSE_CODE, &status);
  return {status, std::move(b.data)};
}
} // namespace moonfin
