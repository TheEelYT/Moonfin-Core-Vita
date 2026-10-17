// SPDX-License-Identifier: GPL-3.0-or-later
#include "moonfin/runtime.hpp"
#include <algorithm>
namespace moonfin {
Jobs *jobs = nullptr;
Jobs::Jobs()
    : worker([this] {
        for (;;) {
          std::function<void()> f;
          {
            std::unique_lock<std::mutex> lock(mutex);
            ready.wait(lock, [this] { return stopping || !pending.empty(); });
            if (stopping && pending.empty())
              return;
            f = std::move(pending.front().fn);
            pending.pop_front();
          }
          try {
            f();
          } catch (...) { /* Each UI/API task handles its own errors. */
          }
        }
      }) {}
Jobs::~Jobs() { shutdown(); }
bool Jobs::submit(std::function<void()> f, bool priority) {
  std::lock_guard<std::mutex> lock(mutex);
  if (stopping || pending.size() >= 96)
    return false;
  if (priority)
    pending.insert(std::find_if(pending.begin(), pending.end(),
                                [](const Job &j) { return !j.priority; }),
                   Job{std::move(f), true});
  else
    pending.push_back({std::move(f), false});
  ready.notify_one();
  return true;
}
void Jobs::complete(std::function<void()> f) {
  std::lock_guard<std::mutex> lock(mutex);
  completed.emplace_back(std::move(f));
}
void Jobs::drain() {
  std::deque<std::function<void()>> list;
  {
    std::lock_guard<std::mutex> lock(mutex);
    list.swap(completed);
  }
  for (auto &f : list)
    f();
}
void Jobs::shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex);
    stopping = true;
    pending.erase(std::remove_if(pending.begin(), pending.end(),
                                 [](const Job &j) { return !j.priority; }),
                  pending.end());
  }
  ready.notify_one();
  if (worker.joinable())
    worker.join();
}
} // namespace moonfin
