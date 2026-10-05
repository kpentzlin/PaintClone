// PaintClone – Zeichenfläche: Darstellung, Zoom, Bildlauf und Werkzeuge
#include "common.h"

#include <random>

using namespace Gdiplus;

namespace {

const wchar_t* kCanvasClass = L"PaintCloneCanvas";
const UINT_PTR TIMER_AIR = 1;
const COLORREF kWorkspaceColor = RGB(201, 211, 226);
const COLORREF kShadowColor = RGB(171, 182, 199);

int scrollX = 0, scrollY = 0;

Pixmap scratch;
bool useScratch = false;

HDC memDC = nullptr;
HBITMAP memBmp = nullptr;
int memW = 0, memH = 0;

enum DragKind {
    DK_NONE, DK_PAINT, DK_LINE, DK_SHAPE, DK_CURVE, DK_POLY, DK_SELRECT, DK_LASSO,
    DK_SELMOVE, DK_SELSIZE, DK_RESIZECANVAS, DK_TEXTRECT, DK_PICK
};

struct DragState {
    DragKind kind = DK_NONE;
    int button = 0;  // 0 = links, 1 = rechts
    POINT start{}, last{}, cur{};
    bool shift = false;
    int handle = 0;
    RECT origRc{};
    POINT grab{};
    std::vector<POINT> brush;
} drag;

struct CurveState {
    int phase = 0;  // 0 = keine, 1 = Linie gezogen, 2 = erster Bogen gesetzt
    POINT p0{}, p3{}, c1{}, c2{};
    int button = 0;
} curve;

struct PolyState {
    bool active = false;
    std::vector<POINT> pts;
    int button = 0;
} poly;

struct SelState {
    bool active = false;
    bool floating = false;
    RECT rc{};                  // Bildkoordinaten, rechts/unten exklusiv
    Pixmap pix;                 // schwebender Inhalt (Alpha = Maske)
    std::vector<uint8_t> mask;  // Freihandmaske (nicht schwebend), leer = Rechteck
    std::vector<POINT> lasso;   // Umriss der Freihandauswahl
} sel;

struct TextState {
    bool active = false;
    RECT rc{};  // Bildkoordinaten, exklusiv
    HWND edit = nullptr;
    HFONT font = nullptr;
    HBRUSH brush = nullptr;
    HBRUSH opaqueBrush = nullptr;
} text;

int resizeW = 0, resizeH = 0;
POINT hoverImg{0, 0};
bool hoverValid = false;
std::mt19937 rng(12345);

// ---------------------------------------------------------------------------
// Koordinaten
// ---------------------------------------------------------------------------
int Margin() { return S(6); }

double ImgXf(int sx) { return (sx - Margin() + scrollX) / g.zoom; }
double ImgYf(int sy) { return (sy - Margin() + scrollY) / g.zoom; }
POINT ToImg(POINT s) { return {(LONG)std::floor(ImgXf(s.x)), (LONG)std::floor(ImgYf(s.y))}; }
int ScrX(double ix) { return (int)std::floor(ix * g.zoom + 1e-7) + Margin() - scrollX; }
int ScrY(double iy) { return (int)std::floor(iy * g.zoom + 1e-7) + Margin() - scrollY; }

RECT ImgRectToScreen(const RECT& r) {
    return {ScrX(r.left), ScrY(r.top), ScrX(r.right), ScrY(r.bottom)};
}

COLORREF Primary(int b) { return b == 0 ? g.fg : g.bg; }
COLORREF Secondary(int b) { return b == 0 ? g.bg : g.fg; }

RECT NormRectIncl(POINT a, POINT b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

POINT ConstrainSquare(POINT a, POINT b) {
    int dx = b.x - a.x, dy = b.y - a.y;
    int m = std::max(std::abs(dx), std::abs(dy));
    return {a.x + (dx < 0 ? -m : m), a.y + (dy < 0 ? -m : m)};
}

POINT ConstrainLine(POINT a, POINT b) {
    int dx = b.x - a.x, dy = b.y - a.y;
    if (std::abs(dx) > 2 * std::abs(dy)) return {b.x, a.y};
    if (std::abs(dy) > 2 * std::abs(dx)) return {a.x, b.y};
    return ConstrainSquare(a, b);
}

int RW(const RECT& r) { return r.right - r.left; }
int RH(const RECT& r) { return r.bottom - r.top; }

// ---------------------------------------------------------------------------
// Auswahl
// ---------------------------------------------------------------------------
// Schwebender Inhalt in der aktuellen Auswahlgröße (nächster Nachbar)
Pixmap FloatingContent() {
    int rw = RW(sel.rc), rh = RH(sel.rc);
    if (rw == sel.pix.w && rh == sel.pix.h) return sel.pix;
    Pixmap d(rw, rh, 0);
    for (int y = 0; y < rh; ++y) {
        int sy = (int)((long long)y * sel.pix.h / rh);
        for (int x = 0; x < rw; ++x) {
            int sx = (int)((long long)x * sel.pix.w / rw);
            d.at(x, y) = sel.pix.at(sx, sy);
        }
    }
    return d;
}

void ComposeFloating(Pixmap& t) {
    int rw = RW(sel.rc), rh = RH(sel.rc);
    if (rw <= 0 || rh <= 0 || sel.pix.empty()) return;
    uint32_t bgk = RGBtoPX(g.bg) & 0x00FFFFFFu;
    int x0 = std::max<int>(0, sel.rc.left), x1 = std::min<int>(t.w, sel.rc.right);
    int y0 = std::max<int>(0, sel.rc.top), y1 = std::min<int>(t.h, sel.rc.bottom);
    for (int y = y0; y < y1; ++y) {
        int sy = (int)((long long)(y - sel.rc.top) * sel.pix.h / rh);
        for (int x = x0; x < x1; ++x) {
            int sx = (int)((long long)(x - sel.rc.left) * sel.pix.w / rw);
            uint32_t v = sel.pix.at(sx, sy);
            if ((v >> 24) < 128) continue;
            if (g.transparentSel && (v & 0x00FFFFFFu) == bgk) continue;
            t.at(x, y) = v | 0xFF000000u;
        }
    }
}

void LiftSelection(bool keepOriginal) {
    if (!sel.active || sel.floating) return;
    PushUndo();
    int rw = RW(sel.rc), rh = RH(sel.rc);
    sel.pix = Pixmap(rw, rh, 0);
    uint32_t bgp = RGBtoPX(g.bg);
    for (int y = 0; y < rh; ++y) {
        int iy = sel.rc.top + y;
        for (int x = 0; x < rw; ++x) {
            int ix = sel.rc.left + x;
            if (!g.img.in(ix, iy)) continue;
            if (!sel.mask.empty() && !sel.mask[(size_t)y * rw + x]) continue;
            sel.pix.at(x, y) = g.img.at(ix, iy) | 0xFF000000u;
            if (!keepOriginal) g.img.at(ix, iy) = bgp;
        }
    }
    sel.mask.clear();
    sel.floating = true;
    MarkDirty();
}

void ClearSelectionState() {
    sel = SelState();
    UpdateStatusSel(0, 0, false);
}

void CommitSelection() {
    if (!sel.active) return;
    if (sel.floating) {
        ComposeFloating(g.img);
        MarkDirty();
    }
    ClearSelectionState();
}

RECT SelScreenRect() { return ImgRectToScreen(sel.rc); }

// Griffe: 1=LO 2=O 3=RO 4=R 5=RU 6=U 7=LU 8=L, 9=innen
POINT HandleCenter(const RECT& r, int h) {
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    switch (h) {
    case 1: return {r.left, r.top};
    case 2: return {cx, r.top};
    case 3: return {r.right, r.top};
    case 4: return {r.right, cy};
    case 5: return {r.right, r.bottom};
    case 6: return {cx, r.bottom};
    case 7: return {r.left, r.bottom};
    default: return {r.left, cy};
    }
}

int HitSelection(POINT s) {
    if (!sel.active) return 0;
    RECT r = SelScreenRect();
    int hs = S(4) + 1;
    for (int h = 1; h <= 8; ++h) {
        POINT c = HandleCenter(r, h);
        if (std::abs(s.x - c.x) <= hs && std::abs(s.y - c.y) <= hs) return h;
    }
    if (s.x >= r.left && s.x < r.right && s.y >= r.top && s.y < r.bottom) return 9;
    return 0;
}

// Leinwand-Ziehgriffe: 1 = rechts, 2 = unten, 3 = Ecke
RECT CanvasHandleRect(int h) {
    int iw = ScrX(g.img.w), ih = ScrY(g.img.h);
    int ox = ScrX(0), oy = ScrY(0);
    int hs = S(5);
    switch (h) {
    case 1: return {iw, (oy + ih) / 2 - hs / 2, iw + hs, (oy + ih) / 2 - hs / 2 + hs};
    case 2: return {(ox + iw) / 2 - hs / 2, ih, (ox + iw) / 2 - hs / 2 + hs, ih + hs};
    default: return {iw, ih, iw + hs, ih + hs};
    }
}

int HitCanvasHandle(POINT s) {
    for (int h = 3; h >= 1; --h) {
        RECT r = CanvasHandleRect(h);
        InflateRect(&r, S(2), S(2));
        if (PtInRect(&r, s)) return h;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Vorschau
// ---------------------------------------------------------------------------
RECT CurrentShapeRect() {
    POINT b = drag.shift ? ConstrainSquare(drag.start, drag.cur) : drag.cur;
    return NormRectIncl(drag.start, b);
}

void DrawPreviewInto(Pixmap& t) {
    if (sel.floating) ComposeFloating(t);
    if (drag.kind == DK_LINE) {
        POINT b = drag.shift ? ConstrainLine(drag.start, drag.cur) : drag.cur;
        DrawStraightLine(t, drag.start, b, g.lineWidth, Primary(drag.button), g.smoothing);
    } else if (drag.kind == DK_SHAPE) {
        DrawShape(t, g.tool, g.shape, CurrentShapeRect(), Primary(drag.button), Secondary(drag.button),
                  g.fillStyle, g.lineWidth, g.smoothing);
    }
    if (curve.phase > 0 || drag.kind == DK_CURVE) {
        DrawBezier(t, curve.p0, curve.c1, curve.c2, curve.p3, g.lineWidth, Primary(curve.button), g.smoothing);
    }
    if (poly.active) {
        for (size_t i = 0; i + 1 < poly.pts.size(); ++i)
            DrawStraightLine(t, poly.pts[i], poly.pts[i + 1], g.lineWidth, Primary(poly.button), g.smoothing);
    }
}

bool NeedsPreview() {
    return sel.floating || drag.kind == DK_LINE || drag.kind == DK_SHAPE || drag.kind == DK_CURVE ||
           curve.phase > 0 || poly.active;
}

void RebuildPreview() {
    if (!NeedsPreview()) {
        useScratch = false;
        return;
    }
    scratch.w = g.img.w;
    scratch.h = g.img.h;
    scratch.px = g.img.px;
    DrawPreviewInto(scratch);
    useScratch = true;
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
HFONT CreateTextFont(double scale) {
    LOGFONTW lf = {};
    lf.lfHeight = -(LONG)std::lround(g.fontSize * 96.0 / 72.0 * scale);
    if (lf.lfHeight == 0) lf.lfHeight = -1;
    lf.lfWeight = g.fontBold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = g.fontItalic;
    lf.lfUnderline = g.fontUnderline;
    lf.lfStrikeOut = g.fontStrike;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = ANTIALIASED_QUALITY;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    wcsncpy_s(lf.lfFaceName, g.fontName.c_str(), _TRUNCATE);
    return CreateFontIndirectW(&lf);
}

const UINT kTextFlags = DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_EXPANDTABS;

int TextLineHeight() {
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT f = CreateTextFont(1.0);
    HGDIOBJ old = SelectObject(dc, f);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    DeleteObject(f);
    DeleteDC(dc);
    return tm.tmHeight + tm.tmExternalLeading;
}

wstring EditText() {
    if (!text.edit) return L"";
    int len = GetWindowTextLengthW(text.edit);
    wstring s(len + 1, L'\0');
    GetWindowTextW(text.edit, &s[0], len + 1);
    s.resize(len);
    return s;
}

void RenderTextInto(Pixmap& t, const RECT& r, const wstring& s) {
    int rw = RW(r), rh = RH(r);
    if (rw <= 0 || rh <= 0) return;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = rw;
    bi.bmiHeader.biHeight = -rh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) return;
    uint32_t* px = static_cast<uint32_t*>(bits);
    for (int y = 0; y < rh; ++y)
        for (int x = 0; x < rw; ++x) {
            int ix = r.left + x, iy = r.top + y;
            px[(size_t)y * rw + x] = t.in(ix, iy) ? t.at(ix, iy) : RGBtoPX(g.bg);
        }
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldBmp = SelectObject(dc, dib);
    HFONT f = CreateTextFont(1.0);
    HGDIOBJ oldFont = SelectObject(dc, f);
    RECT tr = {0, 0, rw, rh};
    if (g.textOpaque) {
        HBRUSH b = CreateSolidBrush(g.bg);
        FillRect(dc, &tr, b);
        DeleteObject(b);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, g.fg);
    DrawTextW(dc, s.c_str(), (int)s.size(), &tr, kTextFlags);
    GdiFlush();
    for (int y = 0; y < rh; ++y)
        for (int x = 0; x < rw; ++x) {
            int ix = r.left + x, iy = r.top + y;
            if (t.in(ix, iy)) t.at(ix, iy) = px[(size_t)y * rw + x] | 0xFF000000u;
        }
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(f);
    DeleteDC(dc);
    DeleteObject(dib);
}

void PaintImage(HDC hdc, const Pixmap& src, int ox, int oy, const RECT& clip);

void RebuildTextBrush() {
    if (text.brush) {
        DeleteObject(text.brush);
        text.brush = nullptr;
    }
    if (text.opaqueBrush) {
        DeleteObject(text.opaqueBrush);
        text.opaqueBrush = nullptr;
    }
    text.opaqueBrush = CreateSolidBrush(g.bg);
    if (!text.edit) return;
    RECT er;
    GetClientRect(text.edit, &er);
    if (er.right <= 0 || er.bottom <= 0) return;
    RECT sr = ImgRectToScreen(text.rc);
    HDC screen = GetDC(g.hCanvas);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, er.right, er.bottom);
    HGDIOBJ old = SelectObject(dc, bmp);
    HBRUSH wb = CreateSolidBrush(kWorkspaceColor);
    FillRect(dc, &er, wb);
    DeleteObject(wb);
    PaintImage(dc, g.img, ScrX(0) - sr.left, ScrY(0) - sr.top, er);
    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(g.hCanvas, screen);
    text.brush = CreatePatternBrush(bmp);
    DeleteObject(bmp);
}

void PositionTextEdit() {
    if (!text.active || !text.edit) return;
    RECT sr = ImgRectToScreen(text.rc);
    SetWindowPos(text.edit, nullptr, sr.left, sr.top, std::max<int>(1, RW(sr)), std::max<int>(1, RH(sr)),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    HFONT nf = CreateTextFont(g.zoom);
    SendMessageW(text.edit, WM_SETFONT, (WPARAM)nf, TRUE);
    if (text.font) DeleteObject(text.font);
    text.font = nf;
    SendMessageW(text.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    RebuildTextBrush();
    InvalidateRect(text.edit, nullptr, TRUE);
}

// Textfeld bei Bedarf in der Höhe wachsen lassen
void GrowTextBox() {
    if (!text.active) return;
    wstring s = EditText() + L" ";
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT f = CreateTextFont(1.0);
    HGDIOBJ old = SelectObject(dc, f);
    RECT r = {0, 0, RW(text.rc), 0};
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, kTextFlags | DT_CALCRECT);
    SelectObject(dc, old);
    DeleteObject(f);
    DeleteDC(dc);
    int need = r.bottom + 2;
    int maxH = std::max<int>(1, g.img.h - text.rc.top);
    need = std::min(need, maxH);
    if (need > RH(text.rc)) {
        text.rc.bottom = text.rc.top + need;
        PositionTextEdit();
        Canvas_Invalidate();
    }
}

LRESULT CALLBACK EditSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        PostMessageW(g.hCanvas, WM_KEYDOWN, VK_ESCAPE, 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        SendMessageW(hwnd, EM_SETSEL, 0, -1);
        return 0;
    }
    if (msg == WM_CHAR && wp == 1) return 0;  // Strg+A ohne Piepton
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, EditSubclass, 1);
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void DestroyTextEdit() {
    if (text.edit) {
        DestroyWindow(text.edit);
        text.edit = nullptr;
    }
    if (text.font) {
        DeleteObject(text.font);
        text.font = nullptr;
    }
    if (text.brush) {
        DeleteObject(text.brush);
        text.brush = nullptr;
    }
    if (text.opaqueBrush) {
        DeleteObject(text.opaqueBrush);
        text.opaqueBrush = nullptr;
    }
}

void CommitText() {
    if (!text.active) return;
    wstring s = EditText();
    RECT r = text.rc;
    DestroyTextEdit();
    text.active = false;
    if (!s.empty()) {
        PushUndo();
        RenderTextInto(g.img, r, s);
        MarkDirty();
    }
    SetFocus(g.hCanvas);
    Canvas_Invalidate();
}

void DiscardText() {
    if (!text.active) return;
    DestroyTextEdit();
    text.active = false;
}

void StartTextBox(RECT r) {
    // r: Bildkoordinaten exklusiv
    int lh = TextLineHeight();
    if (RW(r) < 8) r.right = r.left + std::max(8, std::min<int>(250, g.img.w - r.left));
    if (RH(r) < lh) r.bottom = r.top + lh + 2;
    r.left = std::max<LONG>(0, r.left);
    r.top = std::max<LONG>(0, r.top);
    r.right = std::min<LONG>(g.img.w, r.right);
    r.bottom = std::min<LONG>(g.img.h, r.bottom);
    if (RW(r) < 1 || RH(r) < 1) return;
    text.active = true;
    text.rc = r;
    text.edit = CreateWindowExW(0, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL, 0, 0, 10,
                                10, g.hCanvas, (HMENU)(INT_PTR)1001, g.hInst, nullptr);
    SetWindowSubclass(text.edit, EditSubclass, 1, 0);
    PositionTextEdit();
    SetFocus(text.edit);
    Canvas_Invalidate();
}

// ---------------------------------------------------------------------------
// Polygon / Kurve abschließen
// ---------------------------------------------------------------------------
void FinishPolygon() {
    if (!poly.active) return;
    std::vector<POINT> pts;
    for (auto& p : poly.pts)
        if (pts.empty() || pts.back().x != p.x || pts.back().y != p.y) pts.push_back(p);
    if (pts.size() > 1 && pts.front().x == pts.back().x && pts.front().y == pts.back().y) pts.pop_back();
    int b = poly.button;
    poly = PolyState();
    if (pts.size() >= 3) {
        PushUndo();
        DrawPolygonShape(g.img, pts, Primary(b), Secondary(b), g.fillStyle, g.lineWidth, g.smoothing);
        MarkDirty();
    } else if (pts.size() == 2) {
        PushUndo();
        DrawStraightLine(g.img, pts[0], pts[1], g.lineWidth, Primary(b), g.smoothing);
        MarkDirty();
    }
    RebuildPreview();
    Canvas_Invalidate();
}

void CommitCurve() {
    if (curve.phase == 0) return;
    PushUndo();
    DrawBezier(g.img, curve.p0, curve.c1, curve.c2, curve.p3, g.lineWidth, Primary(curve.button), g.smoothing);
    MarkDirty();
    curve = CurveState();
    RebuildPreview();
    Canvas_Invalidate();
}

// ---------------------------------------------------------------------------
// Sprühdose
// ---------------------------------------------------------------------------
void Spray(POINT c) {
    int r = kAirSizes[std::clamp(g.airSize, 0, 2)];
    int n = r + 2;
    std::uniform_real_distribution<double> ud(0.0, 1.0);
    uint32_t col = RGBtoPX(Primary(drag.button));
    for (int i = 0; i < n; ++i) {
        double a = ud(rng) * 6.283185307179586;
        double d = std::sqrt(ud(rng)) * r;
        int x = c.x + (int)std::lround(d * std::cos(a));
        int y = c.y + (int)std::lround(d * std::sin(a));
        PutPixel(g.img, x, y, col);
    }
}

// ---------------------------------------------------------------------------
// Ziehen beenden / abbrechen
// ---------------------------------------------------------------------------
void ApplyPaintSegment(POINT a, POINT b) {
    uint32_t col = RGBtoPX(Primary(drag.button));
    switch (g.tool) {
    case T_PENCIL: BresenhamLine(g.img, a.x, a.y, b.x, b.y, col); break;
    case T_BRUSH: StampLine(g.img, a, b, drag.brush, col); break;
    case T_ERASER: {
        int size = kEraserSizes[std::clamp(g.eraserSize, 0, 3)];
        if (drag.button == 0) EraseLine(g.img, a, b, size, RGBtoPX(g.bg));
        else ReplaceLine(g.img, a, b, size, RGBtoPX(g.fg), RGBtoPX(g.bg));
        break;
    }
    case T_AIRBRUSH: Spray(b); break;
    default: break;
    }
}

void EndDrag(bool cancel) {
    DragKind k = drag.kind;
    drag.kind = DK_NONE;
    if (GetCapture() == g.hCanvas) ReleaseCapture();
    KillTimer(g.hCanvas, TIMER_AIR);

    switch (k) {
    case DK_PAINT:
        if (cancel) {
            if (!g.undo.empty()) {
                g.img = std::move(g.undo.back());
                g.undo.pop_back();
            }
        } else {
            MarkDirty();
        }
        break;
    case DK_LINE:
        if (!cancel) {
            POINT b = drag.shift ? ConstrainLine(drag.start, drag.cur) : drag.cur;
            PushUndo();
            DrawStraightLine(g.img, drag.start, b, g.lineWidth, Primary(drag.button), g.smoothing);
            MarkDirty();
        }
        break;
    case DK_SHAPE:
        if (!cancel) {
            PushUndo();
            DrawShape(g.img, g.tool, g.shape, CurrentShapeRect(), Primary(drag.button), Secondary(drag.button),
                      g.fillStyle, g.lineWidth, g.smoothing);
            MarkDirty();
        }
        UpdateStatusSel(0, 0, false);
        break;
    case DK_CURVE:
        if (cancel) {
            curve = CurveState();
        } else if (curve.phase == 0) {
            if (curve.p0.x == curve.p3.x && curve.p0.y == curve.p3.y) curve = CurveState();
            else curve.phase = 1;
        } else if (curve.phase == 1) {
            curve.phase = 2;
        } else {
            CommitCurve();
        }
        break;
    case DK_POLY:
        if (cancel) poly = PolyState();
        break;
    case DK_SELRECT:
        if (!cancel) {
            RECT r = NormRectIncl(drag.start, drag.cur);
            r.right += 1;
            r.bottom += 1;
            r.left = std::max<LONG>(0, r.left);
            r.top = std::max<LONG>(0, r.top);
            r.right = std::min<LONG>(g.img.w, r.right);
            r.bottom = std::min<LONG>(g.img.h, r.bottom);
            if (RW(r) > 0 && RH(r) > 0 && !(drag.start.x == drag.cur.x && drag.start.y == drag.cur.y)) {
                sel = SelState();
                sel.active = true;
                sel.rc = r;
                UpdateStatusSel(RW(r), RH(r), true);
            } else {
                UpdateStatusSel(0, 0, false);
            }
        }
        break;
    case DK_LASSO:
        if (!cancel && sel.lasso.size() >= 3) {
            LONG minx = LONG_MAX, miny = LONG_MAX, maxx = LONG_MIN, maxy = LONG_MIN;
            for (auto& p : sel.lasso) {
                minx = std::min(minx, p.x); miny = std::min(miny, p.y);
                maxx = std::max(maxx, p.x); maxy = std::max(maxy, p.y);
            }
            RECT r = {std::max<LONG>(0, minx), std::max<LONG>(0, miny), std::min<LONG>(g.img.w, maxx + 1),
                      std::min<LONG>(g.img.h, maxy + 1)};
            if (RW(r) > 0 && RH(r) > 0) {
                auto m = PolygonMask(sel.lasso, r.left, r.top, RW(r), RH(r));
                bool any = std::any_of(m.begin(), m.end(), [](uint8_t v) { return v != 0; });
                if (any) {
                    auto lasso = std::move(sel.lasso);
                    sel = SelState();
                    sel.active = true;
                    sel.rc = r;
                    sel.mask = std::move(m);
                    sel.lasso = std::move(lasso);
                    UpdateStatusSel(RW(r), RH(r), true);
                    break;
                }
            }
        }
        ClearSelectionState();
        break;
    case DK_SELMOVE:
    case DK_SELSIZE:
        if (cancel) sel.rc = drag.origRc;
        break;
    case DK_RESIZECANVAS:
        if (!cancel && (resizeW != g.img.w || resizeH != g.img.h) && SizeAllowed(resizeW, resizeH)) {
            Canvas_CommitAll();
            ReplaceImage(ExtendCanvas(g.img, resizeW, resizeH, RGBtoPX(g.bg)));
        }
        break;
    case DK_TEXTRECT:
        if (!cancel) {
            RECT r = NormRectIncl(drag.start, drag.cur);
            r.right += 1;
            r.bottom += 1;
            StartTextBox(r);
        }
        break;
    case DK_PICK:
        if (!cancel && g.img.in(drag.cur.x, drag.cur.y)) {
            COLORREF c = PXtoRGB(g.img.at(drag.cur.x, drag.cur.y));
            if (drag.button == 0) SetColors(c, g.bg);
            else SetColors(g.fg, c);
            SetTool(g.prevTool);
        }
        break;
    default: break;
    }
    RebuildPreview();
    UpdateStatusInfo();
    Canvas_Invalidate();
}

// ---------------------------------------------------------------------------
// Maus
// ---------------------------------------------------------------------------
void OnButtonDown(HWND hwnd, int button, POINT s, WPARAM wp) {
    if (GetFocus() != hwnd && !(text.active && GetFocus() == text.edit)) SetFocus(hwnd);
    if (drag.kind != DK_NONE) {
        // zweite Maustaste während des Ziehens: abbrechen (wie Paint)
        EndDrag(true);
        if (drag.kind == DK_NONE && (curve.phase > 0)) curve = CurveState();
        RebuildPreview();
        Canvas_Invalidate();
        return;
    }
    POINT p = ToImg(s);
    drag = DragState();
    drag.button = button;
    drag.start = drag.last = drag.cur = p;
    drag.shift = (wp & MK_SHIFT) != 0;
    bool ctrl = (wp & MK_CONTROL) != 0;

    // Leinwandgröße ändern
    if (button == 0 && !(sel.active && HitSelection(s)) && !poly.active && curve.phase == 0) {
        int h = HitCanvasHandle(s);
        if (h) {
            if (text.active) CommitText();
            drag.kind = DK_RESIZECANVAS;
            drag.handle = h;
            resizeW = g.img.w;
            resizeH = g.img.h;
            SetCapture(hwnd);
            return;
        }
    }

    switch (g.tool) {
    case T_PENCIL:
    case T_BRUSH:
    case T_ERASER:
    case T_AIRBRUSH:
        PushUndo();
        drag.kind = DK_PAINT;
        if (g.tool == T_BRUSH)
            drag.brush = BrushOffsets(g.brushShape, kBrushSizes[std::clamp(g.brushSize, 0, 2)]);
        ApplyPaintSegment(p, p);
        if (g.tool == T_AIRBRUSH) SetTimer(hwnd, TIMER_AIR, 25, nullptr);
        break;
    case T_FILL:
        PushUndo();
        if (FloodFill(g.img, p.x, p.y, RGBtoPX(Primary(button)))) MarkDirty();
        else DropLastUndo();
        Canvas_Invalidate();
        return;
    case T_PICKER:
        drag.kind = DK_PICK;
        break;
    case T_ZOOM: {
        Canvas_ZoomStep(button == 0 ? 1 : -1, &s);
        return;
    }
    case T_LINE:
        drag.kind = DK_LINE;
        break;
    case T_RECT:
    case T_ELLIPSE:
    case T_ROUNDRECT:
    case T_SHAPE:
        drag.kind = DK_SHAPE;
        break;
    case T_CURVE:
        if (curve.phase == 0) {
            curve.button = button;
            curve.p0 = curve.p3 = curve.c1 = curve.c2 = p;
        } else if (button != curve.button) {
            curve = CurveState();
            RebuildPreview();
            Canvas_Invalidate();
            return;
        } else if (curve.phase == 1) {
            curve.c1 = curve.c2 = p;
        } else {
            curve.c2 = p;
        }
        drag.kind = DK_CURVE;
        break;
    case T_POLYGON:
        if (!poly.active) {
            poly.active = true;
            poly.button = button;
            poly.pts = {p, p};
        } else if (button != poly.button) {
            poly = PolyState();
            RebuildPreview();
            Canvas_Invalidate();
            return;
        } else {
            POINT f = poly.pts.front();
            int tol = std::max(2, (int)std::ceil(S(4) / g.zoom));
            if (poly.pts.size() >= 3 && std::abs(p.x - f.x) <= tol && std::abs(p.y - f.y) <= tol) {
                poly.pts.back() = f;
                FinishPolygon();
                return;
            }
            poly.pts.push_back(p);
        }
        drag.kind = DK_POLY;
        break;
    case T_RECTSEL:
    case T_FREESEL: {
        int h = HitSelection(s);
        if (h == 9) {
            if (!sel.floating) LiftSelection(ctrl);
            else if (ctrl) {
                // Kopie an aktueller Stelle absetzen, weiter mit schwebender Kopie
                PushUndo();
                ComposeFloating(g.img);
                MarkDirty();
            }
            drag.kind = DK_SELMOVE;
            drag.origRc = sel.rc;
            drag.grab = {p.x - sel.rc.left, p.y - sel.rc.top};
        } else if (h >= 1 && h <= 8) {
            if (!sel.floating) LiftSelection(false);
            drag.kind = DK_SELSIZE;
            drag.handle = h;
            drag.origRc = sel.rc;
        } else {
            CommitSelection();
            if (g.tool == T_RECTSEL) {
                drag.kind = DK_SELRECT;
            } else {
                drag.kind = DK_LASSO;
                sel.lasso = {p};
            }
        }
        break;
    }
    case T_TEXT:
        if (text.active) {
            CommitText();
            return;
        }
        drag.kind = DK_TEXTRECT;
        break;
    default: break;
    }
    if (drag.kind != DK_NONE) SetCapture(hwnd);
    RebuildPreview();
    Canvas_Invalidate();
}

void OnMouseMove(HWND, POINT s, WPARAM wp) {
    POINT p = ToImg(s);
    bool inside = g.img.in(p.x, p.y);
    UpdateStatusPos(p.x, p.y, inside);
    hoverImg = p;
    hoverValid = true;

    if (drag.kind == DK_NONE) {
        if (g.tool == T_ERASER) Canvas_Invalidate();
        return;
    }
    drag.shift = (wp & MK_SHIFT) != 0;
    POINT prev = drag.cur;
    drag.cur = p;
    switch (drag.kind) {
    case DK_PAINT:
        if (g.tool != T_AIRBRUSH) ApplyPaintSegment(drag.last, p);
        drag.last = p;
        break;
    case DK_LINE:
    case DK_SHAPE: {
        RECT r = drag.kind == DK_SHAPE ? CurrentShapeRect() : NormRectIncl(drag.start, p);
        UpdateStatusSel(RW(r) + 1, RH(r) + 1, true);
        break;
    }
    case DK_CURVE:
        if (curve.phase == 0) {
            curve.p3 = p;
            curve.c1 = curve.p0;
            curve.c2 = p;
        } else if (curve.phase == 1) {
            curve.c1 = curve.c2 = p;
        } else {
            curve.c2 = p;
        }
        break;
    case DK_POLY:
        if (!poly.pts.empty()) {
            POINT b = drag.shift && poly.pts.size() >= 2 ? ConstrainLine(poly.pts[poly.pts.size() - 2], p) : p;
            poly.pts.back() = b;
        }
        break;
    case DK_SELRECT: {
        RECT r = NormRectIncl(drag.start, p);
        UpdateStatusSel(RW(r) + 1, RH(r) + 1, true);
        break;
    }
    case DK_LASSO:
        if (sel.lasso.empty() || sel.lasso.back().x != p.x || sel.lasso.back().y != p.y) sel.lasso.push_back(p);
        break;
    case DK_SELMOVE: {
        int w = RW(sel.rc), h = RH(sel.rc);
        sel.rc.left = p.x - drag.grab.x;
        sel.rc.top = p.y - drag.grab.y;
        sel.rc.right = sel.rc.left + w;
        sel.rc.bottom = sel.rc.top + h;
        break;
    }
    case DK_SELSIZE: {
        RECT r = drag.origRc;
        int dx = p.x - drag.start.x, dy = p.y - drag.start.y;
        int hdl = drag.handle;
        if (hdl == 1 || hdl == 7 || hdl == 8) r.left = std::min<LONG>(r.left + dx, r.right - 1);
        if (hdl == 3 || hdl == 4 || hdl == 5) r.right = std::max<LONG>(r.right + dx, r.left + 1);
        if (hdl == 1 || hdl == 2 || hdl == 3) r.top = std::min<LONG>(r.top + dy, r.bottom - 1);
        if (hdl == 5 || hdl == 6 || hdl == 7) r.bottom = std::max<LONG>(r.bottom + dy, r.top + 1);
        if (SizeAllowed(RW(r), RH(r))) sel.rc = r;
        UpdateStatusSel(RW(sel.rc), RH(sel.rc), true);
        break;
    }
    case DK_RESIZECANVAS: {
        int nw = g.img.w, nh = g.img.h;
        if (drag.handle == 1 || drag.handle == 3) nw = std::max(1, (int)std::lround(ImgXf(s.x)));
        if (drag.handle == 2 || drag.handle == 3) nh = std::max(1, (int)std::lround(ImgYf(s.y)));
        resizeW = std::min(nw, MAX_DIM);
        resizeH = std::min(nh, MAX_DIM);
        UpdateStatusSel(resizeW, resizeH, true);
        break;
    }
    default: break;
    }
    (void)prev;
    if (drag.kind == DK_LINE || drag.kind == DK_SHAPE || drag.kind == DK_CURVE || drag.kind == DK_POLY ||
        drag.kind == DK_SELMOVE || drag.kind == DK_SELSIZE)
        RebuildPreview();
    Canvas_Invalidate();
}

void OnButtonUp(int button) {
    if (drag.kind == DK_NONE || button != drag.button) return;
    EndDrag(false);
}

// ---------------------------------------------------------------------------
// Darstellung
// ---------------------------------------------------------------------------
void PaintImage(HDC hdc, const Pixmap& src, int ox, int oy, const RECT& clip) {
    if (src.empty()) return;
    double z = g.zoom;
    int ix0 = std::max(0, (int)std::floor((clip.left - ox) / z));
    int iy0 = std::max(0, (int)std::floor((clip.top - oy) / z));
    int ix1 = std::min(src.w, (int)std::ceil((clip.right - ox) / z) + 1);
    int iy1 = std::min(src.h, (int)std::ceil((clip.bottom - oy) / z) + 1);
    if (ix1 <= ix0 || iy1 <= iy0) return;
    int dx0 = ox + (int)std::floor(ix0 * z + 1e-7), dx1 = ox + (int)std::floor(ix1 * z + 1e-7);
    int dy0 = oy + (int)std::floor(iy0 * z + 1e-7), dy1 = oy + (int)std::floor(iy1 * z + 1e-7);
    DrawPixmap(hdc, src, dx0, dy0, dx1 - dx0, dy1 - dy0, ix0, iy0, ix1 - ix0, iy1 - iy0, z < 1.0);
}

void DashedRect(HDC dc, RECT r, COLORREF dash) {
    HPEN pen = CreatePen(PS_DOT, 1, dash);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    int obk = SetBkMode(dc, OPAQUE);
    COLORREF oc = SetBkColor(dc, RGB(255, 255, 255));
    Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SetBkColor(dc, oc);
    SetBkMode(dc, obk);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
}

void DrawHandle(HDC dc, POINT c, COLORREF fill) {
    int hs = S(3);
    RECT r = {c.x - hs, c.y - hs, c.x + hs + 1, c.y + hs + 1};
    HBRUSH b = CreateSolidBrush(fill);
    FillRect(dc, &r, b);
    DeleteObject(b);
    HBRUSH fb = CreateSolidBrush(RGB(30, 60, 110));
    FrameRect(dc, &r, fb);
    DeleteObject(fb);
}

void PaintCanvas(HWND hwnd, HDC hdc) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int cw = std::max<int>(1, cr.right), ch = std::max<int>(1, cr.bottom);
    if (!memDC) memDC = CreateCompatibleDC(hdc);
    if (!memBmp || memW < cw || memH < ch) {
        if (memBmp) DeleteObject(memBmp);
        memW = std::max(cw, memW);
        memH = std::max(ch, memH);
        memBmp = CreateCompatibleBitmap(hdc, memW, memH);
        SelectObject(memDC, memBmp);
    }
    HDC dc = memDC;
    HBRUSH wb = CreateSolidBrush(kWorkspaceColor);
    FillRect(dc, &cr, wb);
    DeleteObject(wb);

    const Pixmap& src = useScratch ? scratch : g.img;
    int ox = ScrX(0), oy = ScrY(0);
    int ex = ScrX(src.w), ey = ScrY(src.h);

    // Schatten
    {
        HBRUSH sb = CreateSolidBrush(kShadowColor);
        RECT r1 = {ex, oy + S(4), ex + S(3), ey + S(3)};
        RECT r2 = {ox + S(4), ey, ex + S(3), ey + S(3)};
        FillRect(dc, &r1, sb);
        FillRect(dc, &r2, sb);
        DeleteObject(sb);
    }

    PaintImage(dc, src, ox, oy, cr);

    // Gitternetz
    if (g.showGrid && g.zoom >= 4.0) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
        HGDIOBJ op = SelectObject(dc, pen);
        int ix0 = std::max(0, (int)std::floor(ImgXf(0)));
        int ix1 = std::min(src.w, (int)std::ceil(ImgXf(cw)) + 1);
        int iy0 = std::max(0, (int)std::floor(ImgYf(0)));
        int iy1 = std::min(src.h, (int)std::ceil(ImgYf(ch)) + 1);
        int top = std::max(oy, 0), bottom = std::min(ey, ch);
        int left = std::max(ox, 0), right = std::min(ex, cw);
        for (int x = ix0; x <= ix1; ++x) {
            int X = ScrX(x);
            MoveToEx(dc, X, top, nullptr);
            LineTo(dc, X, bottom);
        }
        for (int y = iy0; y <= iy1; ++y) {
            int Y = ScrY(y);
            MoveToEx(dc, left, Y, nullptr);
            LineTo(dc, right, Y);
        }
        SelectObject(dc, op);
        DeleteObject(pen);
    }

    // Leinwand-Griffe
    if (drag.kind != DK_RESIZECANVAS) {
        HBRUSH hb = CreateSolidBrush(RGB(255, 255, 255));
        HBRUSH fb = CreateSolidBrush(RGB(85, 85, 85));
        for (int h = 1; h <= 3; ++h) {
            RECT r = CanvasHandleRect(h);
            FillRect(dc, &r, hb);
            FrameRect(dc, &r, fb);
        }
        DeleteObject(hb);
        DeleteObject(fb);
    } else {
        RECT r = {ox, oy, ScrX(resizeW), ScrY(resizeH)};
        DashedRect(dc, r, RGB(0, 0, 0));
    }

    // Auswahl / Lasso
    if (drag.kind == DK_SELRECT) {
        RECT r = NormRectIncl(drag.start, drag.cur);
        r.right += 1;
        r.bottom += 1;
        DashedRect(dc, ImgRectToScreen(r), RGB(0, 0, 0));
    }
    auto drawLasso = [&](const std::vector<POINT>& pts, bool closed) {
        if (pts.size() < 2) return;
        std::vector<POINT> sp;
        sp.reserve(pts.size() + 1);
        for (auto& p : pts) sp.push_back({ScrX(p.x + 0.5), ScrY(p.y + 0.5)});
        if (closed) sp.push_back(sp.front());
        HPEN pen = CreatePen(PS_DOT, 1, RGB(0, 0, 0));
        HGDIOBJ op = SelectObject(dc, pen);
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, RGB(255, 255, 255));
        Polyline(dc, sp.data(), (int)sp.size());
        SelectObject(dc, op);
        DeleteObject(pen);
    };
    if (drag.kind == DK_LASSO) drawLasso(sel.lasso, false);
    if (sel.active) {
        RECT r = SelScreenRect();
        if (!sel.floating && !sel.lasso.empty()) drawLasso(sel.lasso, true);
        RECT fr = {r.left - 1, r.top - 1, r.right + 1, r.bottom + 1};
        DashedRect(dc, fr, RGB(0, 0, 0));
        for (int h = 1; h <= 8; ++h) DrawHandle(dc, HandleCenter(fr, h), RGB(255, 255, 255));
    }

