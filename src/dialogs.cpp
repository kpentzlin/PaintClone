// PaintClone – Dialoge, Drucken, Desktophintergrund, Vollbild
#include "common.h"

#include <shlwapi.h>

#include <cwchar>

namespace {

bool ParseNumber(HWND dlg, int id, double& out) {
    wchar_t buf[64] = {};
    GetDlgItemTextW(dlg, id, buf, 63);
    for (wchar_t* p = buf; *p; ++p)
        if (*p == L',') *p = L'.';
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    while (end && (*end == L' ' || *end == L'\t')) ++end;
    if (!end || end == buf || *end) return false;
    out = v;
    return true;
}

void SetNumber(HWND dlg, int id, double v, int decimals) {
    wchar_t buf[64];
    if (decimals == 0) swprintf_s(buf, L"%lld", (long long)std::llround(v));
    else {
        swprintf_s(buf, L"%.*f", decimals, v);
        for (wchar_t* p = buf; *p; ++p)
            if (*p == L'.') *p = L',';
    }
    SetDlgItemTextW(dlg, id, buf);
}

// ---------------------------------------------------------------------------
// Größe ändern und zerren
// ---------------------------------------------------------------------------
struct ResizeState {
    int w0 = 1, h0 = 1;
    bool updating = false;
} rs;

void ResizeSyncOther(HWND dlg, int changedId) {
    if (rs.updating || IsDlgButtonChecked(dlg, IDC_RS_KEEP) != BST_CHECKED) return;
    bool percent = IsDlgButtonChecked(dlg, IDC_RS_PERCENT) == BST_CHECKED;
    double v;
    if (!ParseNumber(dlg, changedId, v)) return;
    rs.updating = true;
    if (percent) {
        SetNumber(dlg, changedId == IDC_RS_H ? IDC_RS_V : IDC_RS_H, v, 0);
    } else if (changedId == IDC_RS_H) {
        SetNumber(dlg, IDC_RS_V, std::max(1.0, v * rs.h0 / rs.w0), 0);
    } else {
        SetNumber(dlg, IDC_RS_H, std::max(1.0, v * rs.w0 / rs.h0), 0);
    }
    rs.updating = false;
}

INT_PTR CALLBACK ResizeDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        Canvas_GetTargetSize(rs.w0, rs.h0);
        rs.updating = true;
        CheckRadioButton(dlg, IDC_RS_PERCENT, IDC_RS_PIXELS, IDC_RS_PERCENT);
        CheckDlgButton(dlg, IDC_RS_KEEP, BST_CHECKED);
        SetDlgItemTextW(dlg, IDC_RS_H, L"100");
        SetDlgItemTextW(dlg, IDC_RS_V, L"100");
        SetDlgItemTextW(dlg, IDC_SK_H, L"0");
        SetDlgItemTextW(dlg, IDC_SK_V, L"0");
        rs.updating = false;
        return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if ((id == IDC_RS_H || id == IDC_RS_V) && code == EN_CHANGE) {
            ResizeSyncOther(dlg, id);
            return TRUE;
        }
        if ((id == IDC_RS_PERCENT || id == IDC_RS_PIXELS) && code == BN_CLICKED) {
            bool percent = id == IDC_RS_PERCENT;
            rs.updating = true;
            if (percent) {
                SetDlgItemTextW(dlg, IDC_RS_H, L"100");
                SetDlgItemTextW(dlg, IDC_RS_V, L"100");
            } else {
                SetNumber(dlg, IDC_RS_H, rs.w0, 0);
                SetNumber(dlg, IDC_RS_V, rs.h0, 0);
            }
            rs.updating = false;
            return TRUE;
        }
        if (id == IDC_RS_KEEP && code == BN_CLICKED) {
            ResizeSyncOther(dlg, IDC_RS_H);
            return TRUE;
        }
        if (id == IDOK) {
            double h, v, sh, sv;
            if (!ParseNumber(dlg, IDC_RS_H, h) || !ParseNumber(dlg, IDC_RS_V, v) || h <= 0 || v <= 0) {
                MsgBox(dlg, L"Bitte geben Sie für die Größe positive Zahlen ein.", MB_ICONWARNING);
                return TRUE;
            }
            if (!ParseNumber(dlg, IDC_SK_H, sh) || !ParseNumber(dlg, IDC_SK_V, sv) || sh < -89 || sh > 89 ||
                sv < -89 || sv > 89) {
                MsgBox(dlg, L"Bitte geben Sie für das Neigen eine Zahl zwischen -89 und 89 ein.", MB_ICONWARNING);
                return TRUE;
            }
            bool percent = IsDlgButtonChecked(dlg, IDC_RS_PERCENT) == BST_CHECKED;
            if (percent && (h > 5000 || v > 5000)) {
                MsgBox(dlg, L"Bitte geben Sie einen Prozentwert zwischen 1 und 5000 ein.", MB_ICONWARNING);
                return TRUE;
            }
            EndDialog(dlg, IDOK);
            Canvas_ResizeSkew(percent, h, v, sh, sv);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Attribute
// ---------------------------------------------------------------------------
int atUnit = 2;  // 0 = Zoll, 1 = cm, 2 = Pixel
double atW = 0, atH = 0;

double ToUnit(double px, int unit) {
    if (unit == 0) return px / 96.0;
    if (unit == 1) return px / 96.0 * 2.54;
    return px;
}
double FromUnit(double v, int unit) {
    if (unit == 0) return v * 96.0;
    if (unit == 1) return v / 2.54 * 96.0;
    return v;
}

void AttribShow(HWND dlg) {
    int dec = atUnit == 2 ? 0 : 2;
    SetNumber(dlg, IDC_AT_W, ToUnit(atW, atUnit), dec);
    SetNumber(dlg, IDC_AT_H, ToUnit(atH, atUnit), dec);
}

bool AttribRead(HWND dlg) {
    double w, h;
    if (!ParseNumber(dlg, IDC_AT_W, w) || !ParseNumber(dlg, IDC_AT_H, h) || w <= 0 || h <= 0) return false;
    atW = std::max(1.0, std::round(FromUnit(w, atUnit)));
    atH = std::max(1.0, std::round(FromUnit(h, atUnit)));
    return true;
}

INT_PTR CALLBACK AttribDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        atW = g.img.w;
        atH = g.img.h;
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (!g.filePath.empty() && GetFileAttributesExW(g.filePath.c_str(), GetFileExInfoStandard, &fad)) {
            FILETIME lt;
            SYSTEMTIME st;
            FileTimeToLocalFileTime(&fad.ftLastWriteTime, &lt);
            FileTimeToSystemTime(&lt, &st);
            wchar_t d[64], t[64], buf[160];
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &st, nullptr, d, 64, nullptr);
            GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, t, 64);
            swprintf_s(buf, L"%s, %s", d, t);
            SetDlgItemTextW(dlg, IDC_AT_SAVED, buf);
            ULONGLONG size = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
            wchar_t sz[64];
            StrFormatByteSizeW((LONGLONG)size, sz, 64);
            swprintf_s(buf, L"%s (%llu Bytes)", sz, size);
            SetDlgItemTextW(dlg, IDC_AT_DISK, buf);
            SetDlgItemTextW(dlg, IDC_AT_FILE, g.filePath.c_str());
        } else {
            SetDlgItemTextW(dlg, IDC_AT_SAVED, L"Nicht verfügbar");
            SetDlgItemTextW(dlg, IDC_AT_DISK, L"Nicht verfügbar");
            SetDlgItemTextW(dlg, IDC_AT_FILE, L"(noch nicht gespeichert)");
        }
        SetDlgItemTextW(dlg, IDC_AT_RES, L"96 × 96 Punkte pro Zoll");
        atUnit = 2;
        CheckRadioButton(dlg, IDC_AT_INCH, IDC_AT_PX, IDC_AT_PX);
        AttribShow(dlg);
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if ((id == IDC_AT_INCH || id == IDC_AT_CM || id == IDC_AT_PX) && HIWORD(wp) == BN_CLICKED) {
            int nu = id - IDC_AT_INCH;
            if (nu != atUnit) {
                AttribRead(dlg);
                atUnit = nu;
                AttribShow(dlg);
            }
            return TRUE;
        }
        if (id == IDOK) {
            if (!AttribRead(dlg)) {
                MsgBox(dlg, L"Bitte geben Sie gültige positive Werte ein.", MB_ICONWARNING);
                return TRUE;
            }
            if (!SizeAllowed((long long)atW, (long long)atH)) {
                MsgBox(dlg, L"Die angegebene Größe ist zu groß.", MB_ICONWARNING);
                return TRUE;
            }
            EndDialog(dlg, IDOK);
            int nw = (int)atW, nh = (int)atH;
            g.newW = nw;
            g.newH = nh;
            if (nw != g.img.w || nh != g.img.h) {
                Canvas_CommitAll();
                ReplaceImage(ExtendCanvas(g.img, nw, nh, RGBtoPX(g.bg)));
            }
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Spiegeln/Drehen
// ---------------------------------------------------------------------------
void FlipRotateEnable(HWND dlg) {
    BOOL rot = IsDlgButtonChecked(dlg, IDC_FR_ROTATE) == BST_CHECKED;
    for (int id : {IDC_FR_90, IDC_FR_180, IDC_FR_270}) EnableWindow(GetDlgItem(dlg, id), rot);
}

INT_PTR CALLBACK FlipRotateDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        CheckRadioButton(dlg, IDC_FR_FLIPH, IDC_FR_ROTATE, IDC_FR_FLIPH);
        CheckRadioButton(dlg, IDC_FR_90, IDC_FR_270, IDC_FR_90);
        FlipRotateEnable(dlg);
        return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_FR_FLIPH || id == IDC_FR_FLIPV || id == IDC_FR_ROTATE) {
            FlipRotateEnable(dlg);
            return TRUE;
        }
        if (id == IDOK) {
            Transform t = TR_FLIPH;
            if (IsDlgButtonChecked(dlg, IDC_FR_FLIPV) == BST_CHECKED) t = TR_FLIPV;
            else if (IsDlgButtonChecked(dlg, IDC_FR_ROTATE) == BST_CHECKED) {
                if (IsDlgButtonChecked(dlg, IDC_FR_180) == BST_CHECKED) t = TR_ROT180;
                else if (IsDlgButtonChecked(dlg, IDC_FR_270) == BST_CHECKED) t = TR_ROT270;
                else t = TR_ROT90;
            }
            EndDialog(dlg, IDOK);
            Canvas_Transform(t);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Info
// ---------------------------------------------------------------------------
INT_PTR CALLBACK AboutDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        HICON ic = (HICON)LoadImageW(g.hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, S(48), S(48), 0);
        SendDlgItemMessageW(dlg, IDC_AB_ICON, STM_SETICON, (WPARAM)ic, 0);
        SetDlgItemTextW(dlg, IDC_AB_TEXT,
                        L"PaintClone 1.0 (64 Bit)\r\n\r\n"
                        L"Ein einfaches Malprogramm nach dem Vorbild von Microsoft Paint.\r\n\r\n"
                        L"Unterstützte Formate: PNG, BMP, JPEG, GIF, TIFF (Öffnen auch ICO).\r\n\r\n"
                        L"Microsoft und Paint sind Marken der Microsoft Corporation. "
                        L"PaintClone ist ein eigenständiges Projekt und steht in keiner Verbindung zu Microsoft.");
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Vollbild
// ---------------------------------------------------------------------------
LRESULT CALLBACK FullProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT cr;
        GetClientRect(hwnd, &cr);
        HDC dc = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max<int>(1, cr.right), std::max<int>(1, cr.bottom));
        HGDIOBJ old = SelectObject(dc, bmp);
        FillRect(dc, &cr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        double z = std::min(1.0, std::min((double)cr.right / g.img.w, (double)cr.bottom / g.img.h));
        int dw = std::max(1, (int)std::lround(g.img.w * z)), dh = std::max(1, (int)std::lround(g.img.h * z));
        int dx = (cr.right - dw) / 2, dy = (cr.bottom - dh) / 2;
        DrawPixmap(dc, g.img, dx, dy, dw, dh, 0, 0, g.img.w, g.img.h, z < 1.0);
        BitBlt(hdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        EnableWindow(g.hMain, TRUE);
        SetForegroundWindow(g.hMain);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void Dlg_ResizeSkew(HWND owner) {
    DialogBoxParamW(g.hInst, MAKEINTRESOURCEW(IDD_RESIZE), owner, ResizeDlgProc, 0);
}

void Dlg_Attributes(HWND owner) {
    DialogBoxParamW(g.hInst, MAKEINTRESOURCEW(IDD_ATTRIB), owner, AttribDlgProc, 0);
}

void Dlg_FlipRotate(HWND owner) {
    DialogBoxParamW(g.hInst, MAKEINTRESOURCEW(IDD_FLIPROTATE), owner, FlipRotateDlgProc, 0);
}

void Dlg_About(HWND owner) {
    DialogBoxParamW(g.hInst, MAKEINTRESOURCEW(IDD_ABOUT), owner, AboutDlgProc, 0);
}

bool EditColor(HWND owner, COLORREF& c) {
    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = owner;
    cc.rgbResult = c;
    cc.lpCustColors = g.custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (!ChooseColorW(&cc)) return false;
    c = cc.rgbResult;
    return true;
}

void DoPageSetup(HWND owner) {
    PAGESETUPDLGW psd = {};
    psd.lStructSize = sizeof(psd);
    psd.hwndOwner = owner;
    psd.hDevMode = g.hDevMode;
    psd.hDevNames = g.hDevNames;
    psd.Flags = PSD_INTHOUSANDTHSOFINCHES | PSD_MARGINS;
    psd.rtMargin = g.printMargins;
    if (PageSetupDlgW(&psd)) {
        g.hDevMode = psd.hDevMode;
        g.hDevNames = psd.hDevNames;
        g.printMargins = psd.rtMargin;
    }
}

void DoPrint(HWND owner) {
    PRINTDLGW pd = {};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = owner;
    pd.hDevMode = g.hDevMode;
    pd.hDevNames = g.hDevNames;
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    pd.nCopies = 1;
    if (!PrintDlgW(&pd)) return;
    g.hDevMode = pd.hDevMode;
    g.hDevNames = pd.hDevNames;
    HDC dc = pd.hDC;
    if (!dc) return;

    wstring title = g.filePath.empty() ? L"Unbenannt" : g.filePath.substr(g.filePath.find_last_of(L"\\/") + 1);
    DOCINFOW di = {sizeof(di)};
    di.lpszDocName = title.c_str();
    if (StartDocW(dc, &di) > 0) {
        if (StartPage(dc) > 0) {
            int dpix = GetDeviceCaps(dc, LOGPIXELSX), dpiy = GetDeviceCaps(dc, LOGPIXELSY);
            int offx = GetDeviceCaps(dc, PHYSICALOFFSETX), offy = GetDeviceCaps(dc, PHYSICALOFFSETY);
            int pw = GetDeviceCaps(dc, PHYSICALWIDTH), ph = GetDeviceCaps(dc, PHYSICALHEIGHT);
            int hres = GetDeviceCaps(dc, HORZRES), vres = GetDeviceCaps(dc, VERTRES);
            // Rand in Druckerpixel, relativ zum bedruckbaren Bereich
            int left = MulDiv(g.printMargins.left, dpix, 1000) - offx;
            int top = MulDiv(g.printMargins.top, dpiy, 1000) - offy;
            int right = pw - MulDiv(g.printMargins.right, dpix, 1000) - offx;
            int bottom = ph - MulDiv(g.printMargins.bottom, dpiy, 1000) - offy;
            left = std::max(0, left);
            top = std::max(0, top);
            right = std::min(hres, right);
            bottom = std::min(vres, bottom);
            int aw = std::max(1, right - left), ah = std::max(1, bottom - top);
            double w = g.img.w * (double)dpix / 96.0, h = g.img.h * (double)dpiy / 96.0;
            double sc = std::min(1.0, std::min(aw / w, ah / h));
            int dw = std::max(1, (int)(w * sc)), dh = std::max(1, (int)(h * sc));
            DrawPixmap(dc, g.img, left, top, dw, dh, 0, 0, g.img.w, g.img.h, false);
            EndPage(dc);
        }
        EndDoc(dc);
    } else {
        MsgBox(owner, L"Der Druckauftrag konnte nicht gestartet werden.", MB_ICONERROR);
    }
    DeleteDC(dc);
}

void SetAsWallpaper(HWND owner, int style) {
    wchar_t appdata[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, appdata))) return;
    wstring dir = wstring(appdata) + L"\\PaintClone";
    CreateDirectoryW(dir.c_str(), nullptr);
    wstring path = dir + L"\\Desktophintergrund.bmp";
    wstring err;
    if (!SaveImageFile(path, g.img, err)) {
        MsgBox(owner, err, MB_ICONERROR);
        return;
    }
    const wchar_t* ws = L"10";
    const wchar_t* tile = L"0";
    if (style == 1) { ws = L"0"; tile = L"1"; }
    else if (style == 2) { ws = L"0"; tile = L"0"; }
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegSetValueExW(key, L"WallpaperStyle", 0, REG_SZ, (const BYTE*)ws, (DWORD)((wcslen(ws) + 1) * 2));
        RegSetValueExW(key, L"TileWallpaper", 0, REG_SZ, (const BYTE*)tile, (DWORD)((wcslen(tile) + 1) * 2));
        RegCloseKey(key);
    }
    if (!SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)path.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
        MsgBox(owner, L"Der Desktophintergrund konnte nicht festgelegt werden.", MB_ICONERROR);
}

void ShowFullScreen(HWND owner) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = FullProc;
        wc.hInstance = g.hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"PaintCloneFullScreen";
        RegisterClassExW(&wc);
        registered = true;
    }
    HMONITOR mon = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    RECT r = mi.rcMonitor;
    HWND w = CreateWindowExW(WS_EX_TOPMOST, L"PaintCloneFullScreen", L"Vollbild", WS_POPUP | WS_VISIBLE, r.left,
                             r.top, r.right - r.left, r.bottom - r.top, owner, nullptr, g.hInst, nullptr);
    if (w) {
        EnableWindow(owner, FALSE);
        SetFocus(w);
    }
}
