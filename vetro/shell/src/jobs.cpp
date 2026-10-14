// PhotoCraft by Vetro -- the engine thread (see jobs.h).
#include "jobs.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace jobs {
namespace {
using Clock = std::chrono::steady_clock;

struct Job {
  std::function<void()> work, done;
  std::wstring label;
};

HWND window = nullptr;
UINT message = 0;
std::thread worker;
std::mutex m;
std::condition_variable wake, idle;
std::deque<Job> queue;
std::vector<std::function<void()>> finished;  // completions not yet run, in order
bool running = false, quit = false, pumping = false;
std::wstring current;
int pending = 0;  // queued + running + finished but not pumped
Clock::time_point since;
std::function<void()> onIdle;

void Loop() {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(m);
      wake.wait(lock, [] { return quit || !queue.empty(); });
      if (quit) return;
      job = std::move(queue.front());
      queue.pop_front();
      running = true;
      current = job.label;
    }
    try {
      if (job.work) job.work();
    } catch (...) {
      // The bridge never throws; a shell bug must not take the engine thread down with it.
    }
    {
      std::lock_guard<std::mutex> lock(m);
      running = false;
      current.clear();
      finished.push_back(std::move(job.done));
    }
    idle.notify_all();
    PostMessage(window, message, 0, 0);
  }
}
}  // namespace

void Init(HWND w, UINT msg) {
  window = w;
  message = msg;
  worker = std::thread(Loop);
}

void Shutdown() {
  {
    std::lock_guard<std::mutex> lock(m);
    quit = true;
    queue.clear();
  }
  wake.notify_all();
  if (worker.joinable()) worker.join();
}

void Run(std::function<void()> work, std::function<void()> done, std::wstring label) {
  bool wait = false;
  {
    std::lock_guard<std::mutex> lock(m);
    if (pending == 0) since = Clock::now();
    pending++;
    queue.push_back({std::move(work), std::move(done), std::move(label)});
    wait = !pumping;
  }
  wake.notify_one();
  if (!wait) return;
  // Quick work (most commands) completes here, so the next frame already shows it.
  {
    std::unique_lock<std::mutex> lock(m);
    idle.wait_for(lock, std::chrono::milliseconds(30), [] { return queue.empty() && !running; });
  }
  Pump();
}

bool Busy() {
  std::lock_guard<std::mutex> lock(m);
  return pending > 0;
}

std::wstring Label() {
  std::lock_guard<std::mutex> lock(m);
  return current;
}

double BusyForMs() {
  std::lock_guard<std::mutex> lock(m);
  if (pending == 0) return 0;
  return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}

void Pump() {
  if (pumping) return;
  pumping = true;
  for (;;) {
    std::vector<std::function<void()>> ready;
    {
      std::lock_guard<std::mutex> lock(m);
      ready.swap(finished);
    }
    if (ready.empty()) break;
    for (auto& done : ready) {
      {
        std::lock_guard<std::mutex> lock(m);
        pending--;
      }
      if (done) done();
    }
  }
  if (!Busy() && onIdle) onIdle();
  pumping = false;
}
void SetIdleCallback(std::function<void()> callback) { onIdle = std::move(callback); }
}  // namespace jobs
