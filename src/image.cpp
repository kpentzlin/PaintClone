// PaintClone – Bildoperationen, Dateiformate, Zwischenablage
#include "common.h"

using namespace Gdiplus;

bool SizeAllowed(long long w, long long h) {
    return w >= 1 && h >= 1 && w <= MAX_DIM && h <= MAX_DIM && w * h <= MAX_PIXELS;
}

std::unique_ptr<Bitmap> WrapBitmap(Pixmap& p) {
    return std::make_unique<Bitmap>(p.w, p.h, p.w * 4, PixelFormat32bppARGB, (BYTE*)p.px.data());
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

Pixmap ResizeHQ(const Pixmap& s, int nw, int nh) {
    Pixmap src = s;  // GDI+ benötigt beschreibbaren Speicher
    Pixmap d(nw, nh, 0);
    {
        auto sb = WrapBitmap(src);
        auto db = WrapBitmap(d);
        Graphics gr(db.get());
        gr.SetCompositingMode(CompositingModeSourceCopy);
        bool enlargeOnly = nw >= s.w && nh >= s.h;
        bool integerScale = enlargeOnly && (nw % s.w == 0) && (nh % s.h == 0);
        gr.SetInterpolationMode(integerScale ? InterpolationModeNearestNeighbor
                                             : InterpolationModeHighQualityBicubic);
        gr.SetPixelOffsetMode(PixelOffsetModeHalf);
        ImageAttributes ia;
        ia.SetWrapMode(WrapModeTileFlipXY);
        gr.DrawImage(sb.get(), Rect(0, 0, nw, nh), 0, 0, s.w, s.h, UnitPixel, &ia);
    }
    return d;
}

// Neigen: zuerst horizontal (Zeilen verschieben), dann vertikal (Spalten verschieben)
static Pixmap SkewOne(const Pixmap& s, double deg, bool horizontal, uint32_t bgpx) {
    double t = std::tan(deg * 3.14159265358979323846 / 180.0);
    if (std::fabs(t) < 1e-9) return s;
    if (horizontal) {
        int extra = (int)std::ceil(std::fabs(t) * s.h);
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
        int extra = (int)std::ceil(std::fabs(t) * s.w);
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

// Zeichnet einen Ausschnitt eines Pixmaps skaliert in einen DC.
// Es wird stets ein vollständiges Zeilenband übergeben, um die bekannten
// Unklarheiten von StretchDIBits bei Top-down-DIBs zu vermeiden.
void DrawPixmap(HDC hdc, const Pixmap& p, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh,
                bool halftone) {
    if (p.empty() || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = p.w;
    bi.bmiHeader.biHeight = -sh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    int oldMode = SetStretchBltMode(hdc, halftone ? HALFTONE : COLORONCOLOR);
    if (halftone) SetBrushOrgEx(hdc, 0, 0, nullptr);
    StretchDIBits(hdc, dx, dy, dw, dh, sx, 0, sw, sh, p.px.data() + (size_t)sy * p.w, &bi, DIB_RGB_COLORS,
                  SRCCOPY);
    SetStretchBltMode(hdc, oldMode);
}

// ---------------------------------------------------------------------------
// Dateien
// ---------------------------------------------------------------------------
static bool GetEncoderClsid(const wchar_t* mime, CLSID* clsid) {
    UINT num = 0, size = 0;
    if (GetImageEncodersSize(&num, &size) != Ok || size == 0) return false;
    std::vector<BYTE> buf(size);
    auto* info = reinterpret_cast<ImageCodecInfo*>(buf.data());
    if (GetImageEncoders(num, size, info) != Ok) return false;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, mime) == 0) {
            *clsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

static wstring LowerExt(const wstring& path) {
    size_t dot = path.find_last_of(L'.');
    size_t slash = path.find_last_of(L"\\/");
    if (dot == wstring::npos || (slash != wstring::npos && dot < slash)) return L"";
    wstring e = path.substr(dot + 1);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

static void ApplyExifOrientation(Bitmap* b) {
    UINT size = b->GetPropertyItemSize(PropertyTagOrientation);
    if (size == 0) return;
    std::vector<BYTE> buf(size);
    auto* item = reinterpret_cast<PropertyItem*>(buf.data());
    if (b->GetPropertyItem(PropertyTagOrientation, size, item) != Ok) return;
    if (item->type != PropertyTagTypeShort || item->length < 2) return;
    unsigned short o = *reinterpret_cast<unsigned short*>(item->value);
    RotateFlipType rf = RotateNoneFlipNone;
    switch (o) {
    case 2: rf = RotateNoneFlipX; break;
    case 3: rf = Rotate180FlipNone; break;
    case 4: rf = Rotate180FlipX; break;
    case 5: rf = Rotate90FlipX; break;
    case 6: rf = Rotate90FlipNone; break;
    case 7: rf = Rotate270FlipX; break;
    case 8: rf = Rotate270FlipNone; break;
    default: return;
    }
    b->RotateFlip(rf);
}

bool LoadImageFile(const wstring& path, Pixmap& out, wstring& err) {
    std::unique_ptr<Bitmap> b(Bitmap::FromFile(path.c_str(), FALSE));
    if (!b || b->GetLastStatus() != Ok) {
        err = L"Die Datei konnte nicht gelesen werden. Es handelt sich nicht um ein gültiges Bild "
              L"oder das Format wird nicht unterstützt.";
        return false;
    }
    ApplyExifOrientation(b.get());
    UINT w = b->GetWidth(), h = b->GetHeight();
    if (!SizeAllowed(w, h)) {
        err = L"Das Bild ist zu groß.";
        return false;
    }
    Pixmap p((int)w, (int)h);
    BitmapData bd = {};
    Rect r(0, 0, (INT)w, (INT)h);
    if (b->LockBits(&r, ImageLockModeRead, PixelFormat32bppARGB, &bd) != Ok) {
        err = L"Die Bilddaten konnten nicht gelesen werden.";
        return false;
    }
    for (UINT y = 0; y < h; ++y) {
        const uint32_t* row = reinterpret_cast<const uint32_t*>(static_cast<const BYTE*>(bd.Scan0) + (INT_PTR)y * bd.Stride);
        uint32_t* dst = p.px.data() + (size_t)y * w;
        for (UINT x = 0; x < w; ++x) {
            uint32_t v = row[x];
            uint32_t a = v >> 24;
            if (a == 255) {
                dst[x] = v;
            } else {
                // auf Weiß verrechnen (wie Paint)
                uint32_t r8 = (v >> 16) & 255, g8 = (v >> 8) & 255, b8 = v & 255;
                r8 = (r8 * a + 255 * (255 - a) + 127) / 255;
                g8 = (g8 * a + 255 * (255 - a) + 127) / 255;
                b8 = (b8 * a + 255 * (255 - a) + 127) / 255;
                dst[x] = 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
            }
        }
    }
    b->UnlockBits(&bd);
    out = std::move(p);
    return true;
}

bool SaveImageFile(const wstring& path, const Pixmap& p, wstring& err) {
    wstring ext = LowerExt(path);
    const wchar_t* mime = L"image/png";
    if (ext == L"bmp" || ext == L"dib") mime = L"image/bmp";
    else if (ext == L"jpg" || ext == L"jpeg" || ext == L"jpe" || ext == L"jfif") mime = L"image/jpeg";
    else if (ext == L"gif") mime = L"image/gif";
    else if (ext == L"tif" || ext == L"tiff") mime = L"image/tiff";
    CLSID clsid;
    if (!GetEncoderClsid(mime, &clsid)) {
        err = L"Für dieses Dateiformat ist kein Encoder verfügbar.";
        return false;
    }
    Pixmap copy = p;
    MakeOpaque(copy);
    auto src = WrapBitmap(copy);
    std::unique_ptr<Bitmap> b24(src->Clone(Rect(0, 0, p.w, p.h), PixelFormat24bppRGB));
    if (!b24 || b24->GetLastStatus() != Ok) {
        err = L"Nicht genügend Arbeitsspeicher.";
        return false;
    }
    b24->SetResolution(96.0f, 96.0f);

    // In temporäre Datei schreiben und dann ersetzen, damit ein Fehler die alte Datei nicht zerstört
    wstring tmp = path + L".~pctmp";
    Status st;
    if (wcscmp(mime, L"image/jpeg") == 0) {
        EncoderParameters ep;
        ULONG quality = 95;
        ep.Count = 1;
        ep.Parameter[0].Guid = EncoderQuality;
        ep.Parameter[0].Type = EncoderParameterValueTypeLong;
        ep.Parameter[0].NumberOfValues = 1;
        ep.Parameter[0].Value = &quality;
        st = b24->Save(tmp.c_str(), &clsid, &ep);
    } else {
        st = b24->Save(tmp.c_str(), &clsid, nullptr);
    }
    if (st != Ok) {
        DeleteFileW(tmp.c_str());
        err = L"Die Datei konnte nicht gespeichert werden. Prüfen Sie, ob der Pfad existiert und "
              L"Schreibrechte vorhanden sind.";
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        err = L"Die Datei konnte nicht ersetzt werden (eventuell schreibgeschützt oder in Benutzung).";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Zwischenablage
// ---------------------------------------------------------------------------
bool CopyPixmapToClipboard(HWND hwnd, const Pixmap& p) {
    if (p.empty()) return false;
    int stride = ((p.w * 3) + 3) & ~3;
    size_t imgSize = (size_t)stride * p.h;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imgSize);
    if (!hg) return false;
    auto* bih = static_cast<BITMAPINFOHEADER*>(GlobalLock(hg));
    if (!bih) {
        GlobalFree(hg);
        return false;
    }
    ZeroMemory(bih, sizeof(*bih));
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = p.w;
    bih->biHeight = p.h;  // bottom-up
    bih->biPlanes = 1;
    bih->biBitCount = 24;
    bih->biCompression = BI_RGB;
    bih->biSizeImage = (DWORD)imgSize;
    BYTE* bits = reinterpret_cast<BYTE*>(bih + 1);
    for (int y = 0; y < p.h; ++y) {
        BYTE* row = bits + (size_t)(p.h - 1 - y) * stride;
        for (int x = 0; x < p.w; ++x) {
            uint32_t v = p.at(x, y);
            row[x * 3 + 0] = v & 255;
            row[x * 3 + 1] = (v >> 8) & 255;
            row[x * 3 + 2] = (v >> 16) & 255;
        }
        for (int k = p.w * 3; k < stride; ++k) row[k] = 0;
    }
    GlobalUnlock(hg);
    if (!OpenClipboard(hwnd)) {
        GlobalFree(hg);
        return false;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_DIB, hg)) {
        GlobalFree(hg);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

bool ClipboardHasImage() {
    return IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_DIB) ||
           IsClipboardFormatAvailable(CF_DIBV5) || IsClipboardFormatAvailable(CF_HDROP);
}

bool PastePixmapFromClipboard(HWND hwnd, Pixmap& out) {
    if (!OpenClipboard(hwnd)) return false;
    bool ok = false;
    if (IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_DIB) ||
        IsClipboardFormatAvailable(CF_DIBV5)) {
        HBITMAP hbm = (HBITMAP)GetClipboardData(CF_BITMAP);
        BITMAP bm = {};
        if (hbm && GetObject(hbm, sizeof(bm), &bm) && SizeAllowed(bm.bmWidth, std::abs(bm.bmHeight))) {
            int w = bm.bmWidth, h = std::abs(bm.bmHeight);
            Pixmap p(w, h);
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            HDC dc = GetDC(nullptr);
            if (GetDIBits(dc, hbm, 0, h, p.px.data(), &bi, DIB_RGB_COLORS) == h) {
                MakeOpaque(p);
                out = std::move(p);
                ok = true;
            }
            ReleaseDC(nullptr, dc);
        }
    }
    if (!ok && IsClipboardFormatAvailable(CF_HDROP)) {
        HDROP hd = (HDROP)GetClipboardData(CF_HDROP);
        if (hd && DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0) > 0) {
            UINT len = DragQueryFileW(hd, 0, nullptr, 0);
            wstring path(len + 1, L'\0');
            DragQueryFileW(hd, 0, &path[0], len + 1);
            path.resize(len);
            CloseClipboard();
            wstring err;
            return LoadImageFile(path, out, err);
        }
    }
    CloseClipboard();
    return ok;
}
