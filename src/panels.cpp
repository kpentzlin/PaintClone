// PaintClone – Werkzeugkasten, Optionen, Farbpalette, Schriftleiste
#include "common.h"

using namespace Gdiplus;

namespace {

const wchar_t* kToolboxClass = L"PaintCloneToolbox";
const wchar_t* kPaletteClass = L"PaintClonePalette";
const wchar_t* kFontBarClass = L"PaintCloneFontBar";

const COLORREF kSelFill = RGB(204, 228, 247);
const COLORREF kSelBorder = RGB(0, 120, 215);
const COLORREF kHotFill = RGB(229, 243, 255);
const COLORREF kHotBorder = RGB(153, 209, 255);
const COLORREF kIconInk = RGB(50, 50, 50);

// ---------------------------------------------------------------------------
// Werkzeugkasten-Geometrie (logische Pixel)
// ---------------------------------------------------------------------------
const int TB_PAD = 4, TB_BTN = 26, TB_GAP = 2;
const int TB_COLS = 2;
int ToolRows() { return (T_COUNT + TB_COLS - 1) / TB_COLS; }

RECT ToolRect(int t) {
    int col = t % TB_COLS, row = t / TB_COLS;
    int x = TB_PAD + col * (TB_BTN + TB_GAP);
    int y = TB_PAD + row * (TB_BTN + TB_GAP);
    return {S(x), S(y), S(x + TB_BTN), S(y + TB_BTN)};
}

int OptionsTop() { return TB_PAD + ToolRows() * (TB_BTN + TB_GAP) + 6; }
int OptionsWidth() { return TB_COLS * TB_BTN + (TB_COLS - 1) * TB_GAP; }

enum OptKind { OK_SELTRANS, OK_ERASER, OK_ZOOM, OK_BRUSH, OK_AIR, OK_WIDTH, OK_FILL, OK_SHAPE, OK_TEXTOPAQUE };

struct OptCell {
    RECT rc;  // Bildschirmpixel
    OptKind kind;
    int value;
};

std::vector<OptCell> BuildOptionCells(int tool) {
    std::vector<OptCell> v;
    int x0 = TB_PAD, y = OptionsTop() + 3, w = OptionsWidth();
    auto add = [&](int lx, int ly, int lw, int lh, OptKind k, int val) {
        v.push_back({{S(lx), S(ly), S(lx + lw), S(ly + lh)}, k, val});
    };
    auto stack = [&](int count, int h, OptKind k) {
        for (int i = 0; i < count; ++i) add(x0 + 2, y + i * h, w - 4, h, k, i);
        y += count * h + 4;
    };
    auto widths = [&](int h) {
        for (int i = 0; i < 5; ++i) add(x0 + 2, y + i * h, w - 4, h, OK_WIDTH, i + 1);
        y += 5 * h + 4;
    };
    switch (tool) {
    case T_RECTSEL:
    case T_FREESEL: stack(2, 26, OK_SELTRANS); break;
    case T_TEXT: stack(2, 26, OK_TEXTOPAQUE); break;
    case T_ERASER: stack(4, 18, OK_ERASER); break;
    case T_ZOOM: stack(5, 16, OK_ZOOM); break;
    case T_AIRBRUSH: stack(3, 22, OK_AIR); break;
    case T_BRUSH: {
        int cw = (w - 4) / 3;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 3; ++c) add(x0 + 2 + c * cw, y + r * 16, cw, 16, OK_BRUSH, r * 3 + c);
        y += 4 * 16 + 4;
        break;
    }
    case T_LINE:
    case T_CURVE: widths(14); break;
    case T_RECT:
    case T_ELLIPSE:
    case T_ROUNDRECT:
    case T_POLYGON:
        stack(3, 18, OK_FILL);
        widths(12);
        break;
    case T_SHAPE: {
        int cw = (w - 4) / 3;
        int rows = (SH_COUNT + 2) / 3;
        for (int i = 0; i < SH_COUNT; ++i) add(x0 + 2 + (i % 3) * cw, y + (i / 3) * cw, cw, cw, OK_SHAPE, i);
        y += rows * cw + 4;
        for (int i = 0; i < 3; ++i) add(x0 + 2 + i * cw, y, cw, 16, OK_FILL, i);
        y += 16 + 4;
        widths(10);
        break;
    }
    default: break;
    }
    return v;
}

bool CellSelected(const OptCell& c) {
    switch (c.kind) {
    case OK_SELTRANS: return (c.value == 1) == g.transparentSel;
    case OK_TEXTOPAQUE: return (c.value == 0) == g.textOpaque;
    case OK_ERASER: return g.eraserSize == c.value;
    case OK_ZOOM: {
        const int z[5] = {1, 2, 4, 6, 8};
        return std::fabs(g.zoom - z[c.value]) < 1e-6;
    }
    case OK_BRUSH: return g.brushShape == c.value / 3 && g.brushSize == c.value % 3;
    case OK_AIR: return g.airSize == c.value;
    case OK_WIDTH: return g.lineWidth == c.value;
    case OK_FILL: return g.fillStyle == c.value;
    case OK_SHAPE: return g.shape == c.value;
    }
    return false;
}

