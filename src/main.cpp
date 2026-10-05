// PaintClone – Hauptfenster, Menübefehle, Einstellungen
#include "common.h"

#include <shlwapi.h>

App g;

const wchar_t* const kToolNames[T_COUNT] = {
    L"Freihandauswahl", L"Auswahl",
    L"Radierer/Farbradierer", L"Farbfüller",
    L"Farbe auswählen", L"Lupe",
    L"Stift", L"Pinsel",
    L"Sprühdose", L"Text",
    L"Linie", L"Kurve",
    L"Rechteck", L"Vieleck",
    L"Ellipse", L"Abgerundetes Rechteck",
    L"Formen",
};

const wchar_t* const kToolHints[T_COUNT] = {
    L"Wählt einen frei geformten Bereich aus, um ihn zu verschieben, zu kopieren oder zu bearbeiten.",
    L"Wählt einen rechteckigen Bereich aus, um ihn zu verschieben, zu kopieren oder zu bearbeiten.",
    L"Radiert mit der Hintergrundfarbe. Rechte Maustaste: ersetzt nur Pixel der Vordergrundfarbe.",
    L"Füllt einen Bereich mit der aktuellen Zeichenfarbe.",
    L"Übernimmt eine Farbe aus dem Bild (linke Taste: Farbe 1, rechte Taste: Farbe 2).",
    L"Ändert die Vergrößerung (linke Taste: vergrößern, rechte Taste: verkleinern).",
    L"Zeichnet eine Freihandlinie mit einer Breite von einem Pixel.",
    L"Zeichnet mit einem Pinsel der ausgewählten Form und Größe.",
    L"Zeichnet mit einer Sprühdose der ausgewählten Größe.",
    L"Fügt Text in das Bild ein.",
    L"Zeichnet eine gerade Linie (Umschalttaste: in 45°-Schritten).",
    L"Zeichnet eine Kurve: Linie ziehen, dann zweimal klicken oder ziehen, um sie zu biegen.",
    L"Zeichnet ein Rechteck (Umschalttaste: Quadrat).",
    L"Zeichnet ein Vieleck: Klicken für jede Ecke, Doppelklick zum Abschließen.",
    L"Zeichnet eine Ellipse (Umschalttaste: Kreis).",
    L"Zeichnet ein Rechteck mit abgerundeten Ecken.",
    L"Zeichnet die im Optionsbereich ausgewählte Form (Umschalttaste: gleichmäßig).",
};

const wchar_t* const kShapeNames[SH_COUNT] = {
    L"Dreieck", L"Rechtwinkliges Dreieck", L"Raute",
    L"Fünfeck", L"Sechseck", L"Achteck",
    L"Pfeil nach rechts", L"Pfeil nach links", L"Pfeil nach oben",
    L"Pfeil nach unten", L"Stern mit vier Zacken", L"Stern mit fünf Zacken",
    L"Stern mit sechs Zacken", L"Herz", L"Blitz",
    L"Rechteckige Legende", L"Ovale Legende", L"Kreuz",
};

const COLORREF kDefaultPalette[28] = {
    RGB(0, 0, 0),       RGB(128, 128, 128), RGB(128, 0, 0),     RGB(128, 128, 0),   RGB(0, 128, 0),
    RGB(0, 128, 128),   RGB(0, 0, 128),     RGB(128, 0, 128),   RGB(128, 128, 64),  RGB(0, 64, 64),
    RGB(0, 128, 255),   RGB(0, 64, 128),    RGB(128, 0, 255),   RGB(128, 64, 0),
    RGB(255, 255, 255), RGB(192, 192, 192), RGB(255, 0, 0),     RGB(255, 255, 0),   RGB(0, 255, 0),
    RGB(0, 255, 255),   RGB(0, 0, 255),     RGB(255, 0, 255),   RGB(255, 255, 128), RGB(0, 255, 128),
    RGB(128, 255, 255), RGB(128, 128, 255), RGB(255, 0, 128),   RGB(255, 128, 64),
};

const double kZoomLevels[] = {0.125, 0.25, 0.5, 1, 2, 3, 4, 6, 8};
const int kZoomLevelCount = sizeof(kZoomLevels) / sizeof(kZoomLevels[0]);

namespace {

const wchar_t* kMainClass = L"PaintCloneMain";
const wchar_t* kAppName = L"PaintClone";
const size_t kUndoMaxCount = 50;
const size_t kUndoMaxBytes = (size_t)768 * 1024 * 1024;
const int kMaxRecent = 9;

wstring statusHint;

// ---------------------------------------------------------------------------
// DPI
// ---------------------------------------------------------------------------
UINT QueryDpi(HWND h) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = (Fn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn && h) return fn(h);
    HDC dc = GetDC(nullptr);
    int d = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return (UINT)d;
}

void CreateUiFont() {
    if (g.uiFont) DeleteObject(g.uiFont);
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    using Fn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    static Fn fn = (Fn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi");
    if (fn) {
        fn(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, g.dpi);
    } else {
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    }
    g.uiFont = CreateFontIndirectW(&ncm.lfMessageFont);
}

// ---------------------------------------------------------------------------
// Einstellungen (INI in %APPDATA%\PaintClone, UTF-16)
// ---------------------------------------------------------------------------
wstring SettingsPath() {
    wchar_t appdata[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, appdata))) return L"";
    wstring dir = wstring(appdata) + L"\\PaintClone";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\PaintClone.ini";
}

int IniInt(const wstring& f, const wchar_t* sec, const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(sec, key, def, f.c_str());
}

wstring IniStr(const wstring& f, const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[2048] = {};
    GetPrivateProfileStringW(sec, key, def, buf, 2048, f.c_str());
    return buf;
}

