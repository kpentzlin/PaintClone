# Oberflächen-Rauchtest für PaintClone (läuft z. B. auf GitHub-Actions-Windows-Runnern).
# Startet das Programm, zeichnet mit simulierter Maus/Tastatur, öffnet Dialoge, speichert und lädt
# eine Datei und legt Bildschirmfotos in .\screens ab. Bei einem Absturz endet das Skript mit Fehler.
param([string]$Exe = "build\Release\PaintClone.exe")

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class U {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, uint d, UIntPtr e);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string cls, string title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    public static string Title(IntPtr h) { var s = new StringBuilder(512); GetWindowTextW(h, s, 512); return s.ToString(); }
}
"@

$shots = Join-Path (Get-Location) "screens"
New-Item -ItemType Directory -Force -Path $shots | Out-Null
$script:n = 0

function Shot([string]$name) {
    Start-Sleep -Milliseconds 400
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
    $script:n++
    $file = Join-Path $shots ("{0:D2}_{1}.png" -f $script:n, $name)
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output "Foto: $file"
}

$script:step = "Start"
function Step([string]$s) { $script:step = $s; Write-Output "--- $s" }

function Alive {
    if ($script:proc.HasExited) {
        Shot "absturz"
        Write-Output "::error::PaintClone wurde im Schritt '$($script:step)' unerwartet beendet (Exitcode $($script:proc.ExitCode))."
        $log = Join-Path $env:LOCALAPPDATA "PaintClone\Absturz.txt"
        if (Test-Path $log) {
            $lines = Get-Content $log
            Write-Output "::error::$($lines -join ' | ')"
        }
        exit 1
    }
}

function ScreenPt([IntPtr]$h, [int]$x, [int]$y) {
    $p = New-Object U+POINT
    $p.X = $x; $p.Y = $y
    [U]::ClientToScreen($h, [ref]$p) | Out-Null
    return $p
}

function Click([IntPtr]$h, [int]$x, [int]$y, [switch]$Right) {
    $p = ScreenPt $h $x $y
    [U]::SetCursorPos($p.X, $p.Y) | Out-Null
    Start-Sleep -Milliseconds 60
    if ($Right) { [U]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40; [U]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero) }
    else { [U]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40; [U]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 150
}