    // Textrahmen
    if (drag.kind == DK_TEXTRECT) {
        RECT r = NormRectIncl(drag.start, drag.cur);
        r.right += 1;
        r.bottom += 1;
        DashedRect(dc, ImgRectToScreen(r), RGB(0, 0, 0));
    }
    if (text.active) {
        RECT r = ImgRectToScreen(text.rc);
        InflateRect(&r, 1, 1);
        DashedRect(dc, r, RGB(0, 0, 0));
    }

    // Radiergummi-Umriss
    if (g.tool == T_ERASER && hoverValid) {
        int size = kEraserSizes[std::clamp(g.eraserSize, 0, 3)];
        int half = size / 2;
        POINT c = drag.kind == DK_PAINT ? drag.cur : hoverImg;
        RECT r = {ScrX(c.x - half), ScrY(c.y - half), ScrX(c.x - half + size), ScrY(c.y - half + size)};
        if (r.right - r.left < 3) InflateRect(&r, 1, 1);
        HBRUSH b1 = CreateSolidBrush(RGB(0, 0, 0));
        FrameRect(dc, &r, b1);
        DeleteObject(b1);
    }

    BitBlt(hdc, 0, 0, cw, ch, dc, 0, 0, SRCCOPY);
}

void ScrollTo(int x, int y) {
    scrollX = x;
    scrollY = y;
    Canvas_UpdateScroll();
    PositionTextEdit();
    Canvas_Invalidate();
}

