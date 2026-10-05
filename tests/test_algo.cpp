// Tests der plattformunabhängigen Pixelalgorithmen (unter Linux/macOS/Windows lauffähig)
//   g++ -std=c++17 -O1 -I../src test_algo.cpp ../src/algo.cpp -o test_algo && ./test_algo
#include "algo.h"

#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static const uint32_t W = 0xFFFFFFFFu, B = 0xFF000000u, R = 0xFFFF0000u, G = 0xFF00FF00u;

static Pixmap Numbered(int w, int h) {
    Pixmap p(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) p.at(x, y) = 0xFF000000u | (uint32_t)(y * 1000 + x);
    return p;
}

static void TestRotations() {
    Pixmap p = Numbered(3, 2);
    Pixmap r = Rotate90(p);
    CHECK(r.w == 2 && r.h == 3);
    // Im Uhrzeigersinn: linke untere Ecke wird zur linken oberen Ecke
    CHECK(r.at(0, 0) == p.at(0, 1));
    CHECK(r.at(1, 0) == p.at(0, 0));
    CHECK(r.at(0, 2) == p.at(2, 1));
    Pixmap back = Rotate270(r);
    CHECK(back.w == p.w && back.h == p.h && back.px == p.px);
    CHECK(Rotate180(Rotate180(p)).px == p.px);
    CHECK(Rotate90(Rotate90(p)).px == Rotate180(p).px);
    CHECK(FlipH(FlipH(p)).px == p.px);
    CHECK(FlipV(FlipV(p)).px == p.px);
    CHECK(FlipH(p).at(0, 0) == p.at(2, 0));
    CHECK(FlipV(p).at(0, 0) == p.at(0, 1));
    CHECK(FlipV(FlipH(p)).px == Rotate180(p).px);
}

static void TestInvert() {
    Pixmap p(2, 1);
    p.at(0, 0) = W;
    p.at(1, 0) = 0x80123456u;
    InvertColors(p);
    CHECK(p.at(0, 0) == B);
    CHECK(p.at(1, 0) == (0x80000000u | (0x123456u ^ 0xFFFFFFu)));
}

static void TestFloodFill() {
    // Ring aus Schwarz, innen Weiß, außen Weiß
    Pixmap p(7, 7, W);
    for (int i = 1; i <= 5; ++i) {
        p.at(i, 1) = B;
        p.at(i, 5) = B;
        p.at(1, i) = B;
        p.at(5, i) = B;
    }
    CHECK(FloodFill(p, 3, 3, R));
    int inner = 0, outerRed = 0;
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 7; ++x) {
            bool in = x >= 2 && x <= 4 && y >= 2 && y <= 4;
            if (in && p.at(x, y) == R) ++inner;
            if (!in && p.at(x, y) == R) ++outerRed;
        }
    CHECK(inner == 9);
    CHECK(outerRed == 0);
    CHECK(!FloodFill(p, 3, 3, R));          // gleiche Farbe: nichts zu tun
    CHECK(!FloodFill(p, -1, 3, R));         // außerhalb
    CHECK(FloodFill(p, 0, 0, G));
    CHECK(p.at(6, 6) == G && p.at(0, 6) == G && p.at(3, 3) == R && p.at(1, 1) == B);
    // Diagonale Lücke wird bei 4er-Nachbarschaft nicht durchlaufen
    Pixmap d(3, 3, W);
    d.at(1, 0) = B;
    d.at(0, 1) = B;
    FloodFill(d, 0, 0, R);
    CHECK(d.at(0, 0) == R && d.at(2, 2) == W);
    // Großes Bild ohne Stapelüberlauf
    Pixmap big(2000, 1500, W);
    CHECK(FloodFill(big, 1000, 700, G));
    CHECK(big.at(0, 0) == G && big.at(1999, 1499) == G);
}

