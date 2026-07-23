// Shared drawing helpers that are too big to sit inline in mode.h.
#include "mode.h"

void HueToRGB(float h, uint8_t *r, uint8_t *g, uint8_t *b) {
  h -= floorf(h);
  const float x = h * 6.0f;
  const int i = (int)x;
  const float f = x - i;
  const uint8_t v = 255;
  const uint8_t p = 0;
  const uint8_t q = (uint8_t)(255 * (1.0f - f));
  const uint8_t t = (uint8_t)(255 * f);
  switch (i) {
  case 0:  *r = v; *g = t; *b = p; break;
  case 1:  *r = q; *g = v; *b = p; break;
  case 2:  *r = p; *g = v; *b = t; break;
  case 3:  *r = p; *g = q; *b = v; break;
  case 4:  *r = t; *g = p; *b = v; break;
  default: *r = v; *g = p; *b = q; break;
  }
}

void DrawLine(Canvas *c, int x0, int y0, int x1, int y1,
                     uint8_t r, uint8_t g, uint8_t b) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    c->SetPixel(x0, y0, r, g, b);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