LPCWSTR CursorForHandle(int h) {
    switch (h) {
    case 1: case 5: return IDC_SIZENWSE;
    case 3: case 7: return IDC_SIZENESW;
    case 2: case 6: return IDC_SIZENS;
    case 4: case 8: return IDC_SIZEWE;
    case 9: return IDC_SIZEALL;
    default: return IDC_ARROW;
    }
}

bool SetToolCursor(HWND hwnd) {
    POINT s;
    GetCursorPos(&s);
    ScreenToClient(hwnd, &s);
    LPCWSTR id = IDC_CROSS;
    if (drag.kind == DK_RESIZECANVAS) {
        id = drag.handle == 1 ? IDC_SIZEWE : drag.handle == 2 ? IDC_SIZENS : IDC_SIZENWSE;
    } else if (drag.kind == DK_SELSIZE) {
        id = CursorForHandle(drag.handle);
    } else if (drag.kind == DK_SELMOVE) {
        id = IDC_SIZEALL;
    } else {
        int sh = (g.tool == T_RECTSEL || g.tool == T_FREESEL) ? HitSelection(s) : 0;
        int ch = (drag.kind == DK_NONE && !poly.active && curve.phase == 0) ? HitCanvasHandle(s) : 0;
        if (sh) id = CursorForHandle(sh);
        else if (ch) id = ch == 1 ? IDC_SIZEWE : ch == 2 ? IDC_SIZENS : IDC_SIZENWSE;
        else if (g.tool == T_TEXT) id = IDC_IBEAM;
        else if (g.tool == T_ZOOM || g.tool == T_PICKER || g.tool == T_FILL) id = IDC_HAND;
        if (g.tool == T_ZOOM && !sh && !ch) id = IDC_CROSS;
    }
    SetCursor(LoadCursorW(nullptr, id));
    return true;
}

LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintCanvas(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        Canvas_UpdateScroll();
        PositionTextEdit();
        return 0;
    case WM_LBUTTONDOWN:
        OnButtonDown(hwnd, 0, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, wp);
        return 0;
    case WM_RBUTTONDOWN:
        OnButtonDown(hwnd, 1, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, wp);
        return 0;
    case WM_LBUTTONDBLCLK:
        if (g.tool == T_POLYGON && poly.active && drag.kind == DK_NONE) {
            FinishPolygon();
            return 0;
        }
        OnButtonDown(hwnd, 0, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, wp);
        return 0;
    case WM_RBUTTONDBLCLK:
        if (g.tool == T_POLYGON && poly.active && drag.kind == DK_NONE) {
            FinishPolygon();
            return 0;
        }
        OnButtonDown(hwnd, 1, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, wp);
        return 0;
    case WM_LBUTTONUP:
        OnButtonUp(0);
        return 0;
    case WM_RBUTTONUP:
        OnButtonUp(1);
        return 0;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        OnMouseMove(hwnd, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, wp);
        return 0;
    }
    case WM_MOUSELEAVE:
        hoverValid = false;
        UpdateStatusPos(0, 0, false);
        if (g.tool == T_ERASER) Canvas_Invalidate();
        return 0;
    case WM_CAPTURECHANGED:
        if (drag.kind != DK_NONE && (HWND)lp != hwnd) EndDrag(drag.kind != DK_PAINT);
        return 0;
    case WM_TIMER:
        if (wp == TIMER_AIR && drag.kind == DK_PAINT && g.tool == T_AIRBRUSH) {
            Spray(drag.cur);
            Canvas_Invalidate();
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) return SetToolCursor(hwnd);
        break;
    case WM_KEYDOWN:
        switch (wp) {
        case VK_ESCAPE:
            if (drag.kind != DK_NONE) {
                EndDrag(true);
                if (g.tool == T_CURVE) curve = CurveState();
            } else if (text.active) {
                CommitText();
            } else if (poly.active) {
                poly = PolyState();
            } else if (curve.phase > 0) {
                curve = CurveState();
            } else if (sel.active) {
                CommitSelection();
            }
            RebuildPreview();
            Canvas_Invalidate();
            return 0;
        case VK_RETURN:
            if (poly.active && drag.kind == DK_NONE) FinishPolygon();
            else if (curve.phase > 0 && drag.kind == DK_NONE) CommitCurve();
            return 0;
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
            if (sel.active && drag.kind == DK_NONE) {
                if (!sel.floating) LiftSelection(false);
                int step = (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 1;
                int dx = wp == VK_LEFT ? -step : wp == VK_RIGHT ? step : 0;
                int dy = wp == VK_UP ? -step : wp == VK_DOWN ? step : 0;
                OffsetRect(&sel.rc, dx, dy);
                MarkDirty();
                RebuildPreview();
                Canvas_Invalidate();
            } else {
                int step = S(40);
                if (wp == VK_LEFT) ScrollTo(scrollX - step, scrollY);
                if (wp == VK_RIGHT) ScrollTo(scrollX + step, scrollY);
                if (wp == VK_UP) ScrollTo(scrollX, scrollY - step);
                if (wp == VK_DOWN) ScrollTo(scrollX, scrollY + step);
            }
            return 0;
        }
        break;
    case WM_HSCROLL:
    case WM_VSCROLL: {
        int bar = msg == WM_HSCROLL ? SB_HORZ : SB_VERT;
        SCROLLINFO si = {sizeof(si), SIF_ALL};
        GetScrollInfo(hwnd, bar, &si);
        int pos = si.nPos;
        int line = S(32);
        switch (LOWORD(wp)) {
        case SB_LINEUP: pos -= line; break;
        case SB_LINEDOWN: pos += line; break;
        case SB_PAGEUP: pos -= (int)si.nPage; break;
        case SB_PAGEDOWN: pos += (int)si.nPage; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = si.nMax; break;
        default: return 0;
        }
        if (bar == SB_HORZ) ScrollTo(pos, scrollY);
        else ScrollTo(scrollX, pos);
        return 0;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        WORD keys = GET_KEYSTATE_WPARAM(wp);
        if (msg == WM_MOUSEWHEEL && (keys & MK_CONTROL)) {
            POINT s = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &s);
            static int accum = 0;
            accum += delta;
            while (accum >= WHEEL_DELTA) { Canvas_ZoomStep(1, &s); accum -= WHEEL_DELTA; }
            while (accum <= -WHEEL_DELTA) { Canvas_ZoomStep(-1, &s); accum += WHEEL_DELTA; }
            return 0;
        }
        int amount = MulDiv(delta, S(48), WHEEL_DELTA);
        if (msg == WM_MOUSEHWHEEL) ScrollTo(scrollX + amount, scrollY);
        else if (keys & MK_SHIFT) ScrollTo(scrollX - amount, scrollY);
        else ScrollTo(scrollX, scrollY - amount);
        if (drag.kind != DK_NONE) {
            POINT s;
            GetCursorPos(&s);
            ScreenToClient(hwnd, &s);
            OnMouseMove(hwnd, s, (GetKeyState(VK_SHIFT) & 0x8000) ? MK_SHIFT : 0);
        }
        return 0;
    }
    case WM_CTLCOLOREDIT:
        if ((HWND)lp == text.edit) {
            HDC dc = (HDC)wp;
            SetTextColor(dc, g.fg);
            SetBrushOrgEx(dc, 0, 0, nullptr);
            if (g.textOpaque) {
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, g.bg);
                if (!text.opaqueBrush) text.opaqueBrush = CreateSolidBrush(g.bg);
                return (LRESULT)text.opaqueBrush;
            }
            SetBkMode(dc, TRANSPARENT);
            if (!text.brush) RebuildTextBrush();
            return (LRESULT)(text.brush ? text.brush : GetStockObject(WHITE_BRUSH));
        }
        break;
    case WM_COMMAND:
        if ((HWND)lp == text.edit && text.edit) {
            if (HIWORD(wp) == EN_CHANGE) {
                GrowTextBox();
                InvalidateRect(text.edit, nullptr, TRUE);
                MarkDirty();
            } else if (HIWORD(wp) == EN_VSCROLL || HIWORD(wp) == EN_HSCROLL) {
                InvalidateRect(text.edit, nullptr, TRUE);
            }
            return 0;
        }
        break;
    case WM_DESTROY:
        if (memBmp) DeleteObject(memBmp);
        if (memDC) DeleteDC(memDC);
        memBmp = nullptr;
        memDC = nullptr;
        DestroyTextEdit();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

