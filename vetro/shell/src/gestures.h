#pragma once
// Pure input rules, shared by the canvas tools and their regression tests.
#include <algorithm>
#include <cmath>
#include <utility>

namespace gestures {
inline std::pair<double, double> Snap45(double x, double y) {
  if (x == 0 && y == 0) return {0, 0};
  constexpr double step = 0.7853981633974483;
  double a = std::round(std::atan2(y, x) / step) * step;
  double length = std::hypot(x, y);
  return {std::cos(a) * length, std::sin(a) * length};
}

struct PercentKeys {
  int first = -1;
  bool flow = false;
  double at = 0;
  void Reset() { first = -1; }
  int Push(int digit, bool isFlow, double now) {
    int value;
    if (first >= 0 && flow == isFlow && now - at <= 700 && now >= at) {
      value = first * 10 + digit;
      first = -1;
      if (value == 0) value = 100;
    } else {
      first = digit;
      value = digit == 0 ? 100 : digit * 10;
    }
    flow = isFlow;
    at = now;
    return value;
  }
};

struct Marquee {
  double ax = 0, ay = 0, px = 0, py = 0;
  double left = 0, top = 0, right = 0, bottom = 0;
  bool center = false;
  void Begin(double x, double y) {
    ax = px = left = right = x;
    ay = py = top = bottom = y;
    center = false;
  }
  void Move(double x, double y, bool square, bool fromCenter, bool reposition) {
    if (reposition) { ax += x - px; ay += y - py; }
    px = x; py = y;
    double dx = x - ax, dy = y - ay;
    if (square) {
      double side = std::max(std::fabs(dx), std::fabs(dy));
      dx = std::copysign(side, dx); dy = std::copysign(side, dy);
    }
    center = fromCenter;
    left = fromCenter ? ax - std::fabs(dx) : std::min(ax, ax + dx);
    right = fromCenter ? ax + std::fabs(dx) : std::max(ax, ax + dx);
    top = fromCenter ? ay - std::fabs(dy) : std::min(ay, ay + dy);
    bottom = fromCenter ? ay + std::fabs(dy) : std::max(ay, ay + dy);
  }
};
}  // namespace gestures
