// PaintClone – Bildoperationen, Dateiformate, Zwischenablage
#include "common.h"

using namespace Gdiplus;

std::unique_ptr<Bitmap> WrapBitmap(Pixmap& p) {
    return std::make_unique<Bitmap>(p.w, p.h, p.w * 4, PixelFormat32bppARGB, (BYTE*)p.px.data());
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