// ---------------------------------------------------------------------------
// Öffentliche Funktionen
// ---------------------------------------------------------------------------
bool Canvas_Register() {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = CanvasProc;
    wc.hInstance = g.hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    wc.lpszClassName = kCanvasClass;
    return RegisterClassExW(&wc) != 0;
}

HWND Canvas_Create(HWND parent) {
    return CreateWindowExW(0, kCanvasClass, L"", WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | WS_CLIPCHILDREN, 0,
                           0, 100, 100, parent, nullptr, g.hInst, nullptr);
}

void Canvas_Invalidate() {
    if (g.hCanvas) InvalidateRect(g.hCanvas, nullptr, FALSE);
}

void Canvas_UpdateScroll() {
    if (!g.hCanvas) return;
    for (int pass = 0; pass < 3; ++pass) {
        RECT cr;
        GetClientRect(g.hCanvas, &cr);
        int totalW = (int)std::ceil(g.img.w * g.zoom) + 2 * Margin() + S(8);
        int totalH = (int)std::ceil(g.img.h * g.zoom) + 2 * Margin() + S(8);
        int maxX = std::max(0, totalW - (int)cr.right);
        int maxY = std::max(0, totalH - (int)cr.bottom);
        scrollX = std::clamp(scrollX, 0, maxX);
        scrollY = std::clamp(scrollY, 0, maxY);
        SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
        si.nMin = 0;
        si.nMax = totalW - 1;
        si.nPage = (UINT)std::max<LONG>(0, cr.right);
        si.nPos = scrollX;
        SetScrollInfo(g.hCanvas, SB_HORZ, &si, TRUE);
        si.nMax = totalH - 1;
        si.nPage = (UINT)std::max<LONG>(0, cr.bottom);
        si.nPos = scrollY;
        SetScrollInfo(g.hCanvas, SB_VERT, &si, TRUE);
        RECT cr2;
        GetClientRect(g.hCanvas, &cr2);
        if (cr2.right == cr.right && cr2.bottom == cr.bottom) break;
    }
}