void IniPut(const wstring& f, const wchar_t* sec, const wchar_t* key, const wstring& v) {
    WritePrivateProfileStringW(sec, key, v.c_str(), f.c_str());
}

void IniPutInt(const wstring& f, const wchar_t* sec, const wchar_t* key, long long v) {
    IniPut(f, sec, key, std::to_wstring(v));
}

WINDOWPLACEMENT savedPlacement = {};
bool havePlacement = false;

void LoadSettings() {
    std::copy(std::begin(kDefaultPalette), std::end(kDefaultPalette), g.palette);
    for (auto& c : g.custom) c = RGB(255, 255, 255);
    wstring f = SettingsPath();
    if (f.empty() || GetFileAttributesW(f.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    const wchar_t* S1 = L"Allgemein";
    g.showToolbox = IniInt(f, S1, L"Werkzeugleiste", 1) != 0;
    g.showPalette = IniInt(f, S1, L"Farbpalette", 1) != 0;
    g.showStatus = IniInt(f, S1, L"Statusleiste", 1) != 0;
    g.showTextBar = IniInt(f, S1, L"Textsymbolleiste", 1) != 0;
    g.showGrid = IniInt(f, S1, L"Gitternetz", 0) != 0;
    g.smoothing = IniInt(f, S1, L"Glaetten", 1) != 0;
    g.newW = std::clamp(IniInt(f, S1, L"NeuBreite", 800), 1, MAX_DIM);
    g.newH = std::clamp(IniInt(f, S1, L"NeuHoehe", 600), 1, MAX_DIM);
    g.lastDir = IniStr(f, S1, L"LetzterOrdner", L"");
    int l = IniInt(f, S1, L"FensterLinks", INT_MIN);
    if (l != INT_MIN) {
        savedPlacement.length = sizeof(savedPlacement);
        savedPlacement.rcNormalPosition.left = l;
        savedPlacement.rcNormalPosition.top = IniInt(f, S1, L"FensterOben", 0);
        savedPlacement.rcNormalPosition.right = IniInt(f, S1, L"FensterRechts", 800);
        savedPlacement.rcNormalPosition.bottom = IniInt(f, S1, L"FensterUnten", 600);
        savedPlacement.showCmd = IniInt(f, S1, L"FensterMaximiert", 0) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        havePlacement = true;
    }

    const wchar_t* S2 = L"Werkzeuge";
    g.lineWidth = std::clamp(IniInt(f, S2, L"Linienbreite", 1), 1, 5);
    g.brushShape = std::clamp(IniInt(f, S2, L"Pinselform", 0), 0, 3);
    g.brushSize = std::clamp(IniInt(f, S2, L"Pinselgroesse", 1), 0, 2);
    g.eraserSize = std::clamp(IniInt(f, S2, L"Radierergroesse", 1), 0, 3);
    g.airSize = std::clamp(IniInt(f, S2, L"Spruehgroesse", 1), 0, 2);
    g.fillStyle = std::clamp(IniInt(f, S2, L"Fuellart", 0), 0, 2);
    g.shape = std::clamp(IniInt(f, S2, L"Form", SH_STAR5), 0, SH_COUNT - 1);
    g.transparentSel = IniInt(f, S2, L"TransparenteAuswahl", 0) != 0;
    g.textOpaque = IniInt(f, S2, L"TextUndurchsichtig", 0) != 0;

    const wchar_t* S3 = L"Schrift";
    g.fontName = IniStr(f, S3, L"Name", L"Arial");
    if (g.fontName.empty()) g.fontName = L"Arial";
    g.fontSize = std::clamp(IniInt(f, S3, L"Groesse", 12), 1, 999);
    g.fontBold = IniInt(f, S3, L"Fett", 0) != 0;
    g.fontItalic = IniInt(f, S3, L"Kursiv", 0) != 0;
    g.fontUnderline = IniInt(f, S3, L"Unterstrichen", 0) != 0;
    g.fontStrike = IniInt(f, S3, L"Durchgestrichen", 0) != 0;

    const wchar_t* S4 = L"Farben";
    for (int i = 0; i < 28; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"Palette%d", i);
        int v = IniInt(f, S4, key, -1);
        if (v >= 0) g.palette[i] = (COLORREF)(v & 0xFFFFFF);
    }
    for (int i = 0; i < 16; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"Eigene%d", i);
        int v = IniInt(f, S4, key, -1);
        if (v >= 0) g.custom[i] = (COLORREF)(v & 0xFFFFFF);
    }

    const wchar_t* S5 = L"ZuletztVerwendet";
    for (int i = 0; i < kMaxRecent; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"Datei%d", i + 1);
        wstring p = IniStr(f, S5, key, L"");
        if (!p.empty()) g.recent.push_back(p);
    }

    const wchar_t* S6 = L"Drucken";
    g.printMargins.left = IniInt(f, S6, L"RandLinks", 750);
    g.printMargins.top = IniInt(f, S6, L"RandOben", 750);
    g.printMargins.right = IniInt(f, S6, L"RandRechts", 750);
    g.printMargins.bottom = IniInt(f, S6, L"RandUnten", 750);
}

