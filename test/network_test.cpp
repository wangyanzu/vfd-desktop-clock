#include "clock/network_policy.h"
#include <cassert>
#include <iostream>
using namespace clockapp;
int main() {
  NetworkPolicy fresh; fresh.configure(0, false);
  assert(fresh.tick(0, false).openSetup);
  assert(!fresh.tick(100, false).openSetup);
  assert(fresh.tick(600000, false).closeSetup);
  assert(!fresh.tick(600001, false).openSetup);
  fresh.requestSetup(); assert(fresh.tick(600002, false).openSetup);
  fresh.tick(600003, true);
  assert(!fresh.tick(660002, true).closeSetup);
  assert(fresh.tick(660003, true).closeSetup);
  NetworkPolicy lost; lost.configure(0, true);
  assert(!lost.tick(0, false).openSetup);
  assert(lost.tick(10000, false).retry);
  assert(!lost.tick(29999, false).retry);
  const auto fallback = lost.tick(30000, false);
  assert(fallback.retry && fallback.openSetup);
  lost.tick(31000, true);
  lost.tick(32000, false);
  assert(!lost.tick(91000, false).closeSetup); // grace period requires continuous connection
  lost.tick(100000, true);
  assert(lost.tick(160000, true).closeSetup);
  assert(!lost.tick(200000, false).openSetup); // do not repeatedly advertise a setup hotspot
  NetworkPolicy backoff; backoff.configure(0, true);
  assert(backoff.tick(10000, false).retry);
  assert(backoff.tick(30000, false).retry);
  assert(!backoff.tick(69999, false).retry);
  assert(backoff.tick(70000, false).retry);
  assert(backoff.tick(130000, false).retry);
  NetworkPolicy wrap; const auto start = UINT32_MAX - 100;
  wrap.configure(start, true);
  assert(wrap.tick(start + 30000, false).openSetup);
  wrap.setupFailed(); assert(!wrap.tick(start + 40000, false).openSetup);
  wrap.requestSetup(); assert(wrap.tick(start + 40001, false).openSetup);
  // Updating credentials preserves the recovery AP while restarting its window.
  wrap.configure(start + 50000, true);
  assert(wrap.setupActive() && !wrap.tick(start + 50001, false).closeSetup);
  std::cout << "PASS: reconnect backoff, setup fallback/grace/expiry, explicit recovery, tick rollover\n";
}