void Canvas_SetZoom(double z, const POINT* anchor) {
    z = std::clamp(z, 0.02, 32.0);
    RECT cr;
    GetClientRect(g.hCanvas, &cr);
    POINT a = anchor ? *anchor : POINT{cr.right / 2, cr.bottom / 2};
    double ix = ImgXf(a.x), iy = ImgYf(a.y);
    ix = std::clamp(ix, 0.0, (double)g.img.w);
    iy = std::clamp(iy, 0.0, (double)g.img.h);
    g.zoom = z;
    scrollX = (int)std::lround(ix * z) + Margin() - a.x;
    scrollY = (int)std::lround(iy * z) + Margin() - a.y;
    Canvas_UpdateScroll();
    PositionTextEdit();
    UpdateStatusInfo();
    Canvas_Invalidate();
}

void Canvas_ZoomStep(int dir, const POINT* anchor) {
    double z = g.zoom;
    double next = z;
    if (dir > 0) {
        for (int i = 0; i < kZoomLevelCount; ++i)
            if (kZoomLevels[i] > z + 1e-6) { next = kZoomLevels[i]; break; }
    } else {
        for (int i = kZoomLevelCount - 1; i >= 0; --i)
            if (kZoomLevels[i] < z - 1e-6) { next = kZoomLevels[i]; break; }
    }
    if (next != z) Canvas_SetZoom(next, anchor);
}