void SaveSettings() {
    wstring f = SettingsPath();
    if (f.empty()) return;
    // Datei als UTF-16 anlegen, damit Umlaute in Pfaden erhalten bleiben
    HANDLE h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        const BYTE bom[2] = {0xFF, 0xFE};
        DWORD written;
        WriteFile(h, bom, 2, &written, nullptr);
        CloseHandle(h);
    }
    const wchar_t* S1 = L"Allgemein";
    IniPutInt(f, S1, L"Werkzeugleiste", g.showToolbox);
    IniPutInt(f, S1, L"Farbpalette", g.showPalette);
    IniPutInt(f, S1, L"Statusleiste", g.showStatus);
    IniPutInt(f, S1, L"Textsymbolleiste", g.showTextBar);
    IniPutInt(f, S1, L"Gitternetz", g.showGrid);
    IniPutInt(f, S1, L"Glaetten", g.smoothing);
    IniPutInt(f, S1, L"NeuBreite", g.newW);
    IniPutInt(f, S1, L"NeuHoehe", g.newH);
    IniPut(f, S1, L"LetzterOrdner", g.lastDir);
    WINDOWPLACEMENT wp = {sizeof(wp)};
    if (g.hMain && GetWindowPlacement(g.hMain, &wp)) {
        IniPutInt(f, S1, L"FensterLinks", wp.rcNormalPosition.left);
        IniPutInt(f, S1, L"FensterOben", wp.rcNormalPosition.top);
        IniPutInt(f, S1, L"FensterRechts", wp.rcNormalPosition.right);
        IniPutInt(f, S1, L"FensterUnten", wp.rcNormalPosition.bottom);
        IniPutInt(f, S1, L"FensterMaximiert", wp.showCmd == SW_SHOWMAXIMIZED || IsZoomed(g.hMain));
    }
    const wchar_t* S2 = L"Werkzeuge";
    IniPutInt(f, S2, L"Linienbreite", g.lineWidth);
    IniPutInt(f, S2, L"Pinselform", g.brushShape);
    IniPutInt(f, S2, L"Pinselgroesse", g.brushSize);
    IniPutInt(f, S2, L"Radierergroesse", g.eraserSize);
    IniPutInt(f, S2, L"Spruehgroesse", g.airSize);
    IniPutInt(f, S2, L"Fuellart", g.fillStyle);
    IniPutInt(f, S2, L"Form", g.shape);
    IniPutInt(f, S2, L"TransparenteAuswahl", g.transparentSel);
    IniPutInt(f, S2, L"TextUndurchsichtig", g.textOpaque);
    const wchar_t* S3 = L"Schrift";
    IniPut(f, S3, L"Name", g.fontName);
    IniPutInt(f, S3, L"Groesse", g.fontSize);
    IniPutInt(f, S3, L"Fett", g.fontBold);
    IniPutInt(f, S3, L"Kursiv", g.fontItalic);
    IniPutInt(f, S3, L"Unterstrichen", g.fontUnderline);
    IniPutInt(f, S3, L"Durchgestrichen", g.fontStrike);
    const wchar_t* S4 = L"Farben";
    for (int i = 0; i < 28; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"Palette%d", i);
        IniPutInt(f, S4, key, g.palette[i]);
    }
    for (int i = 0; i < 16; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"Eigene%d", i);
        IniPutInt(f, S4, key, g.custom[i]);
    }
    const wchar_t* S5 = L"ZuletztVerwendet";
    WritePrivateProfileStringW(S5, nullptr, nullptr, f.c_str());
    for (size_t i = 0; i < g.recent.size(); ++i) {
        wchar_t key[16];
        swprintf_s(key, L"Datei%d", (int)i + 1);
        IniPut(f, S5, key, g.recent[i]);
    }
    const wchar_t* S6 = L"Drucken";
    IniPutInt(f, S6, L"RandLinks", g.printMargins.left);
    IniPutInt(f, S6, L"RandOben", g.printMargins.top);
    IniPutInt(f, S6, L"RandRechts", g.printMargins.right);
    IniPutInt(f, S6, L"RandUnten", g.printMargins.bottom);
}

// ---------------------------------------------------------------------------
// Zuletzt verwendete Dateien
// ---------------------------------------------------------------------------
void RebuildRecentMenu() {
    if (!g.hRecentMenu) return;
    while (GetMenuItemCount(g.hRecentMenu) > 0) DeleteMenu(g.hRecentMenu, 0, MF_BYPOSITION);
    if (g.recent.empty()) {
        AppendMenuW(g.hRecentMenu, MF_STRING | MF_GRAYED, ID_FILE_RECENT_NONE, L"(keine)");
        return;
    }
    for (size_t i = 0; i < g.recent.size(); ++i) {
        wchar_t shortPath[MAX_PATH] = {};
        PathCompactPathExW(shortPath, g.recent[i].c_str(), 60, 0);
        wstring s = L"&" + std::to_wstring(i + 1) + L"  ";
        for (wchar_t* c = shortPath; *c; ++c) {
            if (*c == L'&') s += L"&&";
            else s += *c;
        }
        AppendMenuW(g.hRecentMenu, MF_STRING, ID_FILE_RECENT_FIRST + (UINT)i, s.c_str());
    }
}

void AddRecent(const wstring& path) {
    auto it = std::find_if(g.recent.begin(), g.recent.end(),
                           [&](const wstring& p) { return lstrcmpiW(p.c_str(), path.c_str()) == 0; });
    if (it != g.recent.end()) g.recent.erase(it);
    g.recent.insert(g.recent.begin(), path);
    if (g.recent.size() > (size_t)kMaxRecent) g.recent.resize(kMaxRecent);
    RebuildRecentMenu();
}

// ---------------------------------------------------------------------------
// Dateien
// ---------------------------------------------------------------------------
wstring FileNameOnly(const wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    return s == wstring::npos ? p : p.substr(s + 1);
}

wstring DirOf(const wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    return s == wstring::npos ? L"" : p.substr(0, s);
}