void ApplyCell(const OptCell& c) {
    switch (c.kind) {
    case OK_SELTRANS:
        g.transparentSel = c.value == 1;
        Canvas_OnColorsChanged();
        break;
    case OK_TEXTOPAQUE:
        g.textOpaque = c.value == 0;
        Canvas_OnColorsChanged();
        break;
    case OK_ERASER: g.eraserSize = c.value; break;
    case OK_ZOOM: {
        const int z[5] = {1, 2, 4, 6, 8};
        Canvas_SetZoom(z[c.value]);
        break;
    }
    case OK_BRUSH:
        g.brushShape = c.value / 3;
        g.brushSize = c.value % 3;
        break;
    case OK_AIR: g.airSize = c.value; break;
    case OK_WIDTH: g.lineWidth = c.value; break;
    case OK_FILL: g.fillStyle = c.value; break;
    case OK_SHAPE: g.shape = c.value; break;
    }
}

int hotTool = -1;
int hotCell = -1;
HWND hTip = nullptr;

void FillRectColor(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void FrameRectColor(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FrameRect(dc, &r, b);
    DeleteObject(b);
}

void DrawCellContent(Graphics& gr, const OptCell& c, bool selected) {
    RectF r((REAL)c.rc.left, (REAL)c.rc.top, (REAL)(c.rc.right - c.rc.left), (REAL)(c.rc.bottom - c.rc.top));
    Color ink = selected ? Color(255, 0, 60, 140) : GpColor(kIconInk);
    SolidBrush br(ink);
    float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
    switch (c.kind) {
    case OK_SELTRANS:
    case OK_TEXTOPAQUE: {
        bool opaque = (c.kind == OK_SELTRANS) ? c.value == 0 : c.value == 0;
        float s = std::min(r.Width, r.Height) - SF(6);
        RectF box(cx - s * 0.7f, cy - s / 2, s * 1.4f, s);
        if (opaque) {
            SolidBrush wb(Color(255, 255, 255, 255));
            gr.FillRectangle(&wb, box);
        } else {
            // Schachbrettmuster für Transparenz
            float q = SF(3);
            SolidBrush lb(Color(255, 220, 220, 220));
            SolidBrush wb(Color(255, 255, 255, 255));
            for (float yy = box.Y; yy < box.Y + box.Height; yy += q)
                for (float xx = box.X; xx < box.X + box.Width; xx += q) {
                    bool dark = ((int)((xx - box.X) / q) + (int)((yy - box.Y) / q)) % 2 == 0;
                    gr.FillRectangle(dark ? (Brush*)&lb : (Brush*)&wb, xx, yy, std::min(q, box.X + box.Width - xx),
                                     std::min(q, box.Y + box.Height - yy));
                }
        }
        Pen bp(Color(255, 120, 120, 120), 1);
        gr.DrawRectangle(&bp, box);
        SolidBrush shapeA(Color(255, 220, 60, 60));
        SolidBrush shapeB(Color(255, 40, 110, 210));
        gr.FillEllipse(&shapeA, box.X + box.Width * 0.12f, box.Y + box.Height * 0.15f, box.Height * 0.6f,
                       box.Height * 0.6f);
        PointF tri[3] = {PointF(box.X + box.Width * 0.62f, box.Y + box.Height * 0.2f),
                         PointF(box.X + box.Width * 0.9f, box.Y + box.Height * 0.85f),
                         PointF(box.X + box.Width * 0.35f, box.Y + box.Height * 0.85f)};
        gr.FillPolygon(&shapeB, tri, 3);
        break;
    }
    case OK_ERASER: {
        float s = SF((float)kEraserSizes[c.value]);
        gr.FillRectangle(&br, cx - s / 2, cy - s / 2, s, s);
        break;
    }
    case OK_ZOOM: {
        const wchar_t* t[5] = {L"1×", L"2×", L"4×", L"6×", L"8×"};
        FontFamily ff(L"Segoe UI");
        Font f(&ff, SF(11.0f), FontStyleRegular, UnitPixel);
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        gr.DrawString(t[c.value], -1, &f, r, &sf, &br);
        break;
    }
    case OK_BRUSH: {
        int shape = c.value / 3, size = kBrushSizes[c.value % 3];
        float s = SF((float)size) * 0.9f + 1;
        if (shape == BR_CIRCLE) gr.FillEllipse(&br, cx - s / 2, cy - s / 2, s, s);
        else if (shape == BR_SQUARE) gr.FillRectangle(&br, cx - s / 2, cy - s / 2, s, s);
        else {
            Pen p(ink, SF(1.3f));
            if (shape == BR_SLASH) gr.DrawLine(&p, cx - s / 2, cy + s / 2, cx + s / 2, cy - s / 2);
            else gr.DrawLine(&p, cx - s / 2, cy - s / 2, cx + s / 2, cy + s / 2);
        }
        break;
    }
    case OK_AIR: {
        float rad = SF((float)kAirSizes[c.value]) * 0.8f;
        unsigned seed = 7u + c.value * 13u;
        int n = 8 + c.value * 8;
        for (int i = 0; i < n; ++i) {
            seed = seed * 1103515245u + 12345u;
            float a = (seed % 6283) / 1000.0f;
            seed = seed * 1103515245u + 12345u;
            float d = std::sqrt((seed % 1000) / 1000.0f) * rad;
            float ps = SF(1.0f);
            gr.FillRectangle(&br, cx + d * std::cos(a) - ps / 2, cy + d * std::sin(a) - ps / 2, ps, ps);
        }
        break;
    }
    case OK_WIDTH: {
        float th = SF((float)c.value);
        gr.FillRectangle(&br, r.X + SF(5), cy - th / 2, r.Width - SF(10), th);
        break;
    }
    case OK_FILL: {
        float s = std::min(r.Width - SF(6), r.Height - SF(5));
        RectF box(cx - s * 0.9f, cy - s / 2, s * 1.8f, s);
        if (box.Width > r.Width - SF(4)) {
            box.Width = r.Width - SF(4);
            box.X = cx - box.Width / 2;
        }
        SolidBrush gray(Color(255, 150, 150, 150));
        Pen p(ink, SF(1.2f));
        if (c.value == FS_OUTLINE) gr.DrawRectangle(&p, box);
        else if (c.value == FS_BOTH) {
            gr.FillRectangle(&gray, box);
            gr.DrawRectangle(&p, box);
        } else {
            gr.FillRectangle(&gray, box);
        }
        break;
    }
    case OK_SHAPE: {
        float pad = SF(3);
        RectF sr(r.X + pad, r.Y + pad, r.Width - 2 * pad, r.Height - 2 * pad);
        GraphicsPath path;
        AddShapePath(path, c.value, sr);
        Pen p(ink, SF(1.0f));
        p.SetLineJoin(LineJoinRound);
        gr.DrawPath(&p, &path);
        break;
    }
    }
}

void PaintToolbox(HWND hwnd, HDC hdc) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    HDC dc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max<int>(1, cr.right), std::max<int>(1, cr.bottom));
    HGDIOBJ old = SelectObject(dc, bmp);
    FillRect(dc, &cr, GetSysColorBrush(COLOR_BTNFACE));

    for (int t = 0; t < T_COUNT; ++t) {
        RECT r = ToolRect(t);
        if (t == g.tool) {
            FillRectColor(dc, r, kSelFill);
            FrameRectColor(dc, r, kSelBorder);
        } else if (t == hotTool) {
            FillRectColor(dc, r, kHotFill);
            FrameRectColor(dc, r, kHotBorder);
        }
    }
    // Optionsbereich
    RECT orc = {S(TB_PAD), S(OptionsTop()), S(TB_PAD + OptionsWidth()), cr.bottom - S(TB_PAD)};
    if (orc.bottom > orc.top) {
        DrawEdge(dc, &orc, BDR_SUNKENOUTER, BF_RECT);
    }
    auto cells = BuildOptionCells(g.tool);
    for (size_t i = 0; i < cells.size(); ++i) {
        bool sel = CellSelected(cells[i]);
        if (sel) FillRectColor(dc, cells[i].rc, kSelFill);
        else if ((int)i == hotCell) FillRectColor(dc, cells[i].rc, kHotFill);
        if (sel) FrameRectColor(dc, cells[i].rc, kSelBorder);
    }
    {
        Graphics gr(dc);
        gr.SetSmoothingMode(SmoothingModeAntiAlias);
        gr.SetTextRenderingHint(TextRenderingHintAntiAlias);
        for (int t = 0; t < T_COUNT; ++t) {
            RECT r = ToolRect(t);
            float s = SF(20);
            float x = r.left + (r.right - r.left - s) / 2.0f;
            float y = r.top + (r.bottom - r.top - s) / 2.0f;
            DrawToolIcon(gr, t, x, y, s);
        }
        gr.SetSmoothingMode(SmoothingModeAntiAlias);
        for (auto& c : cells) DrawCellContent(gr, c, CellSelected(c));
    }
    BitBlt(hdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
}

