#include "clock/aht10.h"
#include "clock/i2c_recovery.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace clockapp;
struct SensorBus {
  uint32_t now=0,triggeredAt=0;
  unsigned operations=0, conversions=0, resetCommands=0, initCommands=0, recoverCalls=0;
  unsigned readFailures=0,frameFailures=0,triggerFailures=0,emptyConversions=0;
  bool present=true,stuck=false,recoveryWorks=true,calibrated=true,needsCalibration=false,neverCalibrated=false,initNack=false;
  bool emptyAlways=false,busyAlways=false,badRange=false;
  void operation() { assert(++operations<=1); }
  bool write(const uint8_t* data,size_t size) {
    operation();
    if (!present || stuck) return false;
    if (size==1 && data[0]==0xba) { ++resetCommands; calibrated=!needsCalibration; return true; }
    if (size==3 && data[0]==0xe1) { ++initCommands; calibrated=!neverCalibrated; return !initNack; }
    assert(size==3 && data[0]==0xac && data[1]==0x33 && data[2]==0);
    if (triggerFailures) { --triggerFailures; return false; }
    ++conversions; triggeredAt=now; return true;
  }
  bool read(uint8_t* data,size_t size) {
    operation(); assert(size==1 || size==6);
    if (!present || stuck) return false;
    if (readFailures) { --readFailures; return false; }
    if (size==6 && frameFailures) { --frameFailures; return false; }
    data[0]=uint8_t(0x10 | (calibrated?8:0) | (busyAlways?0x80:0));
    if (size==1) return true;
    for (size_t i=1;i<size;++i) data[i]=0;
    if (!emptyAlways && conversions>emptyConversions) {
      assert(elapsed(now,triggeredAt,120));
      data[1]=0x80; data[3]=0x06; // 50% RH, 25C
      if (badRange) data[3]=0x0f; // >85C, never accepted
    }
    return true;
  }
  bool recover() { operation(); ++recoverCalls; if (recoveryWorks) stuck=false; return recoveryWorks; }
};
void tick(Aht10<SensorBus>& sensor,SensorBus& bus,uint32_t now) { bus.now=now; bus.operations=0; sensor.tick(now); }
void run(Aht10<SensorBus>& sensor,SensorBus& bus,uint32_t from,uint32_t to) {
  for (uint32_t at=from;at<=to;++at) tick(sensor,bus,at);
}
struct Lines {
  bool sdaReleased=false,sclReleased=true,clockHeld=false;
  unsigned raised=0,releaseAfter=0,waits=0;
  void releaseSda() { sdaReleased=true; }
  void releaseScl() { if (!sclReleased) ++raised; sclReleased=true; }
  void driveSdaLow() { sdaReleased=false; }
  void driveSclLow() { sclReleased=false; }
  bool waitSclHigh() { ++waits; return !clockHeld; }
  bool sdaHigh() { return sdaReleased && raised>=releaseAfter; }
  void pause() {}
};
int main() {
  SensorBus bus; Aht10<SensorBus> sensor(bus); sensor.begin(0);
  run(sensor,bus,0,1000);
  assert(sensor.environment().fresh(1000) && sensor.environment().temperature==25 && sensor.environment().humidity==50);
  assert(bus.resetCommands==1 && bus.initCommands==0); // calibrated sensors skip repeated factory-load commands
  assert(sensor.diagnostics().samples==1 && sensor.hasReading() && sensor.lastReading()[0]==0x18);
  // Short reads and NACKs recover inside a measurement cycle, without a 10s outage.
  bus.frameFailures=1; run(sensor,bus,1001,7000);
  assert(sensor.environment().fresh(7000) && sensor.diagnostics().failures==0 && sensor.diagnostics().ioErrors==1);
  bus.triggerFailures=1; run(sensor,bus,7001,13000);
  assert(sensor.environment().fresh(13000) && sensor.diagnostics().failures==0);
  const auto count=sensor.diagnostics().samples;
  sensor.resume(14000); bus.readFailures=2;
  assert(sensor.environment().valid && !sensor.environment().fresh(14000));
  run(sensor,bus,14000,15000);
  assert(sensor.environment().fresh(15000) && sensor.diagnostics().samples>count);
  // Confirmed invalid reads keep the last actual numbers, but mark them unhealthy.
  bus.badRange=true; run(sensor,bus,15001,22000);
  assert(sensor.environment().valid && sensor.environment().temperature==25 && !sensor.environment().healthy);
  assert(sensor.errorCode()==Aht10<SensorBus>::Error::range);
  bus.badRange=false; run(sensor,bus,22001,30000); assert(sensor.environment().fresh(30000));

  SensorBus calibration; calibration.needsCalibration=true;
  Aht10<SensorBus> calibrated(calibration); calibrated.begin(0); run(calibrated,calibration,0,1000);
  assert(calibrated.environment().fresh(1000) && calibration.initCommands==1);
  SensorBus compatible; compatible.needsCalibration=true; compatible.initNack=true;
  Aht10<SensorBus> initAcknowledgement(compatible); initAcknowledgement.begin(0); run(initAcknowledgement,compatible,0,1000);
  assert(initAcknowledgement.environment().fresh(1000) && initAcknowledgement.diagnostics().ioErrors==1);
  SensorBus invalid; invalid.needsCalibration=true; invalid.neverCalibrated=true;
  Aht10<SensorBus> noCalibration(invalid); noCalibration.begin(0); run(noCalibration,invalid,0,1600);
  assert(!noCalibration.environment().valid && noCalibration.errorCode()==Aht10<SensorBus>::Error::calibration);

  SensorBus zero; zero.emptyConversions=2;
  Aht10<SensorBus> firstConversion(zero); firstConversion.begin(0); run(firstConversion,zero,0,2500);
  assert(firstConversion.environment().fresh(2500) && zero.conversions==3 && firstConversion.diagnostics().failures==0);
  SensorBus empty; empty.emptyAlways=true;
  Aht10<SensorBus> bounded(empty); bounded.begin(0); run(bounded,empty,0,25000);
  assert(!bounded.environment().valid && bounded.errorCode()==Aht10<SensorBus>::Error::empty);
  assert(bounded.diagnostics().recoveries>0 && bounded.diagnostics().emptyFrames>0);
  empty.emptyAlways=false; run(bounded,empty,25001,60000);
  assert(bounded.environment().fresh(60000) && bounded.diagnostics().consecutiveFailures==0);

  SensorBus stuck; stuck.stuck=true;
  Aht10<SensorBus> recovered(stuck); recovered.begin(0); run(recovered,stuck,0,20000);
  assert(stuck.recoverCalls==1 && recovered.environment().fresh(20000));
  SensorBus absent; absent.present=false;
  Aht10<SensorBus> unplugged(absent); unplugged.begin(0); run(unplugged,absent,0,100000);
  assert(!unplugged.environment().valid && unplugged.diagnostics().recoveries>0 && unplugged.retryDelayMs()==30000);
  absent.present=true; run(unplugged,absent,100001,140000); assert(unplugged.environment().fresh(140000));
  SensorBus busy; busy.busyAlways=true;
  Aht10<SensorBus> neverReady(busy); neverReady.begin(0); run(neverReady,busy,0,4000);
  assert(!neverReady.environment().valid && neverReady.errorCode()==Aht10<SensorBus>::Error::busy);
  SensorBus converting; Aht10<SensorBus> busyConversion(converting); busyConversion.begin(0); run(busyConversion,converting,0,1000);
  converting.busyAlways=true; run(busyConversion,converting,1001,9000);
  assert(busyConversion.environment().valid && !busyConversion.environment().healthy);
  assert(busyConversion.errorCode()==Aht10<SensorBus>::Error::busy);
  converting.busyAlways=false; run(busyConversion,converting,9001,16000);
  assert(busyConversion.environment().fresh(16000));

  SensorBus late; Aht10<SensorBus> delayed(late); delayed.begin(0); run(delayed,late,0,200);
  tick(delayed,late,2000); tick(delayed,late,2001); assert(delayed.environment().fresh(2001));
  SensorBus rollover; Aht10<SensorBus> wrapped(rollover);
  const uint32_t start=UINT32_MAX-200;
  wrapped.begin(start);
  for (uint32_t age=0;age<1000;++age) tick(wrapped,rollover,start+age);
  assert(wrapped.environment().fresh(start+999));

  Lines idle; assert(clearI2cBus(idle) && idle.sdaReleased && idle.sclReleased);
  Lines released; released.releaseAfter=9;
  assert(clearI2cBus(released) && released.raised<=10 && released.waits<=11);
  Lines permanent; permanent.releaseAfter=100;
  assert(!clearI2cBus(permanent) && permanent.raised<=11 && permanent.sdaReleased && permanent.sclReleased);
  Lines held; held.clockHeld=true;
  assert(!clearI2cBus(held) && held.waits==1 && held.sdaReleased && held.sclReleased);
  std::cout << "PASS: AHT10 cold/wake initialization, short-read/NACK retries, bounded empty/busy/range faults, staged reset/bus recovery, unplug/replug, rollover and open-drain bus clear\n";
}