void Canvas_ZoomFit() {
    RECT cr;
    GetClientRect(g.hCanvas, &cr);
    double zx = (cr.right - 2.0 * Margin() - S(8)) / std::max(1, g.img.w);
    double zy = (cr.bottom - 2.0 * Margin() - S(8)) / std::max(1, g.img.h);
    double z = std::clamp(std::min(zx, zy), kZoomLevels[0], kZoomLevels[kZoomLevelCount - 1]);
    g.zoom = z;
    scrollX = scrollY = 0;
    Canvas_UpdateScroll();
    PositionTextEdit();
    UpdateStatusInfo();
    Canvas_Invalidate();
}

void Canvas_ResetView() {
    scrollX = scrollY = 0;
    g.zoom = 1.0;
    Canvas_UpdateScroll();
    UpdateStatusInfo();
    Canvas_Invalidate();
}

void Canvas_CommitAll() {
    if (drag.kind != DK_NONE) EndDrag(drag.kind != DK_PAINT);
    CommitText();
    if (poly.active) FinishPolygon();
    if (curve.phase > 0) CommitCurve();
    CommitSelection();
    RebuildPreview();
    Canvas_Invalidate();
}

bool Canvas_CancelAll() {
    bool pendingOnly = (poly.active || curve.phase > 0) && !sel.floating && drag.kind == DK_NONE;
    if (drag.kind != DK_NONE) EndDrag(true);
    DiscardText();
    poly = PolyState();
    curve = CurveState();
    ClearSelectionState();
    RebuildPreview();
    Canvas_Invalidate();
    return pendingOnly;
}

bool Canvas_HasSelection() { return sel.active; }

void Canvas_SelectAll() {
    Canvas_CommitAll();
    if (g.tool != T_RECTSEL && g.tool != T_FREESEL) SetTool(T_RECTSEL);
    sel = SelState();
    sel.active = true;
    sel.rc = {0, 0, g.img.w, g.img.h};
    UpdateStatusSel(g.img.w, g.img.h, true);
    Canvas_Invalidate();
}

