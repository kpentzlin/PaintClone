// PaintClone – plattformunabhängige Pixelalgorithmen (auch unter Linux testbar)
#include "algo.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

bool SizeAllowed(long long w, long long h) {
    return w >= 1 && h >= 1 && w <= MAX_DIM && h <= MAX_DIM && w * h <= MAX_PIXELS;
}

void MakeOpaque(Pixmap& p) {
    for (auto& v : p.px) v |= 0xFF000000u;
}

Pixmap FlipH(const Pixmap& s) {
    Pixmap d(s.w, s.h);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) d.at(x, y) = s.at(s.w - 1 - x, y);
    return d;
}

Pixmap FlipV(const Pixmap& s) {
    Pixmap d(s.w, s.h);
    for (int y = 0; y < s.h; ++y)
        std::copy(s.px.begin() + (size_t)(s.h - 1 - y) * s.w, s.px.begin() + (size_t)(s.h - y) * s.w,
                  d.px.begin() + (size_t)y * s.w);
    return d;
}

// im Uhrzeigersinn
Pixmap Rotate90(const Pixmap& s) {
    Pixmap d(s.h, s.w);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) d.at(s.h - 1 - y, x) = s.at(x, y);
    return d;
}

Pixmap Rotate180(const Pixmap& s) {
    Pixmap d(s.w, s.h);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) d.at(s.w - 1 - x, s.h - 1 - y) = s.at(x, y);
    return d;
}

// gegen den Uhrzeigersinn
Pixmap Rotate270(const Pixmap& s) {
    Pixmap d(s.h, s.w);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) d.at(y, s.w - 1 - x) = s.at(x, y);
    return d;
}

void InvertColors(Pixmap& p) {
    for (auto& v : p.px) v ^= 0x00FFFFFFu;
}

// Neigen: zuerst horizontal (Zeilen verschieben), dann vertikal (Spalten verschieben)
static Pixmap SkewOne(const Pixmap& s, double deg, bool horizontal, uint32_t bgpx) {
    double t = std::tan(deg * 3.14159265358979323846 / 180.0);
    if (std::fabs(t) < 1e-9) return s;
    if (horizontal) {
        int extra = (int)std::lround(std::fabs(t) * (s.h - 1));
        if (!SizeAllowed((long long)s.w + extra, s.h)) return s;
        Pixmap d(s.w + extra, s.h, bgpx);
        for (int y = 0; y < s.h; ++y) {
            // positive Gradzahl: obere Kante nach rechts
            double off = t >= 0 ? t * (s.h - 1 - y) : -t * y;
            int o = (int)std::lround(off);
            for (int x = 0; x < s.w; ++x) d.at(x + o, y) = s.at(x, y);
        }
        return d;
    } else {
        int extra = (int)std::lround(std::fabs(t) * (s.w - 1));
        if (!SizeAllowed(s.w, (long long)s.h + extra)) return s;
        Pixmap d(s.w, s.h + extra, bgpx);
        for (int x = 0; x < s.w; ++x) {
            // positive Gradzahl: linke Kante nach unten
            double off = t >= 0 ? t * (s.w - 1 - x) : -t * x;
            int o = (int)std::lround(off);
            for (int y = 0; y < s.h; ++y) d.at(x, y + o) = s.at(x, y);
        }
        return d;
    }
}

Pixmap Skew(const Pixmap& s, double degH, double degV, uint32_t bgpx) {
    Pixmap a = SkewOne(s, degH, true, bgpx);
    return SkewOne(a, degV, false, bgpx);
}

Pixmap ExtendCanvas(const Pixmap& s, int nw, int nh, uint32_t bgpx) {
    Pixmap d(nw, nh, bgpx);
    int cw = std::min(nw, s.w), ch = std::min(nh, s.h);
    for (int y = 0; y < ch; ++y)
        std::copy(s.px.begin() + (size_t)y * s.w, s.px.begin() + (size_t)y * s.w + cw,
                  d.px.begin() + (size_t)y * nw);
    return d;
}

Pixmap SubImage(const Pixmap& s, int x0, int y0, int w, int h, uint32_t outside) {
    Pixmap d(w, h, outside);
    for (int y = 0; y < h; ++y) {
        int sy = y0 + y;
        if (sy < 0 || sy >= s.h) continue;
        for (int x = 0; x < w; ++x) {
            int sx = x0 + x;
            if (sx < 0 || sx >= s.w) continue;
            d.at(x, y) = s.at(sx, sy);
        }
    }
    return d;
}

// Scanline-Füllalgorithmus (4er-Nachbarschaft, exakte Farbgleichheit)
bool FloodFill(Pixmap& p, int x, int y, uint32_t color) {
    if (!p.in(x, y)) return false;
    uint32_t target = p.at(x, y);
    if (target == color) return false;
    std::vector<POINT> stack;
    stack.push_back({x, y});
    while (!stack.empty()) {
        POINT pt = stack.back();
        stack.pop_back();
        int lx = pt.x, cy = pt.y;
        if (p.at(lx, cy) != target) continue;
        while (lx > 0 && p.at(lx - 1, cy) == target) --lx;
        int rx = pt.x;
        while (rx < p.w - 1 && p.at(rx + 1, cy) == target) ++rx;
        for (int i = lx; i <= rx; ++i) p.at(i, cy) = color;
        for (int ny = cy - 1; ny <= cy + 1; ny += 2) {
            if (ny < 0 || ny >= p.h) continue;
            bool inSpan = false;
            for (int i = lx; i <= rx; ++i) {
                if (p.at(i, ny) == target) {
                    if (!inSpan) {
                        stack.push_back({i, ny});
                        inSpan = true;
                    }
                } else {
                    inSpan = false;
                }
            }
        }
    }
    return true;
}

