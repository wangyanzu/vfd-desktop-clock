#pragma once
namespace clockapp {
// Open-drain bus clear: never drive a line HIGH. Bounded clock-stretch waits
// belong to Lines::waitSclHigh(); release the pins even on failure.
template <class Lines> bool clearI2cBus(Lines& lines) {
  lines.releaseSda(); lines.releaseScl();
  if (!lines.waitSclHigh()) return false;
  for (unsigned pulse=0;pulse<9 && !lines.sdaHigh();++pulse) {
    lines.driveSclLow(); lines.pause(); lines.releaseScl();
    if (!lines.waitSclHigh()) return false;
    lines.pause();
  }
  // Generate STOP only after SCL was pulled low, avoiding an accidental START.
  lines.driveSclLow(); lines.driveSdaLow(); lines.pause(); lines.releaseScl();
  const bool clockHigh=lines.waitSclHigh();
  lines.releaseSda(); lines.pause();
  return clockHigh && lines.sdaHigh();
}
}
