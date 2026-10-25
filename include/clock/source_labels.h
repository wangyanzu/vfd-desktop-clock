#pragma once
#include "clock/messages.h"
#include "clock/rotation.h"
namespace clockapp {
// Source labels consume complete character cells, including Chinese glyphs.
inline CellFrame labelSources(const Frame& frame, ContentPage page) {
  CellFrame result(frame);
  std::string text(frame.lines[1].data(), 20);
  while (!text.empty() && text.back() == ' ') text.pop_back();
  if (page == ContentPage::outdoor) {
    if (text.find("OUT:") == 0) text.replace(0, 3, "室外");
    else text = "室外 接口未获取";
  } else if (page == ContentPage::comparison) {
    const auto outside = text.find("OUT:");
    if (outside != std::string::npos) text.replace(outside, 3, "室外");
    if (text.find("IN:") == 0) text.replace(0, 2, "室内");
  } else {
    text = "室内 " + text;
  }
  DisplayText encoded;
  if (encodeMessage(text, encoded)) {
    result.rows[1].fill(Cell{});
    const auto count = encoded.length > 20 ? 20 : encoded.length;
    for (int i = 0; i < count; ++i) result.rows[1][i] = encoded.cells[i];
    if (frame.lines[1][19] == '*') result.rows[1][19] = Cell::ascii('*');
  }
  return result;
}
}