function Drag([IntPtr]$h, [int]$x0, [int]$y0, [int]$x1, [int]$y1, [switch]$Right) {
    $p = ScreenPt $h $x0 $y0
    $q = ScreenPt $h $x1 $y1
    [U]::SetCursorPos($p.X, $p.Y) | Out-Null
    Start-Sleep -Milliseconds 60
    $down = 0x0002; $up = 0x0004
    if ($Right) { $down = 0x0008; $up = 0x0010 }
    [U]::mouse_event($down, 0, 0, 0, [UIntPtr]::Zero)
    $steps = 12
    for ($i = 1; $i -le $steps; $i++) {
        $x = [int]($p.X + ($q.X - $p.X) * $i / $steps)
        $y = [int]($p.Y + ($q.Y - $p.Y) * $i / $steps)
        [U]::SetCursorPos($x, $y) | Out-Null
        Start-Sleep -Milliseconds 25
    }
    [U]::mouse_event($up, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 150
}

function Keys([string]$k) {
    [System.Windows.Forms.SendKeys]::SendWait($k)
    Start-Sleep -Milliseconds 300
}

# Werkzeug t im Werkzeugkasten anklicken (2 Spalten, 26 px + 2 px Abstand, Rand 4 px)
function Tool([int]$t) {
    $col = $t % 2; $row = [math]::Floor($t / 2)
    Click $script:tb (4 + $col * 28 + 13) (4 + $row * 28 + 13)
}

# Farbe i der Palette (14 Spalten), links = Farbe 1, rechts = Farbe 2
function PaletteColor([int]$i, [switch]$Right) {
    $col = $i % 14; $row = [math]::Floor($i / 14)
    Click $script:pal (46 + $col * 18 + 8) (5 + $row * 18 + 8) -Right:$Right
}

# Bildkoordinaten (100 %) -> Zeichenflächen-Client
function Img([int]$x) { return $x + 6 }

# -------------------------------------------------------------------------
$appdata = Join-Path $env:APPDATA "PaintClone"
if (Test-Path $appdata) { Remove-Item -Recurse -Force $appdata }

$script:proc = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds 3
Alive
$main = $script:proc.MainWindowHandle
for ($i = 0; $i -lt 20 -and $main -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 300; $script:proc.Refresh(); $main = $script:proc.MainWindowHandle }
[U]::MoveWindow($main, 0, 0, 1000, 740, $true) | Out-Null
[U]::SetForegroundWindow($main) | Out-Null
Start-Sleep -Milliseconds 500
$script:cv = [U]::FindWindowExW($main, [IntPtr]::Zero, "PaintCloneCanvas", $null)
$script:tb = [U]::FindWindowExW($main, [IntPtr]::Zero, "PaintCloneToolbox", $null)
$script:pal = [U]::FindWindowExW($main, [IntPtr]::Zero, "PaintClonePalette", $null)
Write-Output "Fenster: '$([U]::Title($main))'  canvas=$($script:cv) toolbox=$($script:tb) palette=$($script:pal)"
Shot "start"

# Stift
Step "Stift"
Alive
Tool 6
Drag $script:cv (Img 20) (Img 20) (Img 220) (Img 120)
# Pinsel in Rot
Step "Pinsel in Rot"
Alive
PaletteColor 16
Tool 7
Drag $script:cv (Img 20) (Img 140) (Img 220) (Img 160)
# Ellipse (Umriss) in Blau
Step "Ellipse (Umriss) in Blau"
Alive
PaletteColor 20
Tool 14
Drag $script:cv (Img 260) (Img 30) (Img 420) (Img 150)
# Füllen in Gelb
Step "Füllen in Gelb"
Alive
PaletteColor 17
Tool 3
Click $script:cv (Img 340) (Img 90)
# Stern mit Füllung: Füllart "Umriss + Füllung" (zweite Option)
Step "Stern mit Fuellung"
Alive
PaletteColor 0
PaletteColor 18 -Right
Tool 16
Drag $script:cv (Img 450) (Img 30) (Img 590) (Img 160)
# Rechteck nur Füllung mit rechter Taste
Step "Rechteck nur Füllung mit rechter Taste"
Alive
Tool 12
Drag $script:cv (Img 620) (Img 30) (Img 720) (Img 110) -Right
# Linie mit Umschalt
Step "Linie mit Umschalt"
Alive
Tool 10
Drag $script:cv (Img 620) (Img 140) (Img 760) (Img 150)
# Sprühdose
Step "Sprühdose"
Alive
Tool 8
Drag $script:cv (Img 30) (Img 190) (Img 200) (Img 230)
# Vieleck: Ziehen, zwei Klicks, Doppelklick
Step "Vieleck: Ziehen, zwei Klicks, Doppelklick"
Alive
Tool 13
Drag $script:cv (Img 260) (Img 200) (Img 340) (Img 190)
Click $script:cv (Img 380) (Img 260)
Click $script:cv (Img 300) (Img 300)
Click $script:cv (Img 260) (Img 200)
# Kurve
Step "Kurve"
Alive
Tool 11
Drag $script:cv (Img 420) (Img 220) (Img 600) (Img 220)
Drag $script:cv (Img 470) (Img 220) (Img 470) (Img 170)
Drag $script:cv (Img 550) (Img 220) (Img 550) (Img 290)
Alive
Shot "zeichnen"

# Text
Step "Text"
Alive
Tool 9
Drag $script:cv (Img 30) (Img 320) (Img 330) (Img 360)
Keys "Hallo PaintClone – äöüß"
Shot "text_eingabe"
Click $script:cv (Img 700) (Img 450)
Alive

# Auswahl verschieben und Farben umkehren
Step "Auswahl verschieben und Farben umkehren"
Alive
Tool 1
Drag $script:cv (Img 20) (Img 20) (Img 230) (Img 170)
Drag $script:cv (Img 100) (Img 80) (Img 180) (Img 420)
Shot "auswahl_verschoben"
Keys "^i"
Shot "auswahl_invertiert"
Keys "{ESC}"
Alive

# Freihandauswahl, kopieren und einfügen
Step "Freihandauswahl, kopieren und einfügen"
Alive
Tool 0
Drag $script:cv (Img 450) (Img 30) (Img 600) (Img 170)
Keys "^c"
Keys "^v"
Drag $script:cv (Img 30) (Img 30) (Img 650) (Img 380)
Shot "einfuegen"
Keys "{ESC}"
Alive

# Dialoge
Step "Dialoge"
Alive
Keys "^w"
Shot "dialog_groesse"
Keys "{ESC}"
Keys "^e"
Shot "dialog_attribute"
Keys "{ESC}"
Keys "^r"
Shot "dialog_drehen"
Keys "{ENTER}"
Shot "gespiegelt"
Keys "^z"
Alive

# Zoom und Gitternetz
Step "Zoom und Gitternetz"
Alive
Keys "^{PGUP}"
Keys "^{PGUP}"
Keys "^{PGUP}"
Keys "^g"
Shot "zoom_400_gitter"
Keys "^0"

# Rückgängig mehrfach, dann wiederherstellen
Step "Rückgängig mehrfach, dann wiederherstellen"
Alive
for ($i = 0; $i -lt 6; $i++) { Keys "^z" }
Shot "rueckgaengig"
for ($i = 0; $i -lt 6; $i++) { Keys "^y" }
Alive


# Werkzeugoptionen aller Werkzeuge als Bildstreifen
Step "Werkzeugoptionen"
$strip = New-Object System.Drawing.Bitmap (17 * 66), 520
$sg = [System.Drawing.Graphics]::FromImage($strip)
for ($t = 0; $t -lt 17; $t++) {
    Tool $t
    Start-Sleep -Milliseconds 250
    $o = ScreenPt $script:tb 0 0
    $sg.CopyFromScreen($o.X, $o.Y, $t * 66, 0, (New-Object System.Drawing.Size 64, 520))
}
$script:n++
$strip.Save((Join-Path $shots ("{0:D2}_werkzeugoptionen.png" -f $script:n)), [System.Drawing.Imaging.ImageFormat]::Png)
$sg.Dispose(); $strip.Dispose()
Keys "^n"
Start-Sleep -Seconds 1
Keys "n"
Start-Sleep -Milliseconds 500
Alive

# Optionsbereich: Zellen (logische Pixel ab y = 265)
function Opt([int]$x, [int]$y) { Click $script:tb $x $y }

Step "Pinselformen"
Tool 7
for ($r = 0; $r -lt 4; $r++) {
    for ($c = 0; $c -lt 3; $c++) {
        Opt (6 + $c * 16 + 8) (265 + $r * 16 + 8)
        $x = 20 + ($r * 3 + $c) * 60
        Drag $script:cv (Img $x) (Img 20) (Img ($x + 40)) (Img 60)
    }
}
Alive

Step "Formen-Galerie"
PaletteColor 21 -Right
Tool 16
Opt 30 373
$shapesAt = @(0, 3, 6, 9, 11, 13, 14, 15, 16, 17)
$k = 0
foreach ($i in $shapesAt) {
    Opt (6 + ($i % 3) * 16 + 8) (265 + [math]::Floor($i / 3) * 16 + 8)
    $x = 20 + ($k % 5) * 110
    $y = 90 + [math]::Floor($k / 5) * 100
    Drag $script:cv (Img $x) (Img $y) (Img ($x + 90)) (Img ($y + 80))
    $k++
}
Alive

Step "Fuellarten Rechteck und Ellipse"
Tool 12
Opt 31 (265 + 18 + 9)
Drag $script:cv (Img 580) (Img 90) (Img 680) (Img 160)
Tool 14
Opt 31 (265 + 36 + 9)
Drag $script:cv (Img 580) (Img 190) (Img 680) (Img 260)
Tool 15
Opt 31 (265 + 18 + 9)
Opt 31 (265 + 54 + 4 * 12 + 6)
Drag $script:cv (Img 700) (Img 90) (Img 790) (Img 160)
Alive

Step "Pipette und Radierer"
Tool 4
Click $script:cv (Img 620) (Img 120)
Tool 6
Drag $script:cv (Img 700) (Img 200) (Img 790) (Img 260)
Tool 2
Opt 31 (265 + 3 * 18 + 9)
Drag $script:cv (Img 20) (Img 30) (Img 300) (Img 50)
Shot "formen_pinsel_radierer"
Alive

Step "Text deckend"
Tool 9
Opt 31 278
PaletteColor 17 -Right
Drag $script:cv (Img 20) (Img 300) (Img 260) (Img 330)
Keys "Deckender Text"
Click $script:cv (Img 600) (Img 560)
Tool 9
Opt 31 304
Alive

Step "Lupe"
Tool 5
Click $script:cv (Img 100) (Img 100)
Shot "lupe_200"
Click $script:cv 200 200 -Right
Alive

Step "Leinwand ziehen"
Keys "^{PGDN}"
Tool 6
Drag $script:cv (6 + 400 + 2) (6 + 300 + 2) (6 + 450) (6 + 330)
Shot "leinwand_vergroessert"
Keys "^0"
Alive

Step "Groesse aendern 50 Prozent"
Keys "^w"
Keys "50"
Keys "{ENTER}"
Shot "verkleinert_50"
Keys "^z"
Step "Neigen"
Keys "^w"
Keys "%o"
Keys "20"
Keys "{ENTER}"
Shot "geneigt"
Keys "^z"
Alive

Step "Attribute aendern"
Keys "^e"
Keys "%b"
Keys "640"
Keys "%h"
Keys "480"
Keys "{ENTER}"
Shot "attribute_640x480"
Keys "^z"
Alive

Step "Menues"
Keys "%d"
Shot "menue_datei"
Keys "{ESC}{ESC}"
Keys "%i"
Shot "menue_bild"
Keys "{ESC}{ESC}"
Keys "%a"
Shot "menue_ansicht"
Keys "{ESC}{ESC}"
Alive

Step "Farbdialog und Info"
$c = ScreenPt $script:pal (46 + 3 * 18 + 8) (5 + 8)
[U]::SetCursorPos($c.X, $c.Y) | Out-Null
[U]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero); [U]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 60
[U]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero); [U]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Seconds 1
Shot "farbdialog"
Keys "{ESC}"
Keys "{F1}"
Shot "info"
Keys "{ESC}"
Alive

