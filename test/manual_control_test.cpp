#include "clock/manual_control.h"
#include "clock/display_service.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace clockapp;
struct Port {
  bool busy = false;
  unsigned off = 0;
  size_t write(const uint8_t* data, size_t size) {
    if (busy) return 0;
    if (std::vector<uint8_t>(data, data + size) == std::vector<uint8_t>{0x1f,0x28,0x61,0x40,0}) ++off;
    busy = true; return size;
  }
  bool drained() const { return !busy; }
};
void click(ManualControl& control, uint32_t at, bool message = false) {
  assert(control.update(true, at, message) == Button::Event::none);
  assert(control.update(true, at + 30, message) == Button::Event::none);
  assert(control.update(false, at + 50, message) == Button::Event::none);
  assert(control.update(false, at + 80, message) == Button::Event::shortPress);
}
int main() {
  SleepPreparation preparation;
  using Result = SleepPreparation::Result;
  preparation.begin(100);
  assert(preparation.poll(100, false, true, true) == Result::waiting);
  assert(preparation.poll(129, false, true, true) == Result::waiting);
  assert(preparation.poll(130, false, false, true) == Result::waiting);
  assert(preparation.poll(131, false, true, false) == Result::waiting);
  assert(preparation.poll(132, false, true, true) == Result::ready);
  // A new press or contact bounce during network shutdown cancels the release window.
  assert(preparation.poll(133, true, true, true) == Result::waiting);
  assert(preparation.poll(140, false, true, true) == Result::waiting);
  assert(preparation.poll(169, false, true, true) == Result::waiting);
  assert(preparation.poll(170, false, true, true) == Result::ready);
  preparation.begin(200);
  assert(preparation.poll(15199, false, false, false) == Result::waiting);
  assert(preparation.poll(15200, false, false, false) == Result::timedOut);
  preparation.begin(UINT32_MAX - 10);
  preparation.poll(UINT32_MAX - 10, false, true, true);
  assert(preparation.poll(20, false, true, true) == Result::ready);
  assert(preparation.poll(14989, false, true, true) == Result::timedOut);
  ManualControl control;
  assert(!control.sleeping());
  // Bouncing contacts must not switch power.
  control.update(true, 0); control.update(false, 10); control.update(true, 20);
  control.update(false, 25); control.update(false, 60);
  assert(!control.sleeping());
  click(control, 100); assert(control.sleeping());
  click(control, 200); assert(!control.sleeping()); // cancel closing
  control.sleep();
  control.wake(1000);
  // A wake button held for several seconds must neither close the display nor open setup.
  assert(control.update(true, 1001) == Button::Event::none);
  assert(control.update(true, 5000) == Button::Event::none);
  control.update(false, 5010); control.update(true, 5020); control.update(false, 5030);
  assert(control.update(false, 5060) == Button::Event::none);
  assert(!control.sleeping());
  click(control, 5100); assert(control.sleeping());
  control.wake(6000); control.update(false, 6000); control.update(false, 6030);
  control.update(true, 6100); control.update(true, 6130);
  assert(control.update(true, 9130) == Button::Event::longPress);
  assert(!control.sleeping()); // long press belongs to setup, never power-off
  control.update(false, 9140);
  assert(control.update(false, 9170) == Button::Event::none);
  ManualControl wrapped;
  wrapped.sleep(); wrapped.wake(UINT32_MAX - 10);
  wrapped.update(true, UINT32_MAX - 5); wrapped.update(false, 10); wrapped.update(false, 40);
  click(wrapped, 100); assert(wrapped.sleeping());

  ManualControl confirming;
  click(confirming,100,true);
  assert(confirming.confirmationPress() && !confirming.sleeping());
  // A message disappearing during the press must not turn confirmation into sleep.
  confirming.update(true,200,true); confirming.update(true,230,true);
  confirming.update(false,250,false);
  assert(confirming.update(false,280,false)==Button::Event::shortPress);
  assert(confirming.confirmationPress() && !confirming.sleeping());
  click(confirming,300);
  assert(!confirming.confirmationPress() && confirming.sleeping());
  confirming.wake(1000);
  confirming.update(true,1001,true); confirming.update(true,5000,true);
  confirming.update(false,5010,true); confirming.update(false,5040,true);
  assert(!confirming.sleeping()); // wake press never confirms the message
  click(confirming,5100,true); assert(confirming.confirmationPress() && !confirming.sleeping());

  // MCU sleep eligibility follows the physical display OFF drain, not just a click.
  Port port; DisplayService<Port> screen(port, Motion::off, 0);
  CellFrame frame; frame.setAscii(0, "12:34:56"); screen.page(frame, 1, 0);
  screen.begin(2); screen.tick(0); port.busy = false; screen.tick(1);
  screen.sleep(2); assert(!screen.asleepAndDrained());
  port.busy = false; screen.tick(2); // blank frame queued
  assert(!screen.asleepAndDrained() && !port.off);
  port.busy = false; screen.tick(3); // power-off queued
  assert(!screen.asleepAndDrained() && port.off == 1);
  port.busy = false; assert(screen.asleepAndDrained());
  screen.wake(4); screen.tick(4); port.busy = false; screen.tick(5);
  assert(screen.state() == DisplayService<Port>::State::awake);
  std::cout << "PASS: manual power toggle, bounce, wake hold/release suppression, setup hold, rollover, display OFF drain before MCU sleep\n";
}