void SetupToolTips(HWND hwnd) {
    if (hTip) DestroyWindow(hTip);
    hTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, g.hInst,
                           nullptr);
    for (int t = 0; t < T_COUNT; ++t) {
        TTTOOLINFOW ti = {};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = hwnd;
        ti.uId = (UINT_PTR)t + 1;
        ti.rect = ToolRect(t);
        ti.lpszText = const_cast<LPWSTR>(kToolNames[t]);
        SendMessageW(hTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
    SendMessageW(hTip, TTM_SETMAXTIPWIDTH, 0, S(300));
}

LRESULT CALLBACK ToolboxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetupToolTips(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintToolbox(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int ht = -1, hc = -1;
        for (int t = 0; t < T_COUNT; ++t) {
            RECT r = ToolRect(t);
            if (PtInRect(&r, p)) ht = t;
        }
        auto cells = BuildOptionCells(g.tool);
        for (size_t i = 0; i < cells.size(); ++i)
            if (PtInRect(&cells[i].rc, p)) hc = (int)i;
        if (ht != hotTool || hc != hotCell) {
            hotTool = ht;
            hotCell = hc;
            InvalidateRect(hwnd, nullptr, FALSE);
            if (ht >= 0) SetStatusHint(kToolHints[ht]);
            else if (hc >= 0 && cells[hc].kind == OK_SHAPE) SetStatusHint(kShapeNames[cells[hc].value]);
            else SetStatusHint(nullptr);
        }
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        hotTool = hotCell = -1;
        SetStatusHint(nullptr);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        for (int t = 0; t < T_COUNT; ++t) {
            RECT r = ToolRect(t);
            if (PtInRect(&r, p)) {
                SetTool((Tool)t);
                break;
            }
        }
        auto cells = BuildOptionCells(g.tool);
        for (auto& c : cells)
            if (PtInRect(&c.rc, p)) {
                ApplyCell(c);
                Canvas_OnColorsChanged();  // Vorschau mit neuen Optionen neu aufbauen
                break;
            }
        InvalidateRect(hwnd, nullptr, FALSE);
        if (Canvas_IsTextActive() && Canvas_TextEdit()) SetFocus(Canvas_TextEdit());
        else SetFocus(g.hCanvas);
        return 0;
    }
    case WM_DESTROY:
        if (hTip) DestroyWindow(hTip);
        hTip = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Farbpalette
// ---------------------------------------------------------------------------
const int PL_CELL = 16, PL_GAP = 2, PL_LEFT = 46;
HWND hEditColors = nullptr;

RECT PaletteCell(int i) {
    int col = i % 14, row = i / 14;
    int x = PL_LEFT + col * (PL_CELL + PL_GAP);
    int y = 5 + row * (PL_CELL + PL_GAP);
    return {S(x), S(y), S(x + PL_CELL), S(y + PL_CELL)};
}

RECT FgRect() { return {S(6), S(5), S(6 + 22), S(5 + 22)}; }
RECT BgRect() { return {S(16), S(15), S(16 + 22), S(15 + 22)}; }

void DrawColorSwatch(HDC dc, const RECT& r, COLORREF c) {
    FillRectColor(dc, r, c);
    RECT o = r;
    FrameRectColor(dc, o, RGB(120, 120, 120));
    InflateRect(&o, -1, -1);
    FrameRectColor(dc, o, RGB(255, 255, 255));
}

void PaintPalette(HWND hwnd, HDC hdc) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    HDC dc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max<int>(1, cr.right), std::max<int>(1, cr.bottom));
    HGDIOBJ old = SelectObject(dc, bmp);
    FillRect(dc, &cr, GetSysColorBrush(COLOR_BTNFACE));
    // Linie oben
    RECT line = {0, 0, cr.right, 1};
    FillRectColor(dc, line, GetSysColor(COLOR_3DSHADOW));

    DrawColorSwatch(dc, BgRect(), g.bg);
    DrawColorSwatch(dc, FgRect(), g.fg);
    for (int i = 0; i < 28; ++i) {
        RECT r = PaletteCell(i);
        FillRectColor(dc, r, g.palette[i]);
        FrameRectColor(dc, r, RGB(128, 128, 128));
        if (g.palette[i] == g.fg) {
            RECT o = r;
            InflateRect(&o, 1, 1);
            FrameRectColor(dc, o, kSelBorder);
        }
    }
    BitBlt(hdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
}

int PaletteHit(POINT p) {
    for (int i = 0; i < 28; ++i) {
        RECT r = PaletteCell(i);
        if (PtInRect(&r, p)) return i;
    }
    return -1;
}

void LayoutPalette(HWND hwnd) {
    if (!hEditColors) return;
    RECT last = PaletteCell(13);
    SetWindowPos(hEditColors, nullptr, last.right + S(12), S(9), S(130), S(26), SWP_NOZORDER);
    SendMessageW(hEditColors, WM_SETFONT, (WPARAM)g.uiFont, TRUE);
    (void)hwnd;
}

LRESULT CALLBACK PaletteProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        hEditColors = CreateWindowExW(0, L"BUTTON", L"Farben bearbeiten…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_COLORS_EDIT, g.hInst, nullptr);
        LayoutPalette(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintPalette(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_COLORS_EDIT) {
            SendMessageW(g.hMain, WM_COMMAND, ID_COLORS_EDIT, 0);
            SetFocus(g.hCanvas);
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int i = PaletteHit(p);
        if (i >= 0) {
            if (msg == WM_LBUTTONDOWN) SetColors(g.palette[i], g.bg);
            else SetColors(g.fg, g.palette[i]);
        } else {
            RECT a = FgRect(), b = BgRect();
            RECT u;
            UnionRect(&u, &a, &b);
            if (PtInRect(&u, p)) SetColors(g.bg, g.fg);
        }
        if (Canvas_IsTextActive() && Canvas_TextEdit()) SetFocus(Canvas_TextEdit());
        else SetFocus(g.hCanvas);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int i = PaletteHit(p);
        if (i >= 0) {
            COLORREF c = g.palette[i];
            if (EditColor(g.hMain, c)) {
                g.palette[i] = c;
                SetColors(c, g.bg);
            }
        } else {
            RECT a = FgRect(), b = BgRect();
            RECT u;
            UnionRect(&u, &a, &b);
            if (PtInRect(&u, p)) {
                SetColors(g.bg, g.fg);  // Tausch durch den ersten Klick rückgängig machen
                bool editFg = PtInRect(&a, p) != FALSE;
                COLORREF c = editFg ? g.fg : g.bg;
                if (EditColor(g.hMain, c)) {
                    if (editFg) SetColors(c, g.bg);
                    else SetColors(g.fg, c);
                }
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int i = PaletteHit(p);
        static wchar_t buf[200];
        if (i >= 0) {
            COLORREF c = g.palette[i];
            swprintf_s(buf, L"Farbe: Rot %d, Grün %d, Blau %d – Linksklick: Farbe 1, Rechtsklick: Farbe 2, "
                            L"Doppelklick: bearbeiten",
                       GetRValue(c), GetGValue(c), GetBValue(c));
            SetStatusHint(buf);
        } else {
            SetStatusHint(L"Klicken Sie auf die Farbfelder, um Vorder- und Hintergrundfarbe zu tauschen.");
        }
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        SetStatusHint(nullptr);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Schriftleiste
// ---------------------------------------------------------------------------
enum { IDC_FB_FONT = 3001, IDC_FB_SIZE, IDC_FB_BOLD, IDC_FB_ITALIC, IDC_FB_UNDER, IDC_FB_STRIKE };
HWND fbFont = nullptr, fbSize = nullptr, fbBold = nullptr, fbItalic = nullptr, fbUnder = nullptr, fbStrike = nullptr;
HFONT fbFontBold = nullptr, fbFontItalic = nullptr, fbFontUnder = nullptr, fbFontStrike = nullptr;
bool fbSyncing = false;

int CALLBACK EnumFontProc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM lp) {
    auto* names = reinterpret_cast<std::vector<wstring>*>(lp);
    if (lf->lfFaceName[0] != L'@') names->push_back(lf->lfFaceName);
    return 1;
}

void FillFontList() {
    std::vector<wstring> names;
    HDC dc = GetDC(nullptr);
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &lf, EnumFontProc, (LPARAM)&names, 0);
    ReleaseDC(nullptr, dc);
    std::sort(names.begin(), names.end(), [](const wstring& a, const wstring& b) {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    names.erase(std::unique(names.begin(), names.end()), names.end());
    SendMessageW(fbFont, WM_SETREDRAW, FALSE, 0);
    for (auto& n : names) SendMessageW(fbFont, CB_ADDSTRING, 0, (LPARAM)n.c_str());
    SendMessageW(fbFont, WM_SETREDRAW, TRUE, 0);
}

void CreateStyleFonts() {
    for (HFONT* f : {&fbFontBold, &fbFontItalic, &fbFontUnder, &fbFontStrike})
        if (*f) {
            DeleteObject(*f);
            *f = nullptr;
        }
    LOGFONTW base = {};
    GetObjectW(g.uiFont, sizeof(base), &base);
    LOGFONTW lf = base;
    lf.lfWeight = FW_BOLD;
    fbFontBold = CreateFontIndirectW(&lf);
    lf = base;
    lf.lfItalic = TRUE;
    fbFontItalic = CreateFontIndirectW(&lf);
    lf = base;
    lf.lfUnderline = TRUE;
    fbFontUnder = CreateFontIndirectW(&lf);
    lf = base;
    lf.lfStrikeOut = TRUE;
    fbFontStrike = CreateFontIndirectW(&lf);
}

void LayoutFontBar() {
    if (!fbFont) return;
    int h = S(24), y = S(3);
    int x = S(6);
    SetWindowPos(fbFont, nullptr, x, y, S(200), S(300), SWP_NOZORDER);
    x += S(206);
    SetWindowPos(fbSize, nullptr, x, y, S(56), S(300), SWP_NOZORDER);
    x += S(64);
    HWND btns[4] = {fbBold, fbItalic, fbUnder, fbStrike};
    for (HWND b : btns) {
        SetWindowPos(b, nullptr, x, y, S(26), h, SWP_NOZORDER);
        x += S(28);
    }
    SendMessageW(fbFont, WM_SETFONT, (WPARAM)g.uiFont, TRUE);
    SendMessageW(fbSize, WM_SETFONT, (WPARAM)g.uiFont, TRUE);
    CreateStyleFonts();
    SendMessageW(fbBold, WM_SETFONT, (WPARAM)fbFontBold, TRUE);
    SendMessageW(fbItalic, WM_SETFONT, (WPARAM)fbFontItalic, TRUE);
    SendMessageW(fbUnder, WM_SETFONT, (WPARAM)fbFontUnder, TRUE);
    SendMessageW(fbStrike, WM_SETFONT, (WPARAM)fbFontStrike, TRUE);
    SendMessageW(fbFont, CB_SETMINVISIBLE, 20, 0);
}

void ApplyFontBar() {
    if (fbSyncing) return;
    wchar_t buf[LF_FACESIZE + 1] = {};
    int sel = (int)SendMessageW(fbFont, CB_GETCURSEL, 0, 0);
    if (sel != CB_ERR) SendMessageW(fbFont, CB_GETLBTEXT, sel, (LPARAM)buf);
    else GetWindowTextW(fbFont, buf, LF_FACESIZE);
    if (buf[0]) g.fontName = buf;
    wchar_t sbuf[16] = {};
    int ssel = (int)SendMessageW(fbSize, CB_GETCURSEL, 0, 0);
    if (ssel != CB_ERR) SendMessageW(fbSize, CB_GETLBTEXT, ssel, (LPARAM)sbuf);
    else GetWindowTextW(fbSize, sbuf, 16);
    int sz = _wtoi(sbuf);
    if (sz >= 1 && sz <= 999) g.fontSize = sz;
    g.fontBold = SendMessageW(fbBold, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g.fontItalic = SendMessageW(fbItalic, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g.fontUnderline = SendMessageW(fbUnder, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g.fontStrike = SendMessageW(fbStrike, BM_GETCHECK, 0, 0) == BST_CHECKED;
    Canvas_OnFontChanged();
}

LRESULT CALLBACK FontBarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        fbFont = CreateWindowExW(0, WC_COMBOBOXW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL, 0, 0,
                                 10, 10, hwnd, (HMENU)(INT_PTR)IDC_FB_FONT, g.hInst, nullptr);
        fbSize = CreateWindowExW(0, WC_COMBOBOXW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL, 0, 0,
                                 10, 10, hwnd, (HMENU)(INT_PTR)IDC_FB_SIZE, g.hInst, nullptr);
        struct B { HWND* h; const wchar_t* t; int id; } btns[] = {
            {&fbBold, L"F", IDC_FB_BOLD}, {&fbItalic, L"K", IDC_FB_ITALIC},
            {&fbUnder, L"U", IDC_FB_UNDER}, {&fbStrike, L"S", IDC_FB_STRIKE}};
        for (auto& b : btns)
            *b.h = CreateWindowExW(0, L"BUTTON", b.t, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE, 0, 0, 10,
                                   10, hwnd, (HMENU)(INT_PTR)b.id, g.hInst, nullptr);
        FillFontList();
        const int sizes[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72, 96, 144};
        for (int s : sizes) {
            wchar_t t[8];
            swprintf_s(t, L"%d", s);
            SendMessageW(fbSize, CB_ADDSTRING, 0, (LPARAM)t);
        }
        LayoutFontBar();
        FontBar_Sync();
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT cr;
        GetClientRect(hwnd, &cr);
        FillRect((HDC)wp, &cr, GetSysColorBrush(COLOR_BTNFACE));
        RECT line = {0, cr.bottom - 1, cr.right, cr.bottom};
        FillRectColor((HDC)wp, line, GetSysColor(COLOR_3DSHADOW));
        return 1;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        bool apply = false;
        if (id == IDC_FB_FONT && (code == CBN_SELCHANGE || code == CBN_KILLFOCUS)) apply = true;
        if (id == IDC_FB_SIZE && (code == CBN_SELCHANGE || code == CBN_EDITCHANGE || code == CBN_KILLFOCUS))
            apply = true;
        if (id >= IDC_FB_BOLD && id <= IDC_FB_STRIKE && code == BN_CLICKED) apply = true;
        if ((id == IDC_FB_FONT || id == IDC_FB_SIZE) && code == CBN_CLOSEUP) {
            // Liste zugeklappt: Auswahl übernehmen und zurück ins Textfeld
            PostMessageW(hwnd, WM_APP + 1, 0, 0);
            return 0;
        }
        if (apply) {
            if (code == CBN_SELCHANGE || code == CBN_EDITCHANGE || code == CBN_KILLFOCUS) {
                // Auswahl erst nach der Nachricht im Eingabefeld sichtbar
                ApplyFontBar();
            } else {
                ApplyFontBar();
                if (Canvas_IsTextActive() && Canvas_TextEdit()) SetFocus(Canvas_TextEdit());
            }
            (void)0;
        }
        return 0;
    }
    case WM_APP + 1:
        ApplyFontBar();
        if (Canvas_IsTextActive() && Canvas_TextEdit()) SetFocus(Canvas_TextEdit());
        return 0;
    case WM_DESTROY:
        for (HFONT* f : {&fbFontBold, &fbFontItalic, &fbFontUnder, &fbFontStrike})
            if (*f) {
                DeleteObject(*f);
                *f = nullptr;
            }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

// ---------------------------------------------------------------------------
// Werkzeugsymbole (Vektor, 20×20 Einheiten)
// ---------------------------------------------------------------------------
void DrawToolIcon(Graphics& gr, int tool, float x, float y, float s) {
    auto P = [&](float u, float v) { return PointF(x + u * s / 20.0f, y + v * s / 20.0f); };
    float k = s / 20.0f;
    Color ink = GpColor(kIconInk);
    Pen pen(ink, 1.2f * k);
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    SolidBrush inkBr(ink);
    switch (tool) {
    case T_FREESEL: {
        Pen dp(ink, 1.2f * k);
        dp.SetDashStyle(DashStyleDash);
        GraphicsPath path;
        path.AddBezier(P(4, 6), P(8, 1), P(15, 3), P(16, 7));
        path.AddBezier(P(16, 7), P(18, 12), P(13, 12), P(14, 16));
        path.AddBezier(P(14, 16), P(15, 19), P(6, 19), P(5, 15));
        path.AddBezier(P(5, 15), P(4, 12), P(1, 10), P(4, 6));
        gr.DrawPath(&dp, &path);
        break;
    }
    case T_RECTSEL: {
        Pen dp(ink, 1.2f * k);
        dp.SetDashStyle(DashStyleDash);
        gr.DrawRectangle(&dp, RectF(P(3, 4).X, P(3, 4).Y, 14 * k, 12 * k));
        break;
    }
    case T_ERASER: {
        PointF body[4] = {P(3, 13), P(11, 5), P(17, 11), P(9, 19)};
        SolidBrush pink(Color(255, 245, 160, 180));
        gr.FillPolygon(&pink, body, 4);
        PointF tip[4] = {P(3, 13), P(6, 10), P(12, 16), P(9, 19)};
        SolidBrush white(Color(255, 250, 250, 250));
        gr.FillPolygon(&white, tip, 4);
        gr.DrawPolygon(&pen, body, 4);
        gr.DrawLine(&pen, P(6, 10), P(12, 16));
        break;
    }
    case T_FILL: {
        PointF bucket[4] = {P(3, 9), P(9, 3), P(16, 10), P(10, 16)};
        SolidBrush white(Color(255, 240, 240, 240));
        gr.FillPolygon(&white, bucket, 4);
        gr.DrawPolygon(&pen, bucket, 4);
        SolidBrush paint(Color(255, 30, 120, 220));
        PointF top[3] = {P(3, 9), P(9, 3), P(10, 4)};
        gr.FillPolygon(&paint, top, 3);
        gr.FillEllipse(&paint, RectF(P(15, 13).X, P(15, 13).Y, 3.5f * k, 4.5f * k));
        gr.DrawArc(&pen, RectF(P(6, 1).X, P(6, 1).Y, 7 * k, 7 * k), 200, 140);
        break;
    }
    case T_PICKER: {
        Pen body(ink, 2.6f * k);
        body.SetStartCap(LineCapRound);
        gr.DrawLine(&body, P(4, 16), P(11, 9));
        SolidBrush glass(Color(255, 190, 220, 245));
        Pen thin(ink, 1.0f * k);
        PointF tube[4] = {P(4.5f, 14.5f), P(11, 8), P(12.5f, 9.5f), P(6, 16)};
        gr.FillPolygon(&glass, tube, 4);
        gr.DrawPolygon(&thin, tube, 4);
        gr.FillEllipse(&inkBr, RectF(P(11, 3).X, P(11, 3).Y, 6 * k, 6 * k));
        Pen collar(ink, 2.0f * k);
        gr.DrawLine(&collar, P(10, 7), P(14, 11));
        gr.DrawLine(&pen, P(4.5f, 15.5f), P(2.5f, 17.5f));
        break;
    }
    case T_ZOOM: {
        Pen ring(ink, 1.8f * k);
        SolidBrush lens(Color(255, 220, 238, 252));
        gr.FillEllipse(&lens, RectF(P(3, 3).X, P(3, 3).Y, 10 * k, 10 * k));
        gr.DrawEllipse(&ring, RectF(P(3, 3).X, P(3, 3).Y, 10 * k, 10 * k));
        Pen handle(ink, 3.0f * k);
        handle.SetEndCap(LineCapRound);
        gr.DrawLine(&handle, P(12, 12), P(17, 17));
        break;
    }
    case T_PENCIL: {
        PointF body[4] = {P(6, 12), P(14, 4), P(17, 7), P(9, 15)};
        SolidBrush yellow(Color(255, 250, 200, 60));
        gr.FillPolygon(&yellow, body, 4);
        PointF tip[3] = {P(6, 12), P(9, 15), P(3.5f, 17.5f)};
        SolidBrush wood(Color(255, 240, 210, 160));
        gr.FillPolygon(&wood, tip, 3);
        gr.DrawPolygon(&pen, body, 4);
        gr.DrawPolygon(&pen, tip, 3);
        PointF lead[3] = {P(4.6f, 15.4f), P(5.4f, 16.6f), P(3.5f, 17.5f)};
        gr.FillPolygon(&inkBr, lead, 3);
        break;
    }
    case T_BRUSH: {
        Pen handle(Color(255, 150, 90, 40), 2.4f * k);
        handle.SetEndCap(LineCapRound);
        gr.DrawLine(&handle, P(10, 10), P(17, 3));
        SolidBrush ferrule(Color(255, 170, 170, 170));
        PointF fer[4] = {P(8, 10), P(10, 8), P(12, 10), P(10, 12)};
        gr.FillPolygon(&ferrule, fer, 4);
        GraphicsPath tip;
        tip.AddBezier(P(8, 10), P(4, 11), P(5, 15), P(3, 18));
        tip.AddBezier(P(3, 18), P(7, 17), P(10, 16), P(10, 12));
        tip.CloseFigure();
        SolidBrush paint(Color(255, 40, 40, 40));
        gr.FillPath(&paint, &tip);
        break;
    }
    case T_AIRBRUSH: {
        SolidBrush can(Color(255, 170, 180, 195));
        RectF body(P(3, 8).X, P(3, 8).Y, 7 * k, 10 * k);
        gr.FillRectangle(&can, body);
        gr.DrawRectangle(&pen, body);
        gr.FillRectangle(&inkBr, RectF(P(5, 5.5f).X, P(5, 5.5f).Y, 3 * k, 2.5f * k));
        const float dots[][2] = {{12, 4}, {14, 6}, {16, 3}, {13, 8}, {17, 7}, {15, 10}, {18, 4}, {12, 11}};
        for (auto& d : dots) gr.FillEllipse(&inkBr, RectF(P(d[0], d[1]).X, P(d[0], d[1]).Y, 1.3f * k, 1.3f * k));
        break;
    }
    case T_TEXT: {
        FontFamily ff(L"Times New Roman");
        Font f(&ff, 17 * k, FontStyleBold, UnitPixel);
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        gr.DrawString(L"A", -1, &f, RectF(x, y + 0.5f * k, s, s), &sf, &inkBr);
        break;
    }
    case T_LINE: {
        Pen p(ink, 1.6f * k);
        gr.DrawLine(&p, P(3, 17), P(17, 3));
        break;
    }
    case T_CURVE: {
        Pen p(ink, 1.6f * k);
        gr.DrawBezier(&p, P(2, 15), P(6, 0), P(13, 20), P(18, 5));
        break;
    }
    case T_RECT:
        gr.DrawRectangle(&pen, RectF(P(3, 5).X, P(3, 5).Y, 14 * k, 10 * k));
        break;
    case T_POLYGON: {
        PointF pts[5] = {P(3, 16), P(5, 5), P(11, 9), P(17, 4), P(15, 16)};
        gr.DrawPolygon(&pen, pts, 5);
        break;
    }
    case T_ELLIPSE:
        gr.DrawEllipse(&pen, RectF(P(2, 5).X, P(2, 5).Y, 16 * k, 10 * k));
        break;
    case T_ROUNDRECT: {
        GraphicsPath path;
        float r = 4 * k;
        RectF rc(P(3, 5).X, P(3, 5).Y, 14 * k, 10 * k);
        path.AddArc(rc.X, rc.Y, r, r, 180, 90);
        path.AddArc(rc.X + rc.Width - r, rc.Y, r, r, 270, 90);
        path.AddArc(rc.X + rc.Width - r, rc.Y + rc.Height - r, r, r, 0, 90);
        path.AddArc(rc.X, rc.Y + rc.Height - r, r, r, 90, 90);
        path.CloseFigure();
        gr.DrawPath(&pen, &path);
        break;
    }
    case T_SHAPE: {
        GraphicsPath path;
        AddShapePath(path, SH_STAR5, RectF(P(2, 2).X, P(2, 2).Y, 11 * k, 11 * k));
        SolidBrush fillA(Color(255, 250, 210, 80));
        gr.FillPath(&fillA, &path);
        gr.DrawPath(&pen, &path);
        GraphicsPath heart;
        AddShapePath(heart, SH_HEART, RectF(P(10, 10).X, P(10, 10).Y, 8 * k, 8 * k));
        SolidBrush fillB(Color(255, 230, 80, 90));
        gr.FillPath(&fillB, &heart);
        gr.DrawPath(&pen, &heart);
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Öffentliche Funktionen
// ---------------------------------------------------------------------------
bool Panels_Register() {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.hInstance = g.hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ToolboxProc;
    wc.lpszClassName = kToolboxClass;
    if (!RegisterClassExW(&wc)) return false;

    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = PaletteProc;
    wc.lpszClassName = kPaletteClass;
    if (!RegisterClassExW(&wc)) return false;

    wc.style = 0;
    wc.lpfnWndProc = FontBarProc;
    wc.lpszClassName = kFontBarClass;
    return RegisterClassExW(&wc) != 0;
}

HWND Toolbox_Create(HWND parent) {
    return CreateWindowExW(0, kToolboxClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, nullptr, g.hInst,
                           nullptr);
}

HWND Palette_Create(HWND parent) {
    return CreateWindowExW(0, kPaletteClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10, 10, parent,
                           nullptr, g.hInst, nullptr);
}

HWND FontBar_Create(HWND parent) {
    return CreateWindowExW(0, kFontBarClass, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10, parent, nullptr, g.hInst,
                           nullptr);
}

int Toolbox_PreferredWidth() { return S(TB_PAD * 2 + OptionsWidth()); }
int Palette_PreferredHeight() { return S(5 + 2 * (PL_CELL + PL_GAP) + 4); }
int FontBar_PreferredHeight() { return S(31); }

void FontBar_Sync() {
    if (!fbFont) return;
    fbSyncing = true;
    int idx = (int)SendMessageW(fbFont, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)g.fontName.c_str());
    if (idx != CB_ERR) SendMessageW(fbFont, CB_SETCURSEL, idx, 0);
    else SetWindowTextW(fbFont, g.fontName.c_str());
    wchar_t t[16];
    swprintf_s(t, L"%d", g.fontSize);
    int sidx = (int)SendMessageW(fbSize, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)t);
    if (sidx != CB_ERR) SendMessageW(fbSize, CB_SETCURSEL, sidx, 0);
    else SetWindowTextW(fbSize, t);
    SendMessageW(fbBold, BM_SETCHECK, g.fontBold ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(fbItalic, BM_SETCHECK, g.fontItalic ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(fbUnder, BM_SETCHECK, g.fontUnderline ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(fbStrike, BM_SETCHECK, g.fontStrike ? BST_CHECKED : BST_UNCHECKED, 0);
    fbSyncing = false;
}

void Panels_OnDpiChanged() {
    if (g.hToolbox) {
        SetupToolTips(g.hToolbox);
        InvalidateRect(g.hToolbox, nullptr, FALSE);
    }
    if (g.hPalette) {
        LayoutPalette(g.hPalette);
        InvalidateRect(g.hPalette, nullptr, FALSE);
    }
    if (g.hFontBar) LayoutFontBar();
}
