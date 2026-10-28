#pragma once
#include "model.h"
#include <objidl.h>
#include <gdiplus.h>
namespace drawscr {
SIZE measure_text(const Mark& mark);
void draw_text_grayscale(Gdiplus::Graphics& target, const Mark& mark, POINT origin);
}
