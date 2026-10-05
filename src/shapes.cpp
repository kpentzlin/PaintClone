// PaintClone – Zeichenprimitive
#include "common.h"

using namespace Gdiplus;

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

static void PrepareGraphics(Graphics& gr, bool smooth) {
    gr.SetSmoothingMode(smooth ? SmoothingModeAntiAlias8x8 : SmoothingModeNone);
    gr.SetPixelOffsetMode(PixelOffsetModeNone);
    gr.SetCompositingQuality(CompositingQualityHighQuality);
}

void DrawStraightLine(Pixmap& p, POINT a, POINT b, int width, COLORREF c, bool smooth) {
    if (width <= 1 && !smooth) {
        BresenhamLine(p, a.x, a.y, b.x, b.y, RGBtoPX(c));
        return;
    }
    auto bm = WrapBitmap(p);
    Graphics gr(bm.get());
    PrepareGraphics(gr, smooth);
    Pen pen(GpColor(c), (REAL)width);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    if (a.x == b.x && a.y == b.y) {
        SolidBrush br(GpColor(c));
        if (width <= 1) PutPixel(p, a.x, a.y, RGBtoPX(c));
        else gr.FillEllipse(&br, a.x - width / 2.0f, a.y - width / 2.0f, (REAL)width, (REAL)width);
        return;
    }
    gr.DrawLine(&pen, (REAL)a.x, (REAL)a.y, (REAL)b.x, (REAL)b.y);
}

void DrawBezier(Pixmap& p, POINT p0, POINT c1, POINT c2, POINT p3, int width, COLORREF c, bool smooth) {
    if (width <= 1 && !smooth) {
        // eigene Abtastung für exakte 1-Pixel-Kurven
        double len = std::hypot(c1.x - p0.x, c1.y - p0.y) + std::hypot(c2.x - c1.x, c2.y - c1.y) +
                     std::hypot(p3.x - c2.x, p3.y - c2.y);
        int steps = std::max(8, (int)(len / 2));
        POINT prev = p0;
        uint32_t px = RGBtoPX(c);
        for (int i = 1; i <= steps; ++i) {
            double t = (double)i / steps, u = 1 - t;
            double x = u * u * u * p0.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * p3.x;
            double y = u * u * u * p0.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * p3.y;
            POINT cur = {(LONG)std::lround(x), (LONG)std::lround(y)};
            BresenhamLine(p, prev.x, prev.y, cur.x, cur.y, px);
            prev = cur;
        }
        return;
    }
    auto bm = WrapBitmap(p);
    Graphics gr(bm.get());
    PrepareGraphics(gr, smooth);
    Pen pen(GpColor(c), (REAL)width);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    gr.DrawBezier(&pen, (REAL)p0.x, (REAL)p0.y, (REAL)c1.x, (REAL)c1.y, (REAL)c2.x, (REAL)c2.y, (REAL)p3.x,
                  (REAL)p3.y);
}

// ---------------------------------------------------------------------------
// Formen
// ---------------------------------------------------------------------------
static void AddNormalizedPolygon(GraphicsPath& path, const std::vector<PointF>& pts, RectF r, bool normalize) {
    if (pts.empty()) return;
    float minx = pts[0].X, maxx = pts[0].X, miny = pts[0].Y, maxy = pts[0].Y;
    for (auto& p : pts) {
        minx = std::min(minx, p.X); maxx = std::max(maxx, p.X);
        miny = std::min(miny, p.Y); maxy = std::max(maxy, p.Y);
    }
    if (!normalize) { minx = 0; miny = 0; maxx = 1; maxy = 1; }
    float sw = maxx - minx, sh = maxy - miny;
    if (sw <= 0) sw = 1;
    if (sh <= 0) sh = 1;
    std::vector<PointF> o;
    o.reserve(pts.size());
    for (auto& p : pts) o.push_back(PointF(r.X + (p.X - minx) / sw * r.Width, r.Y + (p.Y - miny) / sh * r.Height));
    path.AddPolygon(o.data(), (INT)o.size());
}

static std::vector<PointF> RegularPolygon(int n, double startDeg) {
    std::vector<PointF> v;
    for (int i = 0; i < n; ++i) {
        double a = (startDeg + 360.0 * i / n) * 3.14159265358979323846 / 180.0;
        v.push_back(PointF((REAL)(std::cos(a)), (REAL)(std::sin(a))));
    }
    return v;
}

static std::vector<PointF> Star(int n, double inner) {
    std::vector<PointF> v;
    for (int i = 0; i < 2 * n; ++i) {
        double a = (-90.0 + 180.0 * i / n) * 3.14159265358979323846 / 180.0;
        double rr = (i % 2 == 0) ? 1.0 : inner;
        v.push_back(PointF((REAL)(rr * std::cos(a)), (REAL)(rr * std::sin(a))));
    }
    return v;
}

