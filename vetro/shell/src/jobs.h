#pragma once
// Engine work off the UI thread. Commands, previews, opening and saving run one after another on
// one engine thread, in the order they were asked for; their completions come back to the UI
// thread through the window's message queue. The interface keeps animating, panning and zooming
// meanwhile; input that would touch the document waits (see app.cpp, InputAllowed).
//
// A job that finishes within a few milliseconds completes before Run returns, so quick commands
// feel as immediate as a direct call.
#include <windows.h>
#include <functional>
#include <string>

namespace jobs {
void Init(HWND window, UINT message);  // completions are announced to `window` as `message`
void Shutdown();                       // waits for the job in flight, drops the rest

// `work` runs on the engine thread after everything queued before it; `done` then runs on the UI
// thread. `label` names the work for the status capsule while it takes long.
void Run(std::function<void()> work, std::function<void()> done = nullptr, std::wstring label = L"");

bool Busy();             // anything queued or running
std::wstring Label();    // what is running (empty: unnamed)
double BusyForMs();      // how long the current stretch of work has run
void Pump();             // on `message`: run the finished completions
}  // namespace jobs
