#include "clock/auto_power.h"
#include "clock/manual_control.h"
#include "clock/display_service.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace clockapp;
struct PowerPort {
  bool busy=false; uint8_t brightness=0; unsigned off=0;
  size_t write(const uint8_t* data,size_t size) {
    if (busy) return 0;
    for (size_t i=0;i+2<size;++i) if (data[i]==0x1f && data[i+1]==0x58) brightness=data[i+2];
    if (std::vector<uint8_t>(data,data+size)==std::vector<uint8_t>{0x1f,0x28,0x61,0x40,0}) ++off;
    busy=true; return size;
  }
  bool drained() const { return !busy; }
};
int main() {
  AutoPower power; power.restart(500);
  for (uint8_t normal : {1,2,3,4}) {
    assert(power.brightness(500,normal)==4);
    assert(power.brightness(60499,normal)==4);
    assert(power.brightness(60500,normal)==(normal<4?normal:3));
  }
  assert(!power.dimmed(60499) && power.dimmed(60500));
  assert(!power.sleepDue(1800499) && power.sleepDue(1800500));
  assert(power.awakeSeconds(60500)==60);
  // Routine polling never changes the session deadline.
  for (uint32_t now=500;now<1800500;now+=1000) assert(!power.sleepDue(now));
  power.restart(1900000);
  assert(power.brightness(1900000,2)==4 && !power.sleepDue(1900000));
  assert(power.brightness(1960000,2)==2 && power.sleepDue(3700000));
  power.restart(UINT32_MAX-1000);
  assert(power.brightness(58998,2)==4 && power.brightness(58999,2)==2);
  assert(!power.sleepDue(1798998) && power.sleepDue(1798999));

  // Exercise actual VFD brightness commands and the power-off drain at the deadline.
  PowerPort port; DisplayService<PowerPort> screen(port,Motion::off,0);
  ManualControl manual; SleepPreparation preparation; power.restart(0);
  CellFrame frame; frame.setAscii(0,"12:34:56"); screen.page(frame,3,0);
  screen.begin(power.brightness(0,2)); screen.tick(0); assert(port.brightness==4);
  port.busy=false; screen.tick(1); port.busy=false;
  screen.brightness(power.brightness(59999,2)); screen.tick(59999); assert(port.brightness==4);
  port.busy=false; screen.brightness(power.brightness(60000,2)); screen.tick(60000); assert(port.brightness==2);
  assert(power.sleepDue(1800000)); manual.sleep(); preparation.begin(1800000); screen.sleep(1800000);
  port.busy=false; screen.tick(1800000); // blank
  port.busy=false; screen.tick(1800001); // OFF
  assert(!screen.asleepAndDrained()); port.busy=false; assert(screen.asleepAndDrained() && port.off==1);
  preparation.poll(1800001,false,true,true);
  assert(preparation.poll(1800031,false,true,true)==SleepPreparation::Result::ready);
  // A wake starts a new high-brightness period and consumes the waking press.
  manual.wake(4000000); power.restart(4000000); screen.brightness(power.brightness(4000000,2)); screen.wake(4000000);
  screen.tick(4000000); assert(port.brightness==4);
  port.busy=false; screen.tick(4000001);
  assert(screen.state()==DisplayService<PowerPort>::State::awake && !power.sleepDue(4000001));
  assert(manual.update(true,4000010)==Button::Event::none && !manual.sleeping());
  std::cout << "PASS: startup/wake high brightness, exact 60s dim and 30min sleep boundaries, legacy brightness, timer rollover, VFD commands and OFF drain\n";
}