static std::vector<PointF> Arrow(int dir) {
    // Pfeil nach rechts im Einheitsquadrat
    const float base[7][2] = {{0, 0.25f}, {0.6f, 0.25f}, {0.6f, 0}, {1, 0.5f}, {0.6f, 1}, {0.6f, 0.75f}, {0, 0.75f}};
    std::vector<PointF> v;
    for (auto& b : base) {
        float x = b[0], y = b[1];
        switch (dir) {
        case 0: v.push_back(PointF(x, y)); break;          // rechts
        case 1: v.push_back(PointF(1 - x, y)); break;      // links
        case 2: v.push_back(PointF(y, 1 - x)); break;      // oben
        default: v.push_back(PointF(y, x)); break;         // unten
        }
    }
    return v;
}

void AddShapePath(GraphicsPath& path, int shape, RectF r) {
    auto poly = [&](std::initializer_list<std::pair<float, float>> l) {
        std::vector<PointF> v;
        for (auto& p : l) v.push_back(PointF(p.first, p.second));
        AddNormalizedPolygon(path, v, r, false);
    };
    switch (shape) {
    case SH_TRIANGLE: poly({{0.5f, 0}, {1, 1}, {0, 1}}); break;
    case SH_RTRIANGLE: poly({{0, 0}, {1, 1}, {0, 1}}); break;
    case SH_DIAMOND: poly({{0.5f, 0}, {1, 0.5f}, {0.5f, 1}, {0, 0.5f}}); break;
    case SH_PENTAGON: AddNormalizedPolygon(path, RegularPolygon(5, -90), r, true); break;
    case SH_HEXAGON: poly({{0.25f, 0}, {0.75f, 0}, {1, 0.5f}, {0.75f, 1}, {0.25f, 1}, {0, 0.5f}}); break;
    case SH_OCTAGON: AddNormalizedPolygon(path, RegularPolygon(8, 22.5), r, true); break;
    case SH_ARROW_R: AddNormalizedPolygon(path, Arrow(0), r, false); break;
    case SH_ARROW_L: AddNormalizedPolygon(path, Arrow(1), r, false); break;
    case SH_ARROW_U: AddNormalizedPolygon(path, Arrow(2), r, false); break;
    case SH_ARROW_D: AddNormalizedPolygon(path, Arrow(3), r, false); break;
    case SH_STAR4: AddNormalizedPolygon(path, Star(4, 0.38), r, true); break;
    case SH_STAR5: AddNormalizedPolygon(path, Star(5, 0.382), r, true); break;
    case SH_STAR6: AddNormalizedPolygon(path, Star(6, 0.5), r, true); break;
    case SH_CROSS:
        poly({{1 / 3.f, 0}, {2 / 3.f, 0}, {2 / 3.f, 1 / 3.f}, {1, 1 / 3.f}, {1, 2 / 3.f}, {2 / 3.f, 2 / 3.f},
              {2 / 3.f, 1}, {1 / 3.f, 1}, {1 / 3.f, 2 / 3.f}, {0, 2 / 3.f}, {0, 1 / 3.f}, {1 / 3.f, 1 / 3.f}});
        break;
    case SH_LIGHTNING:
        poly({{0.38f, 0.00f}, {0.66f, 0.27f}, {0.55f, 0.33f}, {0.86f, 0.62f}, {0.76f, 0.67f}, {1.00f, 1.00f},
              {0.46f, 0.73f}, {0.58f, 0.67f}, {0.20f, 0.44f}, {0.33f, 0.37f}, {0.00f, 0.13f}});
        break;
    case SH_HEART: {
        auto P = [&](float x, float y) { return PointF(r.X + x * r.Width, r.Y + y * r.Height); };
        path.StartFigure();
        path.AddBezier(P(0.5f, 0.22f), P(0.5f, -0.06f), P(0.0f, -0.06f), P(0.0f, 0.3f));
        path.AddBezier(P(0.0f, 0.3f), P(0.0f, 0.62f), P(0.42f, 0.8f), P(0.5f, 1.0f));
        path.AddBezier(P(0.5f, 1.0f), P(0.58f, 0.8f), P(1.0f, 0.62f), P(1.0f, 0.3f));
        path.AddBezier(P(1.0f, 0.3f), P(1.0f, -0.06f), P(0.5f, -0.06f), P(0.5f, 0.22f));
        path.CloseFigure();
        break;
    }
    case SH_CALLOUT_RECT:
        poly({{0, 0}, {1, 0}, {1, 0.75f}, {0.42f, 0.75f}, {0.17f, 1}, {0.25f, 0.75f}, {0, 0.75f}});
        break;
    case SH_CALLOUT_OVAL: {
        // Ellipse im oberen Bereich mit Spitze nach links unten, als Polygon angenähert
        float cx = 0.5f, cy = 0.39f, rx = 0.5f, ry = 0.39f;
        double perim = 3.2 * (r.Width + r.Height) * 0.5;
        int seg = std::max(48, (int)(perim / 3));
        const double pi = 3.14159265358979323846;
        double aStart = 125.0 * pi / 180.0, aEnd = (110.0 + 360.0) * pi / 180.0;
        std::vector<PointF> v;
        for (int i = 0; i <= seg; ++i) {
            double a = aStart + (aEnd - aStart) * i / seg;
            v.push_back(PointF((REAL)(cx + rx * std::cos(a)), (REAL)(cy + ry * std::sin(a))));
        }
        v.push_back(PointF(0.12f, 1.0f));
        AddNormalizedPolygon(path, v, r, false);
        break;
    }
    default: path.AddRectangle(r); break;
    }
}

