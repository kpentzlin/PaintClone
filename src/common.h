// PaintClone – gemeinsame Deklarationen
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objidl.h>

#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "resource.h"

using std::wstring;

// ---------------------------------------------------------------------------
// Pixel / Farben
// Pixel im Speicher: 0xAARRGGBB (entspricht BGRA im DIB / GDI+ 32bppARGB)
// ---------------------------------------------------------------------------
inline uint32_t RGBtoPX(COLORREF c) {
    return 0xFF000000u | ((uint32_t)GetRValue(c) << 16) | ((uint32_t)GetGValue(c) << 8) | GetBValue(c);
}
inline COLORREF PXtoRGB(uint32_t p) {
    return RGB((p >> 16) & 255, (p >> 8) & 255, p & 255);
}
inline Gdiplus::Color GpColor(COLORREF c) {
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

#include "algo.h"


// ---------------------------------------------------------------------------
// Werkzeuge und Optionen
// ---------------------------------------------------------------------------
enum Tool {
    T_FREESEL, T_RECTSEL,
    T_ERASER, T_FILL,
    T_PICKER, T_ZOOM,
    T_PENCIL, T_BRUSH,
    T_AIRBRUSH, T_TEXT,
    T_LINE, T_CURVE,
    T_RECT, T_POLYGON,
    T_ELLIPSE, T_ROUNDRECT,
    T_SHAPE,
    T_COUNT
};

enum ShapeKind {
    SH_TRIANGLE, SH_RTRIANGLE, SH_DIAMOND,
    SH_PENTAGON, SH_HEXAGON, SH_OCTAGON,
    SH_ARROW_R, SH_ARROW_L, SH_ARROW_U,
    SH_ARROW_D, SH_STAR4, SH_STAR5,
    SH_STAR6, SH_HEART, SH_LIGHTNING,
    SH_CALLOUT_RECT, SH_CALLOUT_OVAL, SH_CROSS,
    SH_COUNT
};

enum FillStyle { FS_OUTLINE = 0, FS_BOTH = 1, FS_FILL = 2 };

extern const wchar_t* const kToolNames[T_COUNT];
extern const wchar_t* const kToolHints[T_COUNT];
extern const wchar_t* const kShapeNames[SH_COUNT];
extern const COLORREF kDefaultPalette[28];
extern const double kZoomLevels[];
extern const int kZoomLevelCount;

inline const int kEraserSizes[4] = {4, 6, 8, 10};
inline const int kBrushSizes[3] = {8, 5, 2};
inline const int kAirSizes[3] = {4, 8, 12};   // Radius

// ---------------------------------------------------------------------------
// Anwendungszustand
// ---------------------------------------------------------------------------
struct App {
    HINSTANCE hInst = nullptr;
    HWND hMain = nullptr, hCanvas = nullptr, hToolbox = nullptr, hPalette = nullptr;
    HWND hStatus = nullptr, hFontBar = nullptr;
    HMENU hRecentMenu = nullptr;
    HACCEL hAccel = nullptr;
    UINT dpi = 96;
    HFONT uiFont = nullptr;

    Pixmap img;
    uint64_t imgVersion = 0;  // wird bei jeder Bildänderung erhöht
    wstring filePath;
    bool dirty = false;

    Tool tool = T_PENCIL, prevTool = T_PENCIL;
    COLORREF fg = RGB(0, 0, 0), bg = RGB(255, 255, 255);
    COLORREF palette[28];
    COLORREF custom[16];

    int lineWidth = 1;          // 1..5
    int brushShape = BR_CIRCLE; // BrushShape
    int brushSize = 1;          // Index in kBrushSizes
    int eraserSize = 1;         // Index in kEraserSizes
    int airSize = 1;            // Index in kAirSizes
    int fillStyle = FS_OUTLINE;
    int shape = SH_STAR5;
    bool transparentSel = false;
    bool smoothing = true;

    bool showToolbox = true, showPalette = true, showStatus = true, showTextBar = true;
    bool showGrid = false;
    double zoom = 1.0;

    wstring fontName = L"Arial";
    int fontSize = 12;
    bool fontBold = false, fontItalic = false, fontUnderline = false, fontStrike = false;
    bool textOpaque = false;

    int newW = 800, newH = 600;

    std::vector<wstring> recent;
    wstring lastDir;

    std::vector<Pixmap> undo, redo;

    HGLOBAL hDevMode = nullptr, hDevNames = nullptr;
    RECT printMargins = {2000, 2000, 2000, 2000}; // 1/100 mm
};

extern App g;

// DPI-Skalierung (logische 96-DPI-Pixel -> Bildschirmpixel)
inline int S(int v) { return MulDiv(v, (int)g.dpi, 96); }
inline float SF(float v) { return v * (float)g.dpi / 96.0f; }

// ---------------------------------------------------------------------------
// image.cpp
// ---------------------------------------------------------------------------
std::unique_ptr<Gdiplus::Bitmap> WrapBitmap(Pixmap& p);
Pixmap ResizeHQ(const Pixmap& s, int nw, int nh);
void DrawPixmap(HDC hdc, const Pixmap& p, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, bool halftone);

bool LoadImageFile(const wstring& path, Pixmap& out, wstring& err);
bool SaveImageFile(const wstring& path, const Pixmap& p, wstring& err);
bool CopyPixmapToClipboard(HWND hwnd, const Pixmap& p);
bool ClipboardHasImage();
bool PastePixmapFromClipboard(HWND hwnd, Pixmap& out);

// ---------------------------------------------------------------------------
// shapes.cpp – Zeichenprimitive
// ---------------------------------------------------------------------------
void DrawStraightLine(Pixmap& p, POINT a, POINT b, int width, COLORREF c, bool smooth);
void DrawBezier(Pixmap& p, POINT p0, POINT c1, POINT c2, POINT p3, int width, COLORREF c, bool smooth);
// Rechteck in Bildkoordinaten, inklusive Endpunkte
void DrawShape(Pixmap& p, int tool, int shape, RECT r, COLORREF outline, COLORREF fill,
               int style, int width, bool smooth);
void DrawPolygonShape(Pixmap& p, const std::vector<POINT>& pts, COLORREF outline, COLORREF fill,
                      int style, int width, bool smooth);
void AddShapePath(Gdiplus::GraphicsPath& path, int shape, Gdiplus::RectF r);

// ---------------------------------------------------------------------------
// canvas.cpp
// ---------------------------------------------------------------------------
bool Canvas_Register();
HWND Canvas_Create(HWND parent);
void Canvas_Invalidate();
void Canvas_SetZoom(double z, const POINT* anchorClient = nullptr);
void Canvas_ZoomStep(int dir, const POINT* anchorClient = nullptr);
void Canvas_ZoomFit();
void Canvas_UpdateScroll();
void Canvas_ResetView();
void Canvas_CommitAll();      // schwebende Auswahl / Text / Polygon / Kurve übernehmen
bool Canvas_CancelAll();      // ohne Übernehmen verwerfen; true = nur Polygon/Kurve verworfen
bool Canvas_HasSelection();
void Canvas_SelectAll();
void Canvas_DeleteSelection();
void Canvas_Copy();
void Canvas_Cut();
void Canvas_PastePixmap(Pixmap&& p);
bool Canvas_GetSelectionPixmap(Pixmap& out, bool flattenToBg);
void Canvas_Crop();
// Transformation: auf Auswahl (falls vorhanden) oder ganzes Bild
enum Transform { TR_FLIPH, TR_FLIPV, TR_ROT90, TR_ROT180, TR_ROT270, TR_INVERT };
void Canvas_Transform(Transform t);
void Canvas_ResizeSkew(bool percent, double h, double v, double skH, double skV);
void Canvas_GetTargetSize(int& w, int& h);   // Auswahlgröße oder Bildgröße
void Canvas_OnToolChanged();
void Canvas_OnFontChanged();
void Canvas_OnColorsChanged();
void Canvas_OnDpiChanged();
bool Canvas_IsTextActive();
HWND Canvas_TextEdit();

// ---------------------------------------------------------------------------
// panels.cpp – Werkzeugkasten, Farbpalette, Schriftleiste
// ---------------------------------------------------------------------------
bool Panels_Register();
HWND Toolbox_Create(HWND parent);
HWND Palette_Create(HWND parent);
HWND FontBar_Create(HWND parent);
int Toolbox_PreferredWidth();
int Palette_PreferredHeight();
int FontBar_PreferredHeight();
void FontBar_Sync();
void Panels_OnDpiChanged();
void DrawToolIcon(Gdiplus::Graphics& gr, int tool, float x, float y, float s);

// ---------------------------------------------------------------------------
// dialogs.cpp
// ---------------------------------------------------------------------------
void Dlg_ResizeSkew(HWND owner);
void Dlg_Attributes(HWND owner);
void Dlg_FlipRotate(HWND owner);
void Dlg_About(HWND owner);
void DoPrint(HWND owner);
void DoPageSetup(HWND owner);
void SetAsWallpaper(HWND owner, int style); // 0=Füllen 1=Kacheln 2=Zentriert
void ShowFullScreen(HWND owner);
bool EditColor(HWND owner, COLORREF& c);

// ---------------------------------------------------------------------------
// main.cpp
// ---------------------------------------------------------------------------
void PushUndo();
void DropLastUndo();
void MarkDirty();
void UpdateTitle();
void SetStatusHint(const wchar_t* text);
void UpdateStatusPos(int x, int y, bool valid);
void UpdateStatusSel(int w, int h, bool valid);
void UpdateStatusInfo();
void SetTool(Tool t);
void SetColors(COLORREF fg, COLORREF bg);
void LayoutMain();
void ReplaceImage(Pixmap&& p);   // ersetzt Bild (mit Undo) – Größe kann sich ändern
int MsgBox(HWND owner, const wstring& text, UINT flags);