Step "Drucken"
Keys "^p"
Start-Sleep -Seconds 2
Shot "drucken"
Keys "{ESC}"
Start-Sleep -Seconds 1
Keys "{ESC}"
Alive

# Speichern unter / Neu / Öffnen
Step "Speichern unter / Neu / Öffnen"
Alive
$out = Join-Path $env:RUNNER_TEMP "smoke_test.png"
if (Test-Path $out) { Remove-Item $out }
Keys "{F12}"
Start-Sleep -Seconds 2
Shot "dialog_speichern"
Keys $out
Keys "{ENTER}"
Start-Sleep -Seconds 2
Alive
if (Test-Path $out) { Write-Output "Gespeichert: $((Get-Item $out).Length) Bytes" } else { Write-Output "::error::Datei wurde nicht gespeichert." }
Keys "^n"
Start-Sleep -Seconds 1
Keys "^o"
Start-Sleep -Seconds 2
Keys $out
Keys "{ENTER}"
Start-Sleep -Seconds 2
Shot "wieder_geoeffnet"
Write-Output "Titel nach Öffnen: '$([U]::Title($main))'"
Alive

# Vollbild
Step "Vollbild"
Alive
Keys "{F11}"
Shot "vollbild"
Keys "{ESC}"
Alive

# Beenden
Step "Beenden"
Alive
[U]::SetForegroundWindow($main) | Out-Null
Keys "%{F4}"
Start-Sleep -Seconds 2
if (-not $script:proc.HasExited) {
    Shot "nach_beenden"
    Keys "n"
    Start-Sleep -Seconds 2
}
if (-not $script:proc.HasExited) { Write-Output "::warning::PaintClone hat sich nicht beendet."; $script:proc.Kill() }
else { Write-Output "Beendet mit Exitcode $($script:proc.ExitCode)" }
if (Test-Path (Join-Path $appdata "PaintClone.ini")) { Write-Output "Einstellungen gespeichert." } else { Write-Output "::warning::Keine Einstellungsdatei geschrieben." }
exit 0