const wchar_t kOpenFilter[] =
    L"Alle Bilddateien\0*.png;*.bmp;*.dib;*.jpg;*.jpeg;*.jpe;*.jfif;*.gif;*.tif;*.tiff;*.ico\0"
    L"PNG (*.png)\0*.png\0"
    L"Bitmap (*.bmp;*.dib)\0*.bmp;*.dib\0"
    L"JPEG (*.jpg;*.jpeg;*.jpe;*.jfif)\0*.jpg;*.jpeg;*.jpe;*.jfif\0"
    L"GIF (*.gif)\0*.gif\0"
    L"TIFF (*.tif;*.tiff)\0*.tif;*.tiff\0"
    L"Symbol (*.ico)\0*.ico\0"
    L"Alle Dateien (*.*)\0*.*\0";

const wchar_t kSaveFilter[] =
    L"PNG (*.png)\0*.png\0"
    L"24-Bit-Bitmap (*.bmp;*.dib)\0*.bmp;*.dib\0"
    L"JPEG (*.jpg;*.jpeg;*.jpe;*.jfif)\0*.jpg;*.jpeg;*.jpe;*.jfif\0"
    L"GIF (*.gif)\0*.gif\0"
    L"TIFF (*.tif;*.tiff)\0*.tif;*.tiff\0";

bool AskOpenPath(HWND owner, wstring& path, const wchar_t* title) {
    wchar_t buf[32768] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = kOpenFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 32768;
    ofn.lpstrTitle = title;
    ofn.lpstrInitialDir = g.lastDir.empty() ? nullptr : g.lastDir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return false;
    path = buf;
    g.lastDir = DirOf(path);
    return true;
}

