// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "client.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace moonfin {
// One bounded network worker. UI and all GXM calls stay on the main thread.
class Jobs {
public:
  Jobs();
  ~Jobs();
  bool submit(std::function<void()> task, bool priority = true);
  void complete(std::function<void()> task);
  void drain();
  void shutdown();

private:
  std::mutex mutex;
  std::condition_variable ready;
  struct Job {
    std::function<void()> fn;
    bool priority;
  };
  std::deque<Job> pending;
  std::deque<std::function<void()>> completed;
  bool stopping = false;
  std::thread worker;
};
extern Jobs *jobs;
} // namespace moonfin
