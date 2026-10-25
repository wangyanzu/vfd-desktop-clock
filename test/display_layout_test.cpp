#include "clock/frame.h"
#include "clock/core.h"
#include "clock/display_service.h"
#include "clock/startup_ip.h"
#include "clock/source_labels.h"
#include "clock/burn_in_shift.h"
#include <cassert>
#include <iostream>
using namespace clockapp;
struct Port {
  bool drained() const { return true; }
  size_t write(const uint8_t*,size_t length) { return length; }
};
std::string text(const CellFrame& frame,size_t row) {
  std::string result; for (const auto& cell : frame.rows[row]) result+=char(cell.first()); return result;
}
int main() {
  CellFrame frame; frame.setAscii(0,"12:34:56"); frame.setAscii(1,"192.168.1.40");
  const auto center=centeredFrame(frame);
  assert(text(center,0)=="      12:34:56      ");
  assert(text(center,1)=="    192.168.1.40    ");
  assert(centeredFrame(center)==center);
  for (int offset=-2;offset<=2;++offset) {
    const auto shifted=centeredFrame(frame,offset);
    assert(text(shifted,0).find("12:34:56")==size_t(6+offset));
    assert(text(shifted,1).find("192.168.1.40")==size_t(4+offset));
  }
  CellFrame full; full.setAscii(0,"ABCDEFGHIJKLMNOPQRST");
  assert(centeredFrame(full,-2)==full && centeredFrame(full,2)==full);
  CellFrame chinese; assert(chinese.setEncoded(1,"\xd6\xd0\xce\xc4"));
  const auto centeredChinese=centeredFrame(chinese,2);
  assert(centeredChinese.rows[1][11]==chinese.rows[1][0] && centeredChinese.rows[1][12]==chinese.rows[1][1]);
  BurnInShift shift;
  const int8_t expected[]={0,1,2,1,0,-1,-2,-1,0};
  for (unsigned i=0;i<9;++i) assert(shift.offset(i*300000u)==expected[i]);
  assert(shift.offset(2400001)==0);
  BurnInShift wrapped;
  const int before=wrapped.offset(UINT32_MAX-1000);
  const int after=wrapped.offset(500);
  assert(before==after && after>=-2 && after<=2);

  StartupIp ip;
  assert(!ip.update(0,false,true,false,"0.0.0.0"));
  assert(!ip.update(10,true,true,false,"0.0.0.0"));
  assert(!ip.update(100,true,false,false,"192.168.1.40")); // wait for boot scene
  assert(ip.update(1000,true,true,false,"192.168.1.40"));
  assert(ip.update(5000,true,true,false,"192.168.1.40")); // entry not settled: no reading time spent
  assert(ip.update(6000,true,true,true,"192.168.1.40"));
  assert(ip.update(9999,true,true,true,"192.168.1.40"));
  assert(!ip.update(10000,true,true,true,"192.168.1.40"));
  assert(!ip.update(11000,false,true,false,"0.0.0.0"));
  assert(!ip.update(12000,true,true,false,"192.168.1.41")); // first startup only
  StartupIp paused;
  assert(paused.update(0,true,true,true,"192.168.1.40"));
  assert(!paused.update(1000,true,false,false,"192.168.1.40"));
  assert(paused.update(9000,true,true,true,"192.168.1.40"));
  assert(paused.update(12999,true,true,true,"192.168.1.40"));
  assert(!paused.update(13000,true,true,true,"192.168.1.40"));
  StartupIp rollover;
  assert(rollover.update(UINT32_MAX-100,true,true,true,"192.168.1.40"));
  assert(rollover.update(3898,true,true,true,"192.168.1.40"));
  assert(!rollover.update(3899,true,true,true,"192.168.1.40"));

  Frame indoor; indoor.set(1,"T:24.6C RH:51.2%");
  const auto labelled = labelSources(indoor,ContentPage::indoor);
  DisplayText label; assert(encodeMessage("室内",label));
  assert(labelled.rows[1][0] == label.cells[0] && labelled.rows[1][1] == label.cells[1]);
  assert(labelled.rows[1][3] == Cell::ascii('T'));
  Frame comparison; comparison.set(1,"IN:24.6 OUT:18.0C");
  const auto both = labelSources(comparison,ContentPage::comparison);
  assert(encodeMessage("室内:24.6 室外:18.0C",label));
  for (unsigned i=0;i<label.length;++i) assert(both.rows[1][i] == label.cells[i]);
  Frame weather; weather.set(1,"OUT:18.0C CLOUDY");
  const auto outside = labelSources(weather,ContentPage::outdoor);
  assert(encodeMessage("室外:18.0C CLOUDY",label));
  for (unsigned i=0;i<label.length;++i) assert(outside.rows[1][i] == label.cells[i]);

  Port port; DisplayService<Port> screen(port,Motion::off,0,true);
  screen.page(frame,3,0); screen.begin(2); screen.tick(0); screen.tick(1);
  assert(screen.visible()==center);
  frame.setAscii(0,"12:34:57"); screen.page(frame,3,100); screen.tick(101);
  assert(screen.visible()==centeredFrame(frame)); // direct real time, no intermediate digits
  screen.page(frame,3,300000,false,2,1); screen.tick(300000);
  assert(screen.visible()==centeredFrame(frame,1));
  std::cout << "PASS: centred ASCII/Chinese, bounded +/-2-cell burn-in shift, static time, initial IP reading duration, sleep/disconnect/rollover\n";
}