bool AskSavePath(HWND owner, wstring& path, const wchar_t* title, const wstring& suggested) {
    wchar_t buf[32768] = {};
    wcsncpy_s(buf, suggested.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = kSaveFilter;
    // Filter passend zur aktuellen Endung
    wstring ext = PathFindExtensionW(suggested.c_str());
    for (auto& c : ext) c = (wchar_t)towlower(c);
    ofn.nFilterIndex = 1;
    if (ext == L".bmp" || ext == L".dib") ofn.nFilterIndex = 2;
    else if (ext == L".jpg" || ext == L".jpeg" || ext == L".jpe" || ext == L".jfif") ofn.nFilterIndex = 3;
    else if (ext == L".gif") ofn.nFilterIndex = 4;
    else if (ext == L".tif" || ext == L".tiff") ofn.nFilterIndex = 5;
    const wchar_t* defExt[] = {L"png", L"bmp", L"jpg", L"gif", L"tif"};
    ofn.lpstrDefExt = defExt[ofn.nFilterIndex - 1];
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 32768;
    ofn.lpstrTitle = title;
    wstring initDir = DirOf(suggested);
    if (initDir.empty()) initDir = g.lastDir;
    ofn.lpstrInitialDir = initDir.empty() ? nullptr : initDir.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return false;
    path = buf;
    // Keine oder unbekannte Endung: Endung des gewählten Filters anhängen
    wstring e = PathFindExtensionW(path.c_str());
    for (auto& c : e) c = (wchar_t)towlower(c);
    const wchar_t* known[] = {L".png", L".bmp", L".dib", L".jpg", L".jpeg", L".jpe", L".jfif", L".gif", L".tif", L".tiff"};
    bool ok = std::any_of(std::begin(known), std::end(known), [&](const wchar_t* k) { return e == k; });
    if (!ok && ofn.nFilterIndex >= 1 && ofn.nFilterIndex <= 5) path += wstring(L".") + defExt[ofn.nFilterIndex - 1];
    g.lastDir = DirOf(path);
    return true;
}

bool SaveTo(const wstring& path) {
    Canvas_CommitAll();
    wstring err;
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    bool ok = SaveImageFile(path, g.img, err);
    SetCursor(old);
    if (!ok) {
        MsgBox(g.hMain, err, MB_ICONERROR);
        return false;
    }
    g.filePath = path;
    g.dirty = false;
    AddRecent(path);
    UpdateTitle();
    return true;
}

bool DoSaveAs() {
    wstring sugg = g.filePath.empty() ? (g.lastDir.empty() ? L"Unbenannt.png" : g.lastDir + L"\\Unbenannt.png")
                                      : g.filePath;
    wstring path;
    if (!AskSavePath(g.hMain, path, L"Speichern unter", sugg)) return false;
    return SaveTo(path);
}

bool DoSave() {
    if (g.filePath.empty()) return DoSaveAs();
    return SaveTo(g.filePath);
}

bool ConfirmDiscard() {
    if (Canvas_IsTextActive()) MarkDirty();
    if (!g.dirty) return true;
    wstring name = g.filePath.empty() ? L"Unbenannt" : FileNameOnly(g.filePath);
    int r = MsgBox(g.hMain, L"Möchten Sie die Änderungen an „" + name + L"“ speichern?",
                   MB_YESNOCANCEL | MB_ICONWARNING);
    if (r == IDCANCEL) return false;
    if (r == IDYES) return DoSave();
    return true;
}

void NewImage() {
    Canvas_CancelAll();
    g.img = Pixmap(g.newW, g.newH, 0xFFFFFFFFu);
    g.filePath.clear();
    g.undo.clear();
    g.redo.clear();
    g.dirty = false;
    Canvas_ResetView();
    UpdateTitle();
    UpdateStatusInfo();
}

bool OpenFile(const wstring& path) {
    Pixmap p;
    wstring err;
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    bool ok = LoadImageFile(path, p, err);
    SetCursor(old);
    if (!ok) {
        MsgBox(g.hMain, L"„" + FileNameOnly(path) + L"“ kann nicht geöffnet werden.\n\n" + err, MB_ICONERROR);
        auto it = std::find(g.recent.begin(), g.recent.end(), path);
        if (it != g.recent.end() && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            g.recent.erase(it);
            RebuildRecentMenu();
        }
        return false;
    }
    Canvas_CancelAll();
    g.img = std::move(p);
    wchar_t full[MAX_PATH * 4] = {};
    if (GetFullPathNameW(path.c_str(), MAX_PATH * 4, full, nullptr)) g.filePath = full;
    else g.filePath = path;
    g.lastDir = DirOf(g.filePath);
    g.undo.clear();
    g.redo.clear();
    g.dirty = false;
    AddRecent(g.filePath);
    Canvas_ResetView();
    UpdateTitle();
    UpdateStatusInfo();
    return true;
}

// ---------------------------------------------------------------------------
// Rückgängig / Wiederholen
// ---------------------------------------------------------------------------
void TrimUndo() {
    size_t total = 0;
    for (auto& p : g.undo) total += p.bytes();
    while (g.undo.size() > 1 && (g.undo.size() > kUndoMaxCount || total > kUndoMaxBytes)) {
        total -= g.undo.front().bytes();
        g.undo.erase(g.undo.begin());
    }
}

void AfterImageSwap() {
    MarkDirty();
    Canvas_UpdateScroll();
    UpdateStatusInfo();
    Canvas_Invalidate();
}

void DoUndo() {
    if (Canvas_IsTextActive() && Canvas_TextEdit()) {
        SendMessageW(Canvas_TextEdit(), EM_UNDO, 0, 0);
        return;
    }
    if (Canvas_CancelAll()) return;
    if (g.undo.empty()) return;
    g.redo.push_back(std::move(g.img));
    g.img = std::move(g.undo.back());
    g.undo.pop_back();
    AfterImageSwap();
}

void DoRedo() {
    if (Canvas_IsTextActive()) return;
    Canvas_CancelAll();
    if (g.redo.empty()) return;
    g.undo.push_back(std::move(g.img));
    g.img = std::move(g.redo.back());
    g.redo.pop_back();
    AfterImageSwap();
}

// ---------------------------------------------------------------------------
// Statusleiste
// ---------------------------------------------------------------------------
void SetStatusParts() {
    if (!g.hStatus) return;
    RECT cr;
    GetClientRect(g.hStatus, &cr);
    int w4 = S(70), w3 = S(130), w2 = S(130), w1 = S(120);
    int parts[5];
    parts[4] = -1;
    parts[3] = std::max<int>(0, cr.right - w4 - S(16));
    parts[2] = std::max(0, parts[3] - w3);
    parts[1] = std::max(0, parts[2] - w2);
    parts[0] = std::max(0, parts[1] - w1);
    SendMessageW(g.hStatus, SB_SETPARTS, 5, (LPARAM)parts);
}

// ---------------------------------------------------------------------------
// Befehle
// ---------------------------------------------------------------------------
void ToggleView(bool& flag) {
    flag = !flag;
    LayoutMain();
}

void OnCommand(HWND hwnd, int id) {
    if (id >= ID_FILE_RECENT_FIRST && id <= ID_FILE_RECENT_LAST) {
        size_t i = (size_t)(id - ID_FILE_RECENT_FIRST);
        if (i < g.recent.size()) {
            wstring p = g.recent[i];
            if (ConfirmDiscard()) OpenFile(p);
        }
        return;
    }
    switch (id) {
    case ID_FILE_NEW:
        if (ConfirmDiscard()) NewImage();
        break;
    case ID_FILE_OPEN: {
        if (!ConfirmDiscard()) break;
        wstring p;
        if (AskOpenPath(hwnd, p, L"Öffnen")) OpenFile(p);
        break;
    }
    case ID_FILE_SAVE: DoSave(); break;
    case ID_FILE_SAVEAS: DoSaveAs(); break;
    case ID_FILE_PAGESETUP: DoPageSetup(hwnd); break;
    case ID_FILE_PRINT:
        Canvas_CommitAll();
        DoPrint(hwnd);
        break;
    case ID_FILE_WP_FILL:
    case ID_FILE_WP_TILE:
    case ID_FILE_WP_CENTER:
        Canvas_CommitAll();
        SetAsWallpaper(hwnd, id - ID_FILE_WP_FILL);
        break;
    case ID_FILE_EXIT: SendMessageW(hwnd, WM_CLOSE, 0, 0); break;

    case ID_EDIT_UNDO: DoUndo(); break;
    case ID_EDIT_REDO: DoRedo(); break;
    case ID_EDIT_CUT: Canvas_Cut(); break;
    case ID_EDIT_COPY: Canvas_Copy(); break;
    case ID_EDIT_PASTE: {
        if (Canvas_IsTextActive() && GetFocus() == Canvas_TextEdit()) {
            SendMessageW(Canvas_TextEdit(), WM_PASTE, 0, 0);
            break;
        }
        Pixmap p;
        if (PastePixmapFromClipboard(hwnd, p)) Canvas_PastePixmap(std::move(p));
        else MessageBeep(MB_ICONWARNING);
        break;
    }
    case ID_EDIT_PASTEFROM: {
        wstring path;
        if (!AskOpenPath(hwnd, path, L"Einfügen aus")) break;
        Pixmap p;
        wstring err;
        if (LoadImageFile(path, p, err)) Canvas_PastePixmap(std::move(p));
        else MsgBox(hwnd, err, MB_ICONERROR);
        break;
    }
    case ID_EDIT_COPYTO: {
        Pixmap p;
        if (!Canvas_GetSelectionPixmap(p, true)) break;
        wstring path;
        wstring sugg = g.lastDir.empty() ? L"Auswahl.png" : g.lastDir + L"\\Auswahl.png";
        if (!AskSavePath(hwnd, path, L"Kopieren nach", sugg)) break;
        wstring err;
        if (!SaveImageFile(path, p, err)) MsgBox(hwnd, err, MB_ICONERROR);
        break;
    }
    case ID_EDIT_CLEARSEL:
        if (Canvas_IsTextActive() && GetFocus() == Canvas_TextEdit()) {
            SendMessageW(Canvas_TextEdit(), WM_KEYDOWN, VK_DELETE, 0);
            break;
        }
        Canvas_DeleteSelection();
        break;
    case ID_EDIT_SELECTALL:
        if (Canvas_IsTextActive() && GetFocus() == Canvas_TextEdit()) {
            SendMessageW(Canvas_TextEdit(), EM_SETSEL, 0, -1);
            break;
        }
        Canvas_SelectAll();
        break;

    case ID_VIEW_TOOLBOX: ToggleView(g.showToolbox); break;
    case ID_VIEW_PALETTE: ToggleView(g.showPalette); break;
    case ID_VIEW_STATUS: ToggleView(g.showStatus); break;
    case ID_VIEW_TEXTBAR: ToggleView(g.showTextBar); break;
    case ID_VIEW_ZOOMIN: Canvas_ZoomStep(1); break;
    case ID_VIEW_ZOOMOUT: Canvas_ZoomStep(-1); break;
    case ID_VIEW_ZOOM100: Canvas_SetZoom(1.0); break;
    case ID_VIEW_ZOOMFIT: Canvas_ZoomFit(); break;
    case ID_VIEW_GRID:
        g.showGrid = !g.showGrid;
        if (g.showGrid && g.zoom < 4.0)
            SetStatusHint(L"Das Gitternetz wird ab einer Vergrößerung von 400 % angezeigt.");
        Canvas_Invalidate();
        break;
    case ID_VIEW_FULLSCREEN:
        Canvas_CommitAll();
        ShowFullScreen(hwnd);
        break;
    case ID_VIEW_SMOOTH: g.smoothing = !g.smoothing; break;

    case ID_IMAGE_FLIPROTATE: Dlg_FlipRotate(hwnd); break;
    case ID_IMAGE_FLIPH: Canvas_Transform(TR_FLIPH); break;
    case ID_IMAGE_FLIPV: Canvas_Transform(TR_FLIPV); break;
    case ID_IMAGE_ROT90: Canvas_Transform(TR_ROT90); break;
    case ID_IMAGE_ROT180: Canvas_Transform(TR_ROT180); break;
    case ID_IMAGE_ROT270: Canvas_Transform(TR_ROT270); break;
    case ID_IMAGE_RESIZE: Dlg_ResizeSkew(hwnd); break;
    case ID_IMAGE_CROP: Canvas_Crop(); break;
    case ID_IMAGE_INVERT: Canvas_Transform(TR_INVERT); break;
    case ID_IMAGE_ATTRIBUTES: Dlg_Attributes(hwnd); break;
    case ID_IMAGE_CLEAR:
        if (Canvas_HasSelection()) {
            Canvas_DeleteSelection();
        } else {
            Canvas_CommitAll();
            Pixmap p(g.img.w, g.img.h, RGBtoPX(g.bg));
            ReplaceImage(std::move(p));
        }
        break;
    case ID_IMAGE_OPAQUE:
        g.transparentSel = !g.transparentSel;
        Canvas_OnColorsChanged();
        InvalidateRect(g.hToolbox, nullptr, FALSE);
        break;

    case ID_COLORS_EDIT: {
        COLORREF c = g.fg;
        if (EditColor(hwnd, c)) SetColors(c, g.bg);
        break;
    }
    case ID_COLORS_SWAP: SetColors(g.bg, g.fg); break;
    case ID_COLORS_RESET:
        std::copy(std::begin(kDefaultPalette), std::end(kDefaultPalette), g.palette);
        SetColors(RGB(0, 0, 0), RGB(255, 255, 255));
        break;

    case ID_HELP_ABOUT: Dlg_About(hwnd); break;
    }
    if (g.hToolbox) InvalidateRect(g.hToolbox, nullptr, FALSE);
}

void OnInitMenu(HMENU m) {
    auto en = [&](int id, bool on) { EnableMenuItem(m, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED)); };
    auto ck = [&](int id, bool on) { CheckMenuItem(m, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
    bool text = Canvas_IsTextActive();
    bool sel = Canvas_HasSelection();
    en(ID_EDIT_UNDO, text || !g.undo.empty());
    en(ID_EDIT_REDO, !text && !g.redo.empty());
    en(ID_EDIT_CUT, sel || text);
    en(ID_EDIT_COPY, sel || text);
    en(ID_EDIT_PASTE, ClipboardHasImage() || (text && IsClipboardFormatAvailable(CF_UNICODETEXT)));
    en(ID_EDIT_COPYTO, sel);
    en(ID_EDIT_CLEARSEL, sel || text);
    en(ID_IMAGE_CROP, sel);
    ck(ID_VIEW_TOOLBOX, g.showToolbox);
    ck(ID_VIEW_PALETTE, g.showPalette);
    ck(ID_VIEW_STATUS, g.showStatus);
    ck(ID_VIEW_TEXTBAR, g.showTextBar);
    ck(ID_VIEW_GRID, g.showGrid);
    ck(ID_VIEW_SMOOTH, g.smoothing);
    ck(ID_IMAGE_OPAQUE, !g.transparentSel);
    en(ID_VIEW_ZOOMIN, g.zoom < kZoomLevels[kZoomLevelCount - 1] - 1e-6);
    en(ID_VIEW_ZOOMOUT, g.zoom > kZoomLevels[0] + 1e-6);
}

bool IsTextInputFocus() {
    HWND f = GetFocus();
    if (!f) return false;
    wchar_t cls[32] = {};
    GetClassNameW(f, cls, 32);
    return lstrcmpiW(cls, L"Edit") == 0 || lstrcmpiW(cls, L"ComboBox") == 0;
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.hMain = hwnd;
        g.dpi = QueryDpi(hwnd);
        CreateUiFont();
        g.hCanvas = Canvas_Create(hwnd);
        g.hToolbox = Toolbox_Create(hwnd);
        g.hPalette = Palette_Create(hwnd);
        g.hFontBar = FontBar_Create(hwnd);
        g.hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                                    hwnd, nullptr, g.hInst, nullptr);
        SendMessageW(g.hStatus, WM_SETFONT, (WPARAM)g.uiFont, FALSE);
        HMENU menu = GetMenu(hwnd);
        HMENU file = GetSubMenu(menu, 0);
        for (int i = 0; i < GetMenuItemCount(file); ++i) {
            HMENU sub = GetSubMenu(file, i);
            if (sub && GetMenuItemID(sub, 0) == ID_FILE_RECENT_NONE) {
                g.hRecentMenu = sub;
                break;
            }
        }
        RebuildRecentMenu();
        DragAcceptFiles(hwnd, TRUE);
        g.img = Pixmap(g.newW, g.newH, 0xFFFFFFFFu);
        return 0;
    }
    case WM_SIZE:
        LayoutMain();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = S(480);
        mmi->ptMinTrackSize.y = S(560);
        return 0;
    }
    case WM_DPICHANGED: {
        g.dpi = HIWORD(wp);
        CreateUiFont();
        SendMessageW(g.hStatus, WM_SETFONT, (WPARAM)g.uiFont, FALSE);
        Panels_OnDpiChanged();
        RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutMain();
        Canvas_OnDpiChanged();
        return 0;
    }
    case WM_SETFOCUS:
        if (Canvas_IsTextActive() && Canvas_TextEdit()) SetFocus(Canvas_TextEdit());
        else if (g.hCanvas) SetFocus(g.hCanvas);
        return 0;
    case WM_COMMAND:
        OnCommand(hwnd, LOWORD(wp));
        return 0;
    case WM_INITMENUPOPUP:
        OnInitMenu((HMENU)wp);
        return 0;
    case WM_MENUSELECT:
        if (HIWORD(wp) == 0xFFFF && lp == 0) SetStatusHint(nullptr);
        return 0;
    case WM_DROPFILES: {
        HDROP hd = (HDROP)wp;
        UINT len = DragQueryFileW(hd, 0, nullptr, 0);
        wstring path(len + 1, L'\0');
        DragQueryFileW(hd, 0, &path[0], len + 1);
        path.resize(len);
        DragFinish(hd);
        SetForegroundWindow(hwnd);
        if (!path.empty() && ConfirmDiscard()) OpenFile(path);
        return 0;
    }
    case WM_CLOSE:
        if (ConfirmDiscard()) DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        SaveSettings();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

