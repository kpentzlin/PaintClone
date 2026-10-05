// PaintClone – plattformunabhängige Pixelalgorithmen
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
// Minimale Ersatztypen für Tests unter Linux
typedef long LONG;
struct POINT {
    LONG x, y;
};
#endif

// Pixel im Speicher: 0xAARRGGBB (entspricht BGRA im DIB / GDI+ 32bppARGB)
struct Pixmap {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    Pixmap() {}
    Pixmap(int W, int H, uint32_t fill = 0xFFFFFFFFu) : w(W), h(H), px((size_t)W * H, fill) {}
    bool empty() const { return w <= 0 || h <= 0; }
    bool in(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    uint32_t& at(int x, int y) { return px[(size_t)y * w + x]; }
    uint32_t at(int x, int y) const { return px[(size_t)y * w + x]; }
    size_t bytes() const { return px.size() * sizeof(uint32_t); }
    void fill(uint32_t c) { std::fill(px.begin(), px.end(), c); }
};

// Maximale Bildgröße, um Speicherprobleme zu vermeiden
constexpr long long MAX_PIXELS = 200LL * 1000 * 1000;
constexpr int MAX_DIM = 50000;
bool SizeAllowed(long long w, long long h);

enum BrushShape { BR_CIRCLE = 0, BR_SQUARE = 1, BR_SLASH = 2, BR_BACKSLASH = 3 };

void MakeOpaque(Pixmap& p);
Pixmap FlipH(const Pixmap& s);
Pixmap FlipV(const Pixmap& s);
Pixmap Rotate90(const Pixmap& s);   // im Uhrzeigersinn
Pixmap Rotate180(const Pixmap& s);
Pixmap Rotate270(const Pixmap& s);  // gegen den Uhrzeigersinn
void InvertColors(Pixmap& p);
Pixmap Skew(const Pixmap& s, double degH, double degV, uint32_t bgpx);
Pixmap ExtendCanvas(const Pixmap& s, int nw, int nh, uint32_t bgpx);
Pixmap SubImage(const Pixmap& s, int x, int y, int w, int h, uint32_t outside);
bool FloodFill(Pixmap& p, int x, int y, uint32_t color);
std::vector<uint8_t> PolygonMask(const std::vector<POINT>& pts, int ox, int oy, int w, int h);

void PutPixel(Pixmap& p, int x, int y, uint32_t c);
void BresenhamLine(Pixmap& p, int x0, int y0, int x1, int y1, uint32_t c);
std::vector<POINT> BrushOffsets(int shape, int size);
void StampLine(Pixmap& p, POINT a, POINT b, const std::vector<POINT>& offs, uint32_t c);
void EraseLine(Pixmap& p, POINT a, POINT b, int size, uint32_t bgpx);
void ReplaceLine(Pixmap& p, POINT a, POINT b, int size, uint32_t from, uint32_t to);