static void TestPolygonMask() {
    // Quadrat 2..7
    std::vector<POINT> sq = {{2, 2}, {7, 2}, {7, 7}, {2, 7}};
    auto m = PolygonMask(sq, 0, 0, 10, 10);
    int count = 0;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) {
            bool expect = x >= 2 && x <= 7 && y >= 2 && y <= 7;
            if (m[(size_t)y * 10 + x]) ++count;
            CHECK((m[(size_t)y * 10 + x] != 0) == expect);
        }
    CHECK(count == 36);
    // Versatz
    auto m2 = PolygonMask(sq, 2, 2, 6, 6);
    for (auto v : m2) CHECK(v == 1);
    // Dreieck: Spitze enthalten
    std::vector<POINT> tri = {{5, 0}, {9, 8}, {1, 8}};
    auto m3 = PolygonMask(tri, 0, 0, 11, 9);
    CHECK(m3[0 * 11 + 5] == 1);
    CHECK(m3[4 * 11 + 5] == 1);
    CHECK(m3[0 * 11 + 0] == 0);
    CHECK(m3[8 * 11 + 1] == 1 && m3[8 * 11 + 9] == 1);
}

static void TestSkewExtend() {
    Pixmap p(4, 3, R);
    Pixmap s = Skew(p, 45, 0, W);
    CHECK(s.h == 3 && s.w == 4 + 2);
    // oberste Zeile ganz nach rechts verschoben, unterste nicht
    CHECK(s.at(0, 2) == R && s.at(0, 0) == W);
    CHECK(s.at(s.w - 1, 0) == R && s.at(s.w - 1, 2) == W);
    Pixmap v = Skew(p, 0, -45, W);
    CHECK(v.w == 4 && v.h == 3 + 3);
    Pixmap n = Skew(p, 0, 0, W);
    CHECK(n.px == p.px);

    Pixmap e = ExtendCanvas(Numbered(3, 3), 5, 2, W);
    CHECK(e.w == 5 && e.h == 2);
    CHECK(e.at(2, 1) == (0xFF000000u | 1002u));
    CHECK(e.at(4, 0) == W);
    Pixmap sub = SubImage(Numbered(4, 4), -1, 2, 3, 3, W);
    CHECK(sub.at(0, 0) == W && sub.at(1, 0) == (0xFF000000u | 2000u) && sub.at(1, 2) == W);
}

static void TestLines() {
    Pixmap p(10, 10, W);
    BresenhamLine(p, 0, 0, 9, 9, B);
    for (int i = 0; i < 10; ++i) CHECK(p.at(i, i) == B);
    Pixmap q(10, 10, W);
    BresenhamLine(q, 2, 5, 7, 5, B);
    int n = 0;
    for (auto v : q.px) n += v == B;
    CHECK(n == 6);
    // Linie weit außerhalb darf nicht abstürzen
    BresenhamLine(q, -50, -50, 60, 70, R);

    auto circle = BrushOffsets(BR_CIRCLE, 8);
    auto square = BrushOffsets(BR_SQUARE, 5);
    auto slash = BrushOffsets(BR_SLASH, 5);
    auto single = BrushOffsets(BR_CIRCLE, 1);
    CHECK(square.size() == 25);
    CHECK(slash.size() == 5);
    CHECK(single.size() == 1 && single[0].x == 0 && single[0].y == 0);
    CHECK(circle.size() > 30 && circle.size() < 64);
    Pixmap s(20, 20, W);
    StampLine(s, {5, 10}, {15, 10}, square, B);
    CHECK(s.at(3, 8) == B && s.at(17, 12) == B && s.at(10, 7) == W);

    Pixmap e(20, 20, B);
    EraseLine(e, {0, 0}, {0, 0}, 4, W);
    CHECK(e.at(0, 0) == W && e.at(1, 1) == W && e.at(2, 2) == B);
    Pixmap r(10, 10, B);
    r.at(5, 5) = R;
    ReplaceLine(r, {5, 5}, {5, 5}, 4, B, G);
    CHECK(r.at(5, 5) == R && r.at(4, 4) == G);
}

static void TestLimits() {
    CHECK(SizeAllowed(1, 1));
    CHECK(SizeAllowed(10000, 10000));
    CHECK(!SizeAllowed(0, 10));
    CHECK(!SizeAllowed(50001, 10));
    CHECK(!SizeAllowed(20000, 20000));
}

int main() {
    TestRotations();
    TestInvert();
    TestFloodFill();
    TestPolygonMask();
    TestSkewExtend();
    TestLines();
    TestLimits();
    if (failures) {
        std::printf("%d Test(s) fehlgeschlagen.\n", failures);
        return 1;
    }
    std::printf("Alle Tests bestanden.\n");
    return 0;
}