// ---------------------------------------------------------------------------
// Öffentliche Hilfsfunktionen
// ---------------------------------------------------------------------------
int MsgBox(HWND owner, const wstring& text, UINT flags) {
    return MessageBoxW(owner, text.c_str(), kAppName, flags);
}

void PushUndo() {
    g.undo.push_back(g.img);
    g.redo.clear();
    TrimUndo();
}

void DropLastUndo() {
    if (!g.undo.empty()) g.undo.pop_back();
}

void MarkDirty() {
    if (!g.dirty) {
        g.dirty = true;
        UpdateTitle();
    }
}

void UpdateTitle() {
    if (!g.hMain) return;
    wstring name = g.filePath.empty() ? L"Unbenannt" : FileNameOnly(g.filePath);
    wstring t = (g.dirty ? L"*" : L"") + name + L" – " + kAppName;
    SetWindowTextW(g.hMain, t.c_str());
}

void SetStatusHint(const wchar_t* text) {
    if (!g.hStatus) return;
    statusHint = text ? text : kToolHints[g.tool];
    SendMessageW(g.hStatus, SB_SETTEXTW, 0, (LPARAM)statusHint.c_str());
}

void UpdateStatusPos(int x, int y, bool valid) {
    if (!g.hStatus) return;
    wchar_t buf[64] = L"";
    if (valid) swprintf_s(buf, L"%d, %d Px", x, y);
    SendMessageW(g.hStatus, SB_SETTEXTW, 1, (LPARAM)buf);
}

