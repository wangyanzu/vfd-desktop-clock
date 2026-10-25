#pragma once
#include "clock/rotation.h"
namespace clockapp {
inline CellFrame labelSources(const Frame& frame, ContentPage page) {
  CellFrame result(frame);
  std::string text(frame.lines[1].data(), 20);
  while (!text.empty() && text.back() == ' ') text.pop_back();
  if (page == ContentPage::outdoor) {
    if (text.find("OUT:") != 0) text = "OUT:--.-C";
  } else if (page != ContentPage::comparison) {
    if (text.find("T:") == 0) text.erase(0, 2);
    else if (text.find("TEMP: ") == 0) text.erase(0, 6);
    else if (text.find("TEMP:") == 0) text.erase(0, 5);
    else if (text.find("HUMIDITY: ") == 0) text.replace(0, 10, "RH:");
    else if (text.find("HUMIDITY:") == 0) text.replace(0, 9, "RH:");
    else text = "Sensor offline";
    text = "IN:" + text;
  }
  result.setAscii(1, text.c_str());
  if (frame.lines[1][19] == '*') result.rows[1][19] = Cell::ascii('*');
  return result;
}
}