static void AddRoundRect(GraphicsPath& path, RectF r) {
    float rad = std::min(r.Width, r.Height) / 5.0f;
    rad = std::min(rad, 24.0f);
    float d = rad * 2;
    if (d < 2) {
        path.AddRectangle(r);
        return;
    }
    path.StartFigure();
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

static void BuildToolPath(GraphicsPath& path, int tool, int shape, RectF r) {
    switch (tool) {
    case T_RECT: path.AddRectangle(r); break;
    case T_ELLIPSE: path.AddEllipse(r); break;
    case T_ROUNDRECT: AddRoundRect(path, r); break;
    default: AddShapePath(path, shape, r); break;
    }
}

void DrawShape(Pixmap& p, int tool, int shape, RECT rr, COLORREF outline, COLORREF fill, int style, int width,
               bool smooth) {
    int x0 = std::min(rr.left, rr.right), x1 = std::max(rr.left, rr.right);
    int y0 = std::min(rr.top, rr.bottom), y1 = std::max(rr.top, rr.bottom);
    float wd = (float)std::max(1, width);

    // Rechtecke ohne Glättung pixelgenau selbst zeichnen
    if (tool == T_RECT && !smooth) {
        uint32_t oc = RGBtoPX(outline), fc = RGBtoPX(style == FS_FILL ? outline : fill);
        int w = (int)wd;
        for (int y = y0; y <= y1; ++y) {
            if (y < 0 || y >= p.h) continue;
            for (int x = x0; x <= x1; ++x) {
                if (x < 0 || x >= p.w) continue;
                bool border = (x - x0 < w) || (x1 - x < w) || (y - y0 < w) || (y1 - y < w);
                if (style == FS_FILL) p.at(x, y) = fc;
                else if (border) p.at(x, y) = oc;
                else if (style == FS_BOTH) p.at(x, y) = fc;
            }
        }
        return;
    }

    auto bm = WrapBitmap(p);
    Graphics gr(bm.get());
    PrepareGraphics(gr, smooth);

    if (style == FS_FILL) {
        RectF fr((REAL)x0 - 0.5f, (REAL)y0 - 0.5f, (REAL)(x1 - x0 + 1), (REAL)(y1 - y0 + 1));
        GraphicsPath path;
        BuildToolPath(path, tool, shape, fr);
        SolidBrush br(GpColor(outline));
        gr.FillPath(&br, &path);
        return;
    }
    float inset = (wd - 1) / 2.0f;
    RectF sr(x0 + inset, y0 + inset, std::max(0.0f, (x1 - x0) - (wd - 1)), std::max(0.0f, (y1 - y0) - (wd - 1)));
    GraphicsPath path;
    BuildToolPath(path, tool, shape, sr);
    if (style == FS_BOTH) {
        SolidBrush br(GpColor(fill));
        gr.FillPath(&br, &path);
    }
    Pen pen(GpColor(outline), wd);
    pen.SetLineJoin(tool == T_RECT ? LineJoinMiter : LineJoinRound);
    gr.DrawPath(&pen, &path);
}

void DrawPolygonShape(Pixmap& p, const std::vector<POINT>& pts, COLORREF outline, COLORREF fill, int style,
                      int width, bool smooth) {
    if (pts.size() < 2) return;
    std::vector<PointF> v;
    for (auto& pt : pts) v.push_back(PointF((REAL)pt.x, (REAL)pt.y));
    bool ownOutline = (style != FS_FILL) && width <= 1 && !smooth;
    {
        auto bm = WrapBitmap(p);
        Graphics gr(bm.get());
        PrepareGraphics(gr, smooth);
        if (style != FS_OUTLINE) {
            SolidBrush br(GpColor(style == FS_FILL ? outline : fill));
            gr.FillPolygon(&br, v.data(), (INT)v.size());
        }
        if (style != FS_FILL && !ownOutline) {
            Pen pen(GpColor(outline), (REAL)width);
            pen.SetLineJoin(LineJoinRound);
            gr.DrawPolygon(&pen, v.data(), (INT)v.size());
        }
    }
    if (ownOutline) {
        uint32_t c = RGBtoPX(outline);
        for (size_t i = 0; i < pts.size(); ++i) {
            POINT a = pts[i], b = pts[(i + 1) % pts.size()];
            BresenhamLine(p, a.x, a.y, b.x, b.y, c);
        }
    }
}