void UpdateStatusSel(int w, int h, bool valid) {
    if (!g.hStatus) return;
    wchar_t buf[64] = L"";
    if (valid) swprintf_s(buf, L"%d × %d Px", w, h);
    SendMessageW(g.hStatus, SB_SETTEXTW, 2, (LPARAM)buf);
}

void UpdateStatusInfo() {
    if (!g.hStatus) return;
    wchar_t buf[64];
    swprintf_s(buf, L"%d × %d Px", g.img.w, g.img.h);
    SendMessageW(g.hStatus, SB_SETTEXTW, 3, (LPARAM)buf);
    double pct = g.zoom * 100.0;
    if (std::fabs(pct - std::round(pct)) < 0.05) swprintf_s(buf, L"%d %%", (int)std::lround(pct));
    else swprintf_s(buf, L"%.1f %%", pct);
    SendMessageW(g.hStatus, SB_SETTEXTW, 4, (LPARAM)buf);
}

void SetTool(Tool t) {
    if (t != g.tool) {
        if (t == T_PICKER && g.tool != T_PICKER) g.prevTool = g.tool;
        g.tool = t;
        Canvas_OnToolChanged();
        if (t == T_TEXT) FontBar_Sync();
        LayoutMain();
    }
    SetStatusHint(nullptr);
    if (g.hToolbox) InvalidateRect(g.hToolbox, nullptr, FALSE);
}