bool Canvas_GetSelectionPixmap(Pixmap& out, bool flatten) {
    if (!sel.active) return false;
    Pixmap c;
    if (sel.floating) {
        c = FloatingContent();
    } else {
        int rw = RW(sel.rc), rh = RH(sel.rc);
        c = Pixmap(rw, rh, 0);
        for (int y = 0; y < rh; ++y)
            for (int x = 0; x < rw; ++x) {
                int ix = sel.rc.left + x, iy = sel.rc.top + y;
                if (!g.img.in(ix, iy)) continue;
                if (!sel.mask.empty() && !sel.mask[(size_t)y * rw + x]) continue;
                c.at(x, y) = g.img.at(ix, iy) | 0xFF000000u;
            }
    }
    if (flatten) {
        uint32_t bgp = RGBtoPX(g.bg);
        for (auto& v : c.px)
            if ((v >> 24) < 128) v = bgp;
            else v |= 0xFF000000u;
    }
    out = std::move(c);
    return true;
}

void Canvas_Copy() {
    if (text.active && GetFocus() == text.edit) {
        SendMessageW(text.edit, WM_COPY, 0, 0);
        return;
    }
    Pixmap c;
    if (Canvas_GetSelectionPixmap(c, true)) CopyPixmapToClipboard(g.hMain, c);
}

void Canvas_DeleteSelection() {
    if (!sel.active) return;
    if (sel.floating) {
        ClearSelectionState();
        MarkDirty();
    } else {
        PushUndo();
        int rw = RW(sel.rc), rh = RH(sel.rc);
        uint32_t bgp = RGBtoPX(g.bg);
        for (int y = 0; y < rh; ++y)
            for (int x = 0; x < rw; ++x) {
                int ix = sel.rc.left + x, iy = sel.rc.top + y;
                if (!g.img.in(ix, iy)) continue;
                if (!sel.mask.empty() && !sel.mask[(size_t)y * rw + x]) continue;
                g.img.at(ix, iy) = bgp;
            }
        ClearSelectionState();
        MarkDirty();
    }
    RebuildPreview();
    Canvas_Invalidate();
}

void Canvas_Cut() {
    if (text.active && GetFocus() == text.edit) {
        SendMessageW(text.edit, WM_CUT, 0, 0);
        return;
    }
    if (!sel.active) return;
    Canvas_Copy();
    Canvas_DeleteSelection();
}

void Canvas_PastePixmap(Pixmap&& p) {
    if (p.empty()) return;
    Canvas_CommitAll();
    if (p.w > g.img.w || p.h > g.img.h) {
        int r = MsgBox(g.hMain,
                       L"Das einzufügende Bild ist größer als die Leinwand.\n\nSoll die Leinwand vergrößert werden?",
                       MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return;
        if (r == IDYES) {
            ReplaceImage(ExtendCanvas(g.img, std::max(g.img.w, p.w), std::max(g.img.h, p.h), RGBtoPX(g.bg)));
        }
    }
    if (g.tool != T_RECTSEL && g.tool != T_FREESEL) SetTool(T_RECTSEL);
    PushUndo();
    int ix = std::max(0, (int)std::ceil(ImgXf(0)));
    int iy = std::max(0, (int)std::ceil(ImgYf(0)));
    if (ix + p.w > g.img.w) ix = std::max(0, g.img.w - p.w);
    if (iy + p.h > g.img.h) iy = std::max(0, g.img.h - p.h);
    sel = SelState();
    sel.active = true;
    sel.floating = true;
    MakeOpaque(p);
    sel.rc = {ix, iy, ix + p.w, iy + p.h};
    sel.pix = std::move(p);
    UpdateStatusSel(RW(sel.rc), RH(sel.rc), true);
    MarkDirty();
    RebuildPreview();
    Canvas_Invalidate();
}

void Canvas_Crop() {
    if (!sel.active) return;
    Pixmap c;
    Canvas_GetSelectionPixmap(c, true);
    bool wasFloating = sel.floating;
    ClearSelectionState();
    if (wasFloating && !g.undo.empty()) {
        g.img = std::move(g.undo.back());
        g.undo.pop_back();
    }
    ReplaceImage(std::move(c));
    RebuildPreview();
}

static Pixmap ApplyTransform(const Pixmap& p, Transform t) {
    switch (t) {
    case TR_FLIPH: return FlipH(p);
    case TR_FLIPV: return FlipV(p);
    case TR_ROT90: return Rotate90(p);
    case TR_ROT180: return Rotate180(p);
    case TR_ROT270: return Rotate270(p);
    case TR_INVERT: {
        Pixmap c = p;
        InvertColors(c);
        return c;
    }
    }
    return p;
}

void Canvas_Transform(Transform t) {
    CommitText();
    if (poly.active) FinishPolygon();
    if (curve.phase > 0) CommitCurve();
    if (sel.active) {
        if (!sel.floating) LiftSelection(false);
        Pixmap c = ApplyTransform(FloatingContent(), t);
        sel.rc.right = sel.rc.left + c.w;
        sel.rc.bottom = sel.rc.top + c.h;
        sel.pix = std::move(c);
        UpdateStatusSel(RW(sel.rc), RH(sel.rc), true);
        MarkDirty();
        RebuildPreview();
        Canvas_Invalidate();
    } else {
        ReplaceImage(ApplyTransform(g.img, t));
    }
}

void Canvas_ResizeSkew(bool percent, double h, double v, double skH, double skV) {
    CommitText();
    if (poly.active) FinishPolygon();
    if (curve.phase > 0) CommitCurve();
    int w0, h0;
    Canvas_GetTargetSize(w0, h0);
    int nw = percent ? (int)std::lround(w0 * h / 100.0) : (int)std::lround(h);
    int nh = percent ? (int)std::lround(h0 * v / 100.0) : (int)std::lround(v);
    nw = std::max(1, nw);
    nh = std::max(1, nh);
    if (!SizeAllowed(nw, nh)) {
        MsgBox(g.hMain, L"Die angegebene Größe ist zu groß.", MB_ICONWARNING);
        return;
    }
    bool resize = nw != w0 || nh != h0;
    bool skew = std::fabs(skH) > 1e-9 || std::fabs(skV) > 1e-9;
    if (!resize && !skew) return;
    if (sel.active) {
        if (!sel.floating) LiftSelection(false);
        Pixmap c = FloatingContent();
        if (resize) c = ResizeHQ(c, nw, nh);
        if (skew) c = Skew(c, skH, skV, 0);
        sel.rc.right = sel.rc.left + c.w;
        sel.rc.bottom = sel.rc.top + c.h;
        sel.pix = std::move(c);
        UpdateStatusSel(RW(sel.rc), RH(sel.rc), true);
        MarkDirty();
        RebuildPreview();
        Canvas_Invalidate();
    } else {
        Pixmap c = resize ? ResizeHQ(g.img, nw, nh) : g.img;
        if (resize) MakeOpaque(c);
        if (skew) c = Skew(c, skH, skV, RGBtoPX(g.bg));
        if (!SizeAllowed(c.w, c.h)) return;
        ReplaceImage(std::move(c));
    }
}

void Canvas_GetTargetSize(int& w, int& h) {
    if (sel.active) {
        w = RW(sel.rc);
        h = RH(sel.rc);
    } else {
        w = g.img.w;
        h = g.img.h;
    }
}

void Canvas_OnToolChanged() {
    bool keepSel = (g.tool == T_RECTSEL || g.tool == T_FREESEL) && sel.active;
    if (drag.kind != DK_NONE) EndDrag(true);
    CommitText();
    if (poly.active) FinishPolygon();
    if (curve.phase > 0) CommitCurve();
    if (!keepSel) CommitSelection();
    RebuildPreview();
    Canvas_Invalidate();
}

void Canvas_OnFontChanged() {
    if (text.active) {
        PositionTextEdit();
        GrowTextBox();
        Canvas_Invalidate();
    }
}

void Canvas_OnColorsChanged() {
    if (text.active && text.edit) {
        RebuildTextBrush();
        InvalidateRect(text.edit, nullptr, TRUE);
    }
    if (sel.floating && g.transparentSel) RebuildPreview();
    Canvas_Invalidate();
}

void Canvas_OnDpiChanged() {
    Canvas_UpdateScroll();
    PositionTextEdit();
    Canvas_Invalidate();
}

bool Canvas_IsTextActive() { return text.active; }
HWND Canvas_TextEdit() { return text.edit; }