// Maske für Freihandauswahl: Polygon (Bildkoordinaten) relativ zu (ox,oy), Größe w×h.
// Gerade-Ungerade-Regel auf Pixelmitten, zusätzlich wird die Randlinie selbst markiert.
std::vector<uint8_t> PolygonMask(const std::vector<POINT>& pts, int ox, int oy, int w, int h) {
    std::vector<uint8_t> m((size_t)w * h, 0);
    size_t n = pts.size();
    if (n == 0 || w <= 0 || h <= 0) return m;
    std::vector<double> xs;
    for (int y = 0; y < h; ++y) {
        double cy = oy + y + 0.5;
        xs.clear();
        for (size_t i = 0; i < n; ++i) {
            const POINT& a = pts[i];
            const POINT& b = pts[(i + 1) % n];
            double ay = a.y + 0.5, by = b.y + 0.5;
            if ((ay <= cy && by > cy) || (by <= cy && ay > cy)) {
                double t = (cy - ay) / (by - ay);
                xs.push_back(a.x + 0.5 + t * (b.x - a.x));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int x0 = (int)std::ceil(xs[k] - 0.5) - ox;
            int x1 = (int)std::floor(xs[k + 1] - 0.5) - ox;
            x0 = std::max(x0, 0);
            x1 = std::min(x1, w - 1);
            for (int x = x0; x <= x1; ++x) m[(size_t)y * w + x] = 1;
        }
    }
    // Randlinie
    for (size_t i = 0; i < n; ++i) {
        POINT a = pts[i], b = pts[(i + 1) % n];
        int x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y;
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            int mx = x0 - ox, my = y0 - oy;
            if (mx >= 0 && my >= 0 && mx < w && my < h) m[(size_t)my * w + mx] = 1;
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    return m;
}

void PutPixel(Pixmap& p, int x, int y, uint32_t c) {
    if (p.in(x, y)) p.at(x, y) = c;
}

template <typename F>
static void WalkLine(int x0, int y0, int x1, int y1, F f) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        f(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void BresenhamLine(Pixmap& p, int x0, int y0, int x1, int y1, uint32_t c) {
    // Sehr lange Linien weit außerhalb des Bildes begrenzen
    WalkLine(x0, y0, x1, y1, [&](int x, int y) { PutPixel(p, x, y, c); });
}

std::vector<POINT> BrushOffsets(int shape, int size) {
    std::vector<POINT> o;
    if (size <= 1) {
        o.push_back({0, 0});
        return o;
    }
    int half = size / 2;
    switch (shape) {
    case BR_CIRCLE: {
        double c = (size - 1) / 2.0;
        double r2 = (size / 2.0) * (size / 2.0) + 0.25;
        for (int j = 0; j < size; ++j)
            for (int i = 0; i < size; ++i) {
                double dx = i - c, dy = j - c;
                if (dx * dx + dy * dy <= r2) o.push_back({i - half, j - half});
            }
        break;
    }
    case BR_SQUARE:
        for (int j = 0; j < size; ++j)
            for (int i = 0; i < size; ++i) o.push_back({i - half, j - half});
        break;
    case BR_SLASH:
        for (int k = 0; k < size; ++k) o.push_back({k - half, (size - 1 - k) - half});
        break;
    case BR_BACKSLASH:
    default:
        for (int k = 0; k < size; ++k) o.push_back({k - half, k - half});
        break;
    }
    return o;
}

void StampLine(Pixmap& p, POINT a, POINT b, const std::vector<POINT>& offs, uint32_t c) {
    WalkLine(a.x, a.y, b.x, b.y, [&](int x, int y) {
        for (const POINT& o : offs) PutPixel(p, x + o.x, y + o.y, c);
    });
}

void EraseLine(Pixmap& p, POINT a, POINT b, int size, uint32_t bgpx) {
    int half = size / 2;
    WalkLine(a.x, a.y, b.x, b.y, [&](int x, int y) {
        int x0 = std::max(0, x - half), y0 = std::max(0, y - half);
        int x1 = std::min(p.w - 1, x - half + size - 1), y1 = std::min(p.h - 1, y - half + size - 1);
        for (int yy = y0; yy <= y1; ++yy)
            for (int xx = x0; xx <= x1; ++xx) p.at(xx, yy) = bgpx;
    });
}

void ReplaceLine(Pixmap& p, POINT a, POINT b, int size, uint32_t from, uint32_t to) {
    int half = size / 2;
    WalkLine(a.x, a.y, b.x, b.y, [&](int x, int y) {
        int x0 = std::max(0, x - half), y0 = std::max(0, y - half);
        int x1 = std::min(p.w - 1, x - half + size - 1), y1 = std::min(p.h - 1, y - half + size - 1);
        for (int yy = y0; yy <= y1; ++yy)
            for (int xx = x0; xx <= x1; ++xx)
                if (p.at(xx, yy) == from) p.at(xx, yy) = to;
    });
}