void SetColors(COLORREF fg, COLORREF bg) {
    g.fg = fg;
    g.bg = bg;
    if (g.hPalette) InvalidateRect(g.hPalette, nullptr, FALSE);
    Canvas_OnColorsChanged();
}

void LayoutMain() {
    if (!g.hMain || !g.hCanvas) return;
    RECT cr;
    GetClientRect(g.hMain, &cr);
    int top = 0, bottom = cr.bottom;
    if (g.showStatus) {
        SendMessageW(g.hStatus, WM_SIZE, 0, 0);
        RECT sr;
        GetWindowRect(g.hStatus, &sr);
        bottom -= sr.bottom - sr.top;
        ShowWindow(g.hStatus, SW_SHOWNA);
        SetStatusParts();
    } else {
        ShowWindow(g.hStatus, SW_HIDE);
    }
    if (g.showPalette) {
        int ph = Palette_PreferredHeight();
        SetWindowPos(g.hPalette, nullptr, 0, bottom - ph, cr.right, ph, SWP_NOZORDER | SWP_SHOWWINDOW);
        bottom -= ph;
    } else {
        ShowWindow(g.hPalette, SW_HIDE);
    }
    bool fontVisible = g.showTextBar && g.tool == T_TEXT;
    if (fontVisible) {
        int fh = FontBar_PreferredHeight();
        SetWindowPos(g.hFontBar, nullptr, 0, 0, cr.right, fh, SWP_NOZORDER | SWP_SHOWWINDOW);
        top += fh;
    } else {
        ShowWindow(g.hFontBar, SW_HIDE);
    }
    int left = 0;
    if (g.showToolbox) {
        int tw = Toolbox_PreferredWidth();
        SetWindowPos(g.hToolbox, nullptr, 0, top, tw, std::max(0, bottom - top), SWP_NOZORDER | SWP_SHOWWINDOW);
        left = tw;
    } else {
        ShowWindow(g.hToolbox, SW_HIDE);
    }
    SetWindowPos(g.hCanvas, nullptr, left, top, std::max<int>(0, cr.right - left), std::max(0, bottom - top),
                 SWP_NOZORDER);
}

void ReplaceImage(Pixmap&& p) {
    PushUndo();
    g.img = std::move(p);
    MarkDirty();
    Canvas_UpdateScroll();
    UpdateStatusInfo();
    Canvas_Invalidate();
}

// ---------------------------------------------------------------------------
// Einstiegspunkt
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    g.hInst = hInst;
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdipToken = 0;
    if (Gdiplus::GdiplusStartup(&gdipToken, &gsi, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"GDI+ konnte nicht initialisiert werden.", kAppName, MB_ICONERROR);
        return 1;
    }

    LoadSettings();

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                 GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MENU);
    wc.lpszClassName = kMainClass;
    if (!RegisterClassExW(&wc) || !Canvas_Register() || !Panels_Register()) {
        MessageBoxW(nullptr, L"Fensterklassen konnten nicht registriert werden.", kAppName, MB_ICONERROR);
        return 1;
    }

    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, kMainClass, kAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1100, 800, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Das Hauptfenster konnte nicht erstellt werden.", kAppName, MB_ICONERROR);
        return 1;
    }
    g.hAccel = LoadAcceleratorsW(hInst, MAKEINTRESOURCEW(IDR_ACCEL));

    if (havePlacement) {
        // Nur übernehmen, wenn das Fenster auf einem vorhandenen Bildschirm liegt
        HMONITOR mon = MonitorFromRect(&savedPlacement.rcNormalPosition, MONITOR_DEFAULTTONULL);
        if (mon) {
            WINDOWPLACEMENT wp = savedPlacement;
            if (nShow == SW_SHOWMINIMIZED || nShow == SW_MINIMIZE || nShow == SW_SHOWMINNOACTIVE)
                wp.showCmd = SW_SHOWMINNOACTIVE;
            SetWindowPlacement(hwnd, &wp);
        } else {
            ShowWindow(hwnd, nShow);
        }
    } else {
        ShowWindow(hwnd, nShow);
    }
    LayoutMain();
    Canvas_ResetView();
    UpdateTitle();
    UpdateStatusInfo();
    SetStatusHint(nullptr);
    UpdateWindow(hwnd);

    // Datei aus der Befehlszeile öffnen
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1) OpenFile(argv[1]);
        LocalFree(argv);
    }
    SetFocus(g.hCanvas);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        bool translated = false;
        if (g.hAccel && (m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN)) {
            HWND top = GetAncestor(m.hwnd, GA_ROOT);
            if (top == g.hMain) {
                bool allow = true;
                if (IsTextInputFocus()) {
                    // In Eingabefeldern nur Datei- und Funktionstasten-Befehle zulassen
                    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    WPARAM k = m.wParam;
                    allow = (k >= VK_F1 && k <= VK_F24) ||
                            (ctrl && (k == 'S' || k == 'N' || k == 'O' || k == 'P'));
                }
                if (allow) translated = TranslateAcceleratorW(g.hMain, g.hAccel, &m) != 0;
            }
        }
        if (!translated) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    if (g.uiFont) DeleteObject(g.uiFont);
    g.undo.clear();
    g.redo.clear();
    Gdiplus::GdiplusShutdown(gdipToken);
    if (SUCCEEDED(hrCo)) CoUninitialize();
    return (int)m.wParam;
}
