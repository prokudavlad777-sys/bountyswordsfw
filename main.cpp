// =====================================================================
//  RUST CLONE  -  survival sandbox (C++17 + raylib 5.x)
//  Збирання ресурсів, крафт, будівництво як у Rust (фундамент/стіни/двері/дах,
//  дерево/камінь/метал), голод/спрага, звірі, день/ніч.
// =====================================================================
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ------------------------------------------------------------ constants
static const float WORLD   = 400.0f;   // розмір карти (метри)
static const int   GRID    = 140;      // клітинок террейну по стороні
static const float WATER_Y = 0.0f;
static const float CELL    = 3.0f;     // розмір будівельної клітинки
static const float WALL_H  = 3.0f;
static const float STEP_H  = 0.75f;    // висота "підйому" без стрибка
static const float PL_R    = 0.35f;
static const float PL_H    = 1.8f;

static Color Col(int r, int g, int b, int a = 255) {
    return Color{ (unsigned char)Clamp((float)r, 0, 255), (unsigned char)Clamp((float)g, 0, 255),
                  (unsigned char)Clamp((float)b, 0, 255), (unsigned char)Clamp((float)a, 0, 255) };
}
static float Rf(float a, float b) { return a + (b - a) * (float)GetRandomValue(0, 10000) / 10000.0f; }
static float Dist2(Vector3 a, Vector3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// ------------------------------------------------------------ font / text
static Font gFont;
static void LoadUiFont() {
    std::vector<int> cp;
    for (int c = 32; c < 127; c++) cp.push_back(c);
    for (int c = 0x400; c < 0x500; c++) cp.push_back(c);   // кирилиця (укр/рос)
    const char* paths[] = { "font.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                            "/System/Library/Fonts/Supplemental/Arial.ttf" };
    for (const char* p : paths) {
        if (!FileExists(p)) continue;
        gFont = LoadFontEx(p, 32, cp.data(), (int)cp.size());
        if (gFont.texture.id != 0) { SetTextureFilter(gFont.texture, TEXTURE_FILTER_BILINEAR); return; }
    }
    gFont = GetFontDefault();
}
static void Txt(const std::string& s, float x, float y, float size, Color c) {
    DrawTextEx(gFont, s.c_str(), Vector2{ x + 1, y + 1 }, size, 1.0f, Fade(BLACK, 0.6f * c.a / 255.0f));
    DrawTextEx(gFont, s.c_str(), Vector2{ x, y }, size, 1.0f, c);
}
static float TxtW(const std::string& s, float size) { return MeasureTextEx(gFont, s.c_str(), size, 1.0f).x; }
static void TxtC(const std::string& s, float cx, float y, float size, Color c) { Txt(s, cx - TxtW(s, size) * 0.5f, y, size, c); }

// ------------------------------------------------------------ terrain
static float Hf(float x, float z) {
    float h = 4.5f * sinf(x * 0.018f + 1.3f) * cosf(z * 0.022f + 0.4f)
            + 2.2f * sinf(x * 0.055f + z * 0.047f)
            + 0.9f * sinf(x * 0.13f + 0.7f) * sinf(z * 0.11f);
    float d = sqrtf(x * x + z * z) / (WORLD * 0.5f);
    return h + 7.0f - 16.0f * d * d;   // острів
}
static std::vector<float> HG;
static float Hash2(int i, int j) {
    unsigned n = (unsigned)i * 374761393u + (unsigned)j * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return (float)((n ^ (n >> 16)) & 0xffff) / 65535.0f;
}
static float TerrainH(float x, float z) {
    float u = (x + WORLD * 0.5f) / WORLD * GRID, v = (z + WORLD * 0.5f) / WORLD * GRID;
    if (u < 0 || v < 0 || u >= GRID - 0.001f || v >= GRID - 0.001f) return -20.0f;
    int i = (int)u, j = (int)v;
    float fu = u - i, fv = v - j;
    float h00 = HG[j * (GRID + 1) + i], h10 = HG[j * (GRID + 1) + i + 1];
    float h01 = HG[(j + 1) * (GRID + 1) + i], h11 = HG[(j + 1) * (GRID + 1) + i + 1];
    if (fu + fv <= 1.0f) return h00 + (h10 - h00) * fu + (h01 - h00) * fv;
    return h11 + (h01 - h11) * (1 - fu) + (h10 - h11) * (1 - fv);
}
static Mesh BuildTerrainMesh() {
    const int N = GRID;
    HG.assign((N + 1) * (N + 1), 0.0f);
    for (int j = 0; j <= N; j++)
        for (int i = 0; i <= N; i++)
            HG[j * (N + 1) + i] = Hf(-WORLD * 0.5f + i * WORLD / N, -WORLD * 0.5f + j * WORLD / N);

    Mesh m = { 0 };
    int vc = (N + 1) * (N + 1), tc = N * N * 2;
    m.vertexCount = vc; m.triangleCount = tc;
    m.vertices  = (float*)MemAlloc(vc * 3 * sizeof(float));
    m.normals   = (float*)MemAlloc(vc * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(vc * 2 * sizeof(float));
    m.colors    = (unsigned char*)MemAlloc(vc * 4);
    m.indices   = (unsigned short*)MemAlloc(tc * 3 * sizeof(unsigned short));
    Vector3 L = Vector3Normalize(Vector3{ 0.5f, 0.8f, 0.3f });
    float e = WORLD / N;
    for (int j = 0; j <= N; j++) {
        for (int i = 0; i <= N; i++) {
            int k = j * (N + 1) + i;
            float x = -WORLD * 0.5f + i * e, z = -WORLD * 0.5f + j * e, h = HG[k];
            Vector3 n = Vector3Normalize(Vector3{ Hf(x - 1, z) - Hf(x + 1, z), 2.0f, Hf(x, z - 1) - Hf(x, z + 1) });
            m.vertices[k * 3] = x; m.vertices[k * 3 + 1] = h; m.vertices[k * 3 + 2] = z;
            m.normals[k * 3] = n.x; m.normals[k * 3 + 1] = n.y; m.normals[k * 3 + 2] = n.z;
            m.texcoords[k * 2] = (float)i / N; m.texcoords[k * 2 + 1] = (float)j / N;
            float shade = 0.55f + 0.45f * fmaxf(0.0f, Vector3DotProduct(n, L));
            float rnd = (Hash2(i, j) - 0.5f) * 16.0f;
            int r, g, b;
            if (h < -0.3f)                       { r = 150; g = 138; b = 104; }
            else if (h < 0.7f)                   { r = 194; g = 178; b = 126; }
            else if (n.y < 0.80f || h > 11.5f)   { r = 112; g = 108; b = 102; }
            else { float t = Clamp((h - 0.7f) / 8.0f, 0, 1); r = (int)(78 + t * 20); g = (int)(128 + t * 10); b = (int)(52 + t * 10); }
            Color c = Col((int)((r + rnd) * shade), (int)((g + rnd) * shade), (int)((b + rnd) * shade));
            m.colors[k * 4] = c.r; m.colors[k * 4 + 1] = c.g; m.colors[k * 4 + 2] = c.b; m.colors[k * 4 + 3] = 255;
        }
    }
    int t = 0;
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            unsigned short v0 = (unsigned short)(j * (N + 1) + i), v1 = v0 + 1, v2 = (unsigned short)(v0 + N + 1), v3 = v2 + 1;
            m.indices[t++] = v0; m.indices[t++] = v2; m.indices[t++] = v1;
            m.indices[t++] = v1; m.indices[t++] = v2; m.indices[t++] = v3;
        }
    UploadMesh(&m, false);
    return m;
}

// ------------------------------------------------------------ geometry helpers
static float RaySphere(const Ray& r, Vector3 c, float rad) {
    Vector3 oc = Vector3Subtract(c, r.position);
    float tca = Vector3DotProduct(oc, r.direction);
    if (tca < 0) return -1;
    float d2 = Vector3DotProduct(oc, oc) - tca * tca;
    if (d2 > rad * rad) return -1;
    float thc = sqrtf(rad * rad - d2);
    return (tca - thc > 0) ? tca - thc : tca + thc;
}

// ------------------------------------------------------------ world objects
struct Node { int kind; Vector3 pos; float scale; float amt; float metal; bool alive; float respawn; float shake; };
struct Animal {
    int kind; Vector3 pos; float yaw; float hp; float cd; float wander; bool moving; bool alive; float respawn; bool aggro; float hurt;
};
static std::vector<Node> nodes;
static std::vector<Animal> animals;

// ------------------------------------------------------------ building
enum PT { P_FOUND = 0, P_WALL = 1, P_DOOR = 2, P_FLOOR = 3 };
struct Piece { PT t = P_FOUND; int tier = 0; int cx = 0, cz = 0, orient = 0, level = 0; float baseY = 0, depth = 0.5f; };
struct Box { BoundingBox bb; bool walk; int tier; int pi; };
static std::vector<Piece> pieces;
static std::vector<Box> gBoxes;
static bool gDirty = true;

static const char* PT_NAME[4]   = { "Фундамент", "Стіна", "Дверний проріз", "Дах" };
static const char* TIER_NAME[3] = { "Дерево", "Камінь", "Метал" };
static const Color TIER_COL[3]  = { { 156, 112, 68, 255 }, { 138, 138, 144, 255 }, { 108, 122, 148, 255 } };

static int PieceCost(PT t, int tier) {
    static const int base[4] = { 80, 50, 40, 60 };
    float m = tier == 0 ? 1.0f : (tier == 1 ? 1.5f : 0.5f);
    return (int)ceilf(base[t] * m);
}
static void PieceBoxes(const Piece& p, int pi, std::vector<Box>& out) {
    float x0 = p.cx * CELL, z0 = p.cz * CELL;
    const float T = 0.12f;
    auto add = [&](float a, float b, float c, float d, float e, float f, bool walk) {
        out.push_back(Box{ BoundingBox{ Vector3{ a, b, c }, Vector3{ d, e, f } }, walk, p.tier, pi });
    };
    switch (p.t) {
    case P_FOUND: add(x0, p.baseY - p.depth, z0, x0 + CELL, p.baseY, z0 + CELL, true); break;
    case P_FLOOR: add(x0, p.baseY - 0.25f, z0, x0 + CELL, p.baseY, z0 + CELL, true); break;
    case P_WALL:
        if (p.orient == 0) add(x0, p.baseY, z0 - T, x0 + CELL, p.baseY + WALL_H, z0 + T, false);
        else               add(x0 - T, p.baseY, z0, x0 + T, p.baseY + WALL_H, z0 + CELL, false);
        break;
    case P_DOOR:
        if (p.orient == 0) {
            add(x0, p.baseY, z0 - T, x0 + 0.7f, p.baseY + WALL_H, z0 + T, false);
            add(x0 + CELL - 0.7f, p.baseY, z0 - T, x0 + CELL, p.baseY + WALL_H, z0 + T, false);
            add(x0 + 0.7f, p.baseY + 2.2f, z0 - T, x0 + CELL - 0.7f, p.baseY + WALL_H, z0 + T, false);
        } else {
            add(x0 - T, p.baseY, z0, x0 + T, p.baseY + WALL_H, z0 + 0.7f, false);
            add(x0 - T, p.baseY, z0 + CELL - 0.7f, x0 + T, p.baseY + WALL_H, z0 + CELL, false);
            add(x0 - T, p.baseY + 2.2f, z0 + 0.7f, x0 + T, p.baseY + WALL_H, z0 + CELL - 0.7f, false);
        }
        break;
    }
}
static void RebuildBoxes() {
    gBoxes.clear();
    for (size_t i = 0; i < pieces.size(); i++) PieceBoxes(pieces[i], (int)i, gBoxes);
    gDirty = false;
}
static Piece* FindF(int cx, int cz) {
    for (auto& p : pieces) if (p.t == P_FOUND && p.cx == cx && p.cz == cz) return &p;
    return nullptr;
}
static Piece* FindW(int cx, int cz, int o, int L) {
    for (auto& p : pieces) if ((p.t == P_WALL || p.t == P_DOOR) && p.cx == cx && p.cz == cz && p.orient == o && p.level == L) return &p;
    return nullptr;
}
static Piece* FindFl(int cx, int cz, int L) {
    for (auto& p : pieces) if (p.t == P_FLOOR && p.cx == cx && p.cz == cz && p.level == L) return &p;
    return nullptr;
}
// Повертає true, якщо деталь можна поставити. g заповнюється завжди (для "привида").
static bool ComputePlacement(PT type, int tier, Vector3 aim, Piece& g) {
    g = Piece(); g.t = type; g.tier = tier; g.baseY = aim.y;
    float fx = aim.x / CELL, fz = aim.z / CELL;
    int cx = (int)floorf(fx), cz = (int)floorf(fz);
    if (type == P_FOUND) {
        g.cx = cx; g.cz = cz;
        float mx = -1e9f, mn = 1e9f;
        for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
            float h = TerrainH((cx + a) * CELL, (cz + b) * CELL);
            mx = fmaxf(mx, h); mn = fminf(mn, h);
        }
        Piece* nb = FindF(cx + 1, cz); if (!nb) nb = FindF(cx - 1, cz);
        if (!nb) nb = FindF(cx, cz + 1); if (!nb) nb = FindF(cx, cz - 1);
        g.baseY = nb ? nb->baseY : mx + 0.3f;
        g.depth = fmaxf(0.5f, g.baseY - (mn - 0.6f));
        if (FindF(cx, cz)) return false;
        if (mx < 0.3f) return false;
        if (g.baseY - mn > 3.5f) return false;
        if (g.baseY < mx - 0.3f) return false;
        return true;
    }
    if (type == P_WALL || type == P_DOOR) {
        float dS = fz - cz, dN = 1 - dS, dW = fx - cx, dE = 1 - dW;
        float m = fminf(fminf(dS, dN), fminf(dW, dE));
        int o, ex, ez;
        if (m == dS)      { o = 0; ex = cx;     ez = cz; }
        else if (m == dN) { o = 0; ex = cx;     ez = cz + 1; }
        else if (m == dW) { o = 1; ex = cx;     ez = cz; }
        else              { o = 1; ex = cx + 1; ez = cz; }
        g.cx = ex; g.cz = ez; g.orient = o;
        int L = 0;
        while (FindW(ex, ez, o, L)) L++;
        if (L > 6) return false;
        g.level = L;
        if (L == 0) {
            Piece* f1 = FindF(ex, ez);
            Piece* f2 = (o == 0) ? FindF(ex, ez - 1) : FindF(ex - 1, ez);
            Piece* ff = f1 ? f1 : f2;
            if (!ff) return false;
            g.baseY = ff->baseY;
            return true;
        }
        Piece* below = FindW(ex, ez, o, L - 1);
        g.baseY = below->baseY + WALL_H;
        return true;
    }
    // дах / стеля
    g.cx = cx; g.cz = cz;
    int L = 1;
    while (FindFl(cx, cz, L)) L++;
    g.level = L;
    Piece* w = FindW(cx, cz, 0, L - 1);
    if (!w) w = FindW(cx, cz + 1, 0, L - 1);
    if (!w) w = FindW(cx, cz, 1, L - 1);
    if (!w) w = FindW(cx + 1, cz, 1, L - 1);
    if (!w) { g.baseY = aim.y + 1.0f; return false; }
    g.baseY = w->baseY + WALL_H;
    return true;
}

// ------------------------------------------------------------ collisions
static float GroundAt(float x, float z, float feetY) {
    float g = TerrainH(x, z);
    for (const Box& b : gBoxes) {
        if (!b.walk) continue;
        const BoundingBox& bb = b.bb;
        if (x >= bb.min.x && x <= bb.max.x && z >= bb.min.z && z <= bb.max.z && bb.max.y <= feetY + STEP_H && bb.max.y > g) g = bb.max.y;
    }
    return g;
}
static void ResolveBoxes(Vector3& p, float r, float h, float step) {
    for (const Box& b : gBoxes) {
        const BoundingBox& bb = b.bb;
        if (p.y + step >= bb.max.y || p.y + h <= bb.min.y) continue;
        if (p.x < bb.min.x - r || p.x > bb.max.x + r || p.z < bb.min.z - r || p.z > bb.max.z + r) continue;
        float cx = Clamp(p.x, bb.min.x, bb.max.x), cz = Clamp(p.z, bb.min.z, bb.max.z);
        float dx = p.x - cx, dz = p.z - cz, d2 = dx * dx + dz * dz;
        if (d2 >= r * r) continue;
        if (d2 > 1e-8f) { float d = sqrtf(d2), push = r - d; p.x += dx / d * push; p.z += dz / d * push; }
        else {
            float l = p.x - bb.min.x, rr = bb.max.x - p.x, f = p.z - bb.min.z, bk = bb.max.z - p.z;
            float m = fminf(fminf(l, rr), fminf(f, bk));
            if (m == l) p.x = bb.min.x - r; else if (m == rr) p.x = bb.max.x + r;
            else if (m == f) p.z = bb.min.z - r; else p.z = bb.max.z + r;
        }
    }
}
static bool AimPoint(const Ray& ray, float maxd, Vector3& out, int* boxIdx = nullptr) {
    float best = maxd; bool hit = false; int bi = -1;
    for (size_t i = 0; i < gBoxes.size(); i++) {
        RayCollision rc = GetRayCollisionBox(ray, gBoxes[i].bb);
        if (rc.hit && rc.distance < best) { best = rc.distance; hit = true; bi = (int)i; }
    }
    for (float t = 0; t < best; t += 0.12f) {
        Vector3 p = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
        if (p.y < TerrainH(p.x, p.z)) { best = t; hit = true; bi = -1; break; }
    }
    out = Vector3Add(ray.position, Vector3Scale(ray.direction, best));
    if (boxIdx) *boxIdx = bi;
    return hit;
}

// ------------------------------------------------------------ crafting / inventory
struct Recipe { const char* name; int slot; int wood, stone, metal; };
static const Recipe REC[4] = {
    { "Сокира", 1, 60, 20, 0 }, { "Кирка", 2, 60, 40, 0 }, { "Спис", 3, 120, 40, 0 }, { "Будівельний план", 4, 100, 0, 0 } };
static const char* SLOT_NAME[5] = { "Камінь", "Сокира", "Кирка", "Спис", "План" };
static int  inv[4] = { 0, 0, 0, 0 };   // дерево, камінь, метал, м'ясо
static bool has[5] = { true, false, false, false, false };

struct Pop { std::string s; float t; };
static std::vector<Pop> gPops;
static void AddPop(const std::string& s) { gPops.push_back(Pop{ s, 2.2f }); if (gPops.size() > 6) gPops.erase(gPops.begin()); }

// ------------------------------------------------------------ drawing of objects
static void DrawNode(const Node& n) {
    float sx = n.shake > 0 ? sinf(n.shake * 40.0f) * 0.08f * n.shake : 0.0f;
    float s = n.scale;
    if (n.kind == 0) {
        Vector3 p = { n.pos.x + sx, n.pos.y - 0.1f, n.pos.z };
        DrawCylinder(p, 0.28f * s, 0.42f * s, 4.2f * s, 7, Col(104, 76, 48));
        DrawCylinder(Vector3{ p.x, p.y + 2.2f * s, p.z }, 0.0f, 2.1f * s, 3.6f * s, 8, Col(40, 102, 48));
        DrawCylinder(Vector3{ p.x, p.y + 4.2f * s, p.z }, 0.0f, 1.4f * s, 2.6f * s, 8, Col(52, 122, 58));
    } else {
        Vector3 c = { n.pos.x + sx, n.pos.y + 0.55f * s, n.pos.z };
        DrawSphereEx(c, 1.1f * s, 5, 6, Col(118, 118, 122));
        DrawSphereEx(Vector3{ c.x + 0.7f * s, c.y - 0.3f * s, c.z + 0.4f * s }, 0.6f * s, 4, 5, Col(100, 100, 104));
        if (n.metal > 0) DrawSphereEx(Vector3{ c.x - 0.3f * s, c.y + 0.8f * s, c.z + 0.2f * s }, 0.32f * s, 4, 4, Col(176, 112, 66));
    }
}
static void DrawAnimal(const Animal& a) {
    static const Color col[3] = { { 120, 84, 56, 255 }, { 112, 114, 122, 255 }, { 72, 50, 36, 255 } };
    static const Vector3 sz[3] = { { 0.7f, 0.7f, 1.3f }, { 0.55f, 0.65f, 1.25f }, { 1.0f, 1.15f, 1.9f } };
    Color c = col[a.kind];
    if (a.hurt > 0) { float k = Clamp(a.hurt * 4.0f, 0, 1); c = Col((int)(c.r + (230 - c.r) * k), (int)(c.g * (1 - k)), (int)(c.b * (1 - k))); }
    Vector3 s = sz[a.kind];
    rlPushMatrix();
    rlTranslatef(a.pos.x, a.pos.y, a.pos.z);
    rlRotatef(a.yaw * RAD2DEG, 0, 1, 0);
    float leg = s.y * 0.45f;
    DrawCube(Vector3{ 0, leg + s.y * 0.5f, 0 }, s.x, s.y, s.z, c);
    DrawCube(Vector3{ 0, leg + s.y * 0.75f, s.z * 0.55f }, s.x * 0.7f, s.y * 0.65f, s.z * 0.35f, c);
    for (int i = 0; i < 4; i++) {
        float lx = (i & 1 ? 1 : -1) * s.x * 0.35f, lz = (i & 2 ? 1 : -1) * s.z * 0.33f;
        DrawCube(Vector3{ lx, leg * 0.5f, lz }, 0.14f, leg, 0.14f, Col(c.r * 0.7f, c.g * 0.7f, c.b * 0.7f));
    }
    if (a.kind != 0) { DrawCube(Vector3{ s.x * 0.2f, leg + s.y * 0.85f, s.z * 0.74f }, 0.06f, 0.06f, 0.04f, RED);
                       DrawCube(Vector3{ -s.x * 0.2f, leg + s.y * 0.85f, s.z * 0.74f }, 0.06f, 0.06f, 0.04f, RED); }
    rlPopMatrix();
}
static void DrawBoxes() {
    for (const Box& b : gBoxes) {
        Vector3 c = { (b.bb.min.x + b.bb.max.x) * 0.5f, (b.bb.min.y + b.bb.max.y) * 0.5f, (b.bb.min.z + b.bb.max.z) * 0.5f };
        Vector3 s = { b.bb.max.x - b.bb.min.x, b.bb.max.y - b.bb.min.y, b.bb.max.z - b.bb.min.z };
        DrawCubeV(c, s, TIER_COL[b.tier]);
        DrawCubeWiresV(c, s, Fade(BLACK, 0.55f));
    }
}

// ------------------------------------------------------------ animals
static const float A_HP[3]    = { 50, 60, 200 };
static const float A_SPEED[3] = { 5.0f, 6.4f, 5.4f };
static const float A_DMG[3]   = { 8, 12, 28 };
static const float A_CD[3]    = { 1.0f, 1.0f, 1.4f };
static const float A_RANGE[3] = { 1.7f, 1.7f, 2.4f };
static const float A_RAD[3]   = { 0.8f, 0.75f, 1.25f };
static const int   A_MEAT[3]  = { 6, 4, 10 };
static const char* A_NAME[3]  = { "кабан", "вовк", "ведмідь" };

static void SpawnAnimal(Animal& a, int kind, Vector3 avoid, float minDist) {
    for (int tries = 0; tries < 200; tries++) {
        float x = Rf(-170, 170), z = Rf(-170, 170), h = TerrainH(x, z);
        if (h < 1.0f) continue;
        if (Dist2(Vector3{ x, h, z }, avoid) < minDist * minDist) continue;
        a = Animal{ kind, Vector3{ x, h, z }, Rf(0, 6.28f), A_HP[kind], 0, 0, false, true, 0, false, 0 };
        return;
    }
    a.alive = false; a.respawn = 20;
}

// =====================================================================
int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "RUST CLONE - Survival Sandbox");
    SetExitKey(KEY_NULL);
    SetTargetFPS(144);
    rlSetClipPlanes(0.08, 900.0);
    LoadUiFont();
    SetRandomSeed(2026);

    Model terrain = LoadModelFromMesh(BuildTerrainMesh());

    // ---------- світ
    for (int i = 0; i < 4000 && (int)nodes.size() < 420; i++) {
        float x = Rf(-175, 175), z = Rf(-175, 175), h = TerrainH(x, z);
        if (h < 1.0f || h > 10.0f) continue;
        if (fabsf(TerrainH(x + 1.5f, z) - TerrainH(x - 1.5f, z)) > 1.6f) continue;
        nodes.push_back(Node{ 0, Vector3{ x, h, z }, Rf(0.8f, 1.5f), 300, 0, true, 0, 0 });
    }
    int trees = (int)nodes.size();
    for (int i = 0; i < 4000 && (int)nodes.size() < trees + 150; i++) {
        float x = Rf(-175, 175), z = Rf(-175, 175), h = TerrainH(x, z);
        if (h < 0.6f) continue;
        nodes.push_back(Node{ 1, Vector3{ x, h, z }, Rf(0.8f, 1.7f), 250, (GetRandomValue(0, 2) == 0) ? 60.0f : 0.0f, true, 0, 0 });
    }
    Vector3 spawn = { 0, 5, 0 };
    for (float r = 170; r > 0; r -= 2) {
        float x = cosf(0.8f) * r, z = sinf(0.8f) * r;
        if (TerrainH(x, z) > 1.2f) { spawn = Vector3{ x, TerrainH(x, z), z }; break; }
    }
    animals.resize(22);
    for (int i = 0; i < 22; i++) SpawnAnimal(animals[i], i < 10 ? 0 : (i < 18 ? 1 : 2), spawn, i < 10 ? 40.0f : 80.0f);

    // ---------- стан гравця
    Vector3 pos = spawn, vel = { 0, 0, 0 };
    float yaw = 3.9f, pitch = 0.0f, eyeH = 1.65f;
    bool onGround = false, dead = false, paused = false, craftOpen = false, cursorShown = false;
    float hp = 100, hunger = 100, thirst = 100;
    float hour = 9.0f, hitCd = 0, swing = 0, drinkCd = 0, hurtFlash = 0, hitMark = 0, helpT = 25.0f;
    int slot = 0, planType = 0, planTier = 0;
    DisableCursor();

    Camera3D cam = { 0 };
    cam.up = Vector3{ 0, 1, 0 }; cam.fovy = 72.0f; cam.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        float dt = fminf(GetFrameTime(), 0.05f);
        int sw = GetScreenWidth(), sh = GetScreenHeight();
        if (gDirty) RebuildBoxes();

        // ---------- системні кнопки
        if (IsKeyPressed(KEY_F10)) break;
        if (IsKeyPressed(KEY_ESCAPE)) { if (craftOpen) craftOpen = false; else paused = !paused; }
        if (IsKeyPressed(KEY_TAB) && !dead && !paused) craftOpen = !craftOpen;
        bool wantCursor = paused || craftOpen || dead;
        if (wantCursor != cursorShown) { if (wantCursor) EnableCursor(); else DisableCursor(); cursorShown = wantCursor; }

        bool inputOn = !paused && !craftOpen && !dead;
        Vector3 fwd3 = { cosf(pitch) * sinf(yaw), sinf(pitch), -cosf(pitch) * cosf(yaw) };
        Ray look = { Vector3{ pos.x, pos.y + eyeH, pos.z }, fwd3 };

        // =============================================== UPDATE
        if (!paused) {
            hour += dt * 24.0f / 900.0f; if (hour >= 24.0f) hour -= 24.0f;
            hitCd -= dt; swing -= dt * 2.4f; drinkCd -= dt; hurtFlash -= dt * 1.6f; hitMark -= dt; helpT -= dt;
            for (auto& p : gPops) p.t -= dt;
            gPops.erase(std::remove_if(gPops.begin(), gPops.end(), [](const Pop& p) { return p.t <= 0; }), gPops.end());

            // --- погляд
            if (inputOn) {
                Vector2 md = GetMouseDelta();
                yaw += md.x * 0.0022f; pitch -= md.y * 0.0022f;
                pitch = Clamp(pitch, -1.5f, 1.5f);
                for (int i = 0; i < 5; i++) if (IsKeyPressed(KEY_ONE + i) && has[i]) slot = i;
                float wh = GetMouseWheelMove();
                if (wh != 0) { int d = wh > 0 ? -1 : 1; for (int k = 0; k < 5; k++) { slot = (slot + d + 5) % 5; if (has[slot]) break; } }
            }

            // --- рух
            if (!dead) {
                bool crouch = inputOn && IsKeyDown(KEY_LEFT_CONTROL);
                bool sprint = inputOn && IsKeyDown(KEY_LEFT_SHIFT) && !crouch && hunger > 0;
                float mx = 0, mz = 0;
                if (inputOn) { mx = (float)(IsKeyDown(KEY_D) - IsKeyDown(KEY_A)); mz = (float)(IsKeyDown(KEY_W) - IsKeyDown(KEY_S)); }
                Vector3 fl = { sinf(yaw), 0, -cosf(yaw) }, rt = { cosf(yaw), 0, sinf(yaw) };
                Vector3 wish = Vector3Add(Vector3Scale(fl, mz), Vector3Scale(rt, mx));
                float wl = Vector3Length(wish);
                if (wl > 0.001f) wish = Vector3Scale(wish, 1.0f / wl);
                float speed = crouch ? 2.2f : (sprint ? 6.6f : 4.3f);
                if (pos.y < WATER_Y + 0.4f) speed *= 0.55f;
                Vector3 np = pos;
                np.x += wish.x * speed * dt;
                if (TerrainH(np.x, pos.z) > -2.5f) pos.x = np.x;
                np.z += wish.z * speed * dt;
                if (TerrainH(pos.x, np.z) > -2.5f) pos.z = np.z;
                ResolveBoxes(pos, PL_R, PL_H, STEP_H);

                eyeH += ((crouch ? 1.2f : 1.65f) - eyeH) * fminf(1.0f, dt * 12.0f);
                if (inputOn && IsKeyPressed(KEY_SPACE) && onGround) { vel.y = 7.0f; onGround = false; }
                vel.y -= 22.0f * dt;
                float g = GroundAt(pos.x, pos.z, pos.y);
                pos.y += vel.y * dt;
                if (pos.y <= g) {
                    if (vel.y < -14.0f) { hp -= (-vel.y - 14.0f) * 5.0f; hurtFlash = 0.6f; }
                    pos.y = g; vel.y = 0; onGround = true;
                } else onGround = false;
                if (pos.y < WATER_Y - 3.0f) pos.y = WATER_Y - 3.0f;

                // --- голод / спрага / здоров'я
                hunger = fmaxf(0, hunger - (sprint ? 0.2f : 0.11f) * dt);
                thirst = fmaxf(0, thirst - (sprint ? 0.26f : 0.16f) * dt);
                if (hunger <= 0 || thirst <= 0) hp -= 0.8f * dt;
                else if (hunger > 40 && thirst > 40) hp = fminf(100, hp + 0.4f * dt);
                if (hp <= 0) { hp = 0; dead = true; }

                // --- дії
                if (inputOn) {
                    if (IsKeyPressed(KEY_F) && inv[3] > 0) { inv[3]--; hunger = fminf(100, hunger + 22); AddPop("З'їдено сире м'ясо"); }
                    Vector3 ahead = Vector3Add(pos, Vector3Scale(Vector3{ fl.x, 0, fl.z }, 2.4f));
                    if (IsKeyPressed(KEY_E) && drinkCd <= 0 && (TerrainH(ahead.x, ahead.z) < WATER_Y - 0.1f || pos.y < WATER_Y + 0.3f)) {
                        thirst = fminf(100, thirst + 28); drinkCd = 0.6f; AddPop("Ви напилися води");
                    }
                    if (slot <= 3 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hitCd <= 0) {
                        float reach = (slot == 3) ? 4.4f : 3.3f;
                        float best = reach; int what = -1, idx = -1;
                        for (size_t i = 0; i < nodes.size(); i++) {
                            Node& n = nodes[i];
                            if (!n.alive || Dist2(n.pos, pos) > 100.0f) continue;
                            float t = -1;
                            if (n.kind == 0) {
                                for (int k = 0; k < 3; k++) {
                                    float tt = RaySphere(look, Vector3{ n.pos.x, n.pos.y + (1.2f + k * 1.4f) * n.scale, n.pos.z }, 0.6f * n.scale);
                                    if (tt >= 0 && (t < 0 || tt < t)) t = tt;
                                }
                            } else t = RaySphere(look, Vector3{ n.pos.x, n.pos.y + 0.55f * n.scale, n.pos.z }, 1.15f * n.scale);
                            if (t >= 0 && t < best) { best = t; what = 0; idx = (int)i; }
                        }
                        for (size_t i = 0; i < animals.size(); i++) {
                            Animal& a = animals[i];
                            if (!a.alive) continue;
                            float t = RaySphere(look, Vector3{ a.pos.x, a.pos.y + 0.8f, a.pos.z }, A_RAD[a.kind]);
                            if (t >= 0 && t < best) { best = t; what = 1; idx = (int)i; }
                        }
                        hitCd = (slot == 3) ? 0.65f : 0.5f; swing = 1.0f;
                        char buf[64];
                        if (what == 0) {
                            Node& n = nodes[idx]; n.shake = 0.35f; hitMark = 0.15f;
                            if (n.kind == 0) {
                                static const int yw[4] = { 8, 30, 10, 5 };
                                int gain = std::min((int)n.amt, yw[slot]); n.amt -= gain; inv[0] += gain;
                                snprintf(buf, sizeof buf, "+%d дерево", gain); AddPop(buf);
                            } else {
                                static const int ys[4] = { 8, 10, 30, 3 };
                                int gain = std::min((int)n.amt, ys[slot]); n.amt -= gain; inv[1] += gain;
                                snprintf(buf, sizeof buf, "+%d камінь", gain); AddPop(buf);
                                if (slot == 2 && n.metal > 0) {
                                    int mg = std::min((int)n.metal, 8); n.metal -= mg; inv[2] += mg;
                                    snprintf(buf, sizeof buf, "+%d метал", mg); AddPop(buf);
                                }
                            }
                            if (n.amt <= 0) { n.alive = false; n.respawn = 180; }
                        } else if (what == 1) {
                            Animal& a = animals[idx];
                            static const float dm[4] = { 5, 15, 12, 35 };
                            a.hp -= dm[slot]; a.hurt = 0.25f; a.aggro = true; hitMark = 0.2f;
                            if (a.hp <= 0) {
                                a.alive = false; a.respawn = 120; inv[3] += A_MEAT[a.kind];
                                snprintf(buf, sizeof buf, "Вбито: %s  +%d м'яса", A_NAME[a.kind], A_MEAT[a.kind]); AddPop(buf);
                            }
                        }
                    }
                }
            }
        }

        // --- будівництво (прицілювання)
        Piece ghost; bool ghostShow = false, ghostOk = false;
        if (inputOn && slot == 4 && has[4]) {
            if (IsKeyPressed(KEY_Q)) planType = (planType + 1) % 4;
            if (IsKeyPressed(KEY_T)) planTier = (planTier + 1) % 3;
            Vector3 aim;
            if (AimPoint(look, 9.0f, aim)) {
                ghostShow = true;
                ghostOk = ComputePlacement((PT)planType, planTier, aim, ghost) && inv[planTier] >= PieceCost((PT)planType, planTier);
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ghostShow && ghostOk) {
                inv[planTier] -= PieceCost((PT)planType, planTier);
                pieces.push_back(ghost); gDirty = true; RebuildBoxes();
                AddPop(std::string("Збудовано: ") + PT_NAME[planType]);
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                int bi; Vector3 ap;
                if (AimPoint(look, 8.0f, ap, &bi) && bi >= 0) {
                    Piece& pc = pieces[gBoxes[bi].pi];
                    inv[pc.tier] += PieceCost(pc.t, pc.tier) / 2;
                    pieces.erase(pieces.begin() + gBoxes[bi].pi); gDirty = true; RebuildBoxes();
                    AddPop("Деталь розібрано (50% повернено)");
                }
            }
        }

        if (!paused) {
            // --- ресурси (відродження)
            for (auto& n : nodes) {
                if (n.shake > 0) n.shake -= dt;
                if (!n.alive && (n.respawn -= dt) <= 0) { n.alive = true; n.amt = n.kind == 0 ? 300.0f : 250.0f; if (n.kind == 1 && GetRandomValue(0, 2) == 0) n.metal = 60; }
            }
            // --- звірі
            for (auto& a : animals) {
                if (!a.alive) { if ((a.respawn -= dt) <= 0) SpawnAnimal(a, a.kind, pos, 60); continue; }
                a.cd -= dt; a.hurt -= dt;
                float dx = pos.x - a.pos.x, dz = pos.z - a.pos.z, dist = sqrtf(dx * dx + dz * dz);
                bool chase = !dead && ((a.kind == 1 && dist < 28) || (a.kind == 2 && dist < 20) || (a.aggro && dist < 35));
                float spd = 0;
                if (chase) {
                    a.yaw = atan2f(dx, dz);
                    spd = A_SPEED[a.kind];
                    if (dist < A_RANGE[a.kind]) {
                        spd = 0;
                        if (a.cd <= 0 && fabsf(pos.y - a.pos.y) < 2.0f) { hp -= A_DMG[a.kind]; hurtFlash = 0.8f; a.cd = A_CD[a.kind]; if (hp <= 0) { hp = 0; dead = true; } }
                    }
                } else {
                    if ((a.wander -= dt) <= 0) { a.yaw = Rf(0, 6.28f); a.wander = Rf(3, 7); a.moving = GetRandomValue(0, 2) > 0; }
                    spd = a.moving ? 1.6f : 0;
                }
                if (spd > 0) {
                    Vector3 np = { a.pos.x + sinf(a.yaw) * spd * dt, a.pos.y, a.pos.z + cosf(a.yaw) * spd * dt };
                    if (TerrainH(np.x, np.z) > 0.2f) { a.pos.x = np.x; a.pos.z = np.z; ResolveBoxes(a.pos, 0.6f, 1.0f, 0.3f); }
                    else a.wander = 0;
                }
                a.pos.y = TerrainH(a.pos.x, a.pos.z);
            }
        }

        // --- respawn
        if (dead && IsKeyPressed(KEY_R)) {
            dead = false; hp = hunger = thirst = 100; pos = spawn; vel = Vector3{ 0, 0, 0 };
            for (int i = 0; i < 4; i++) inv[i] = 0;
            for (int i = 1; i < 5; i++) has[i] = false;
            slot = 0; AddPop("Ви відродилися. Інвентар втрачено.");
        }

        // =============================================== DRAW
        cam.position = Vector3{ pos.x, pos.y + eyeH, pos.z };
        cam.target = Vector3Add(cam.position, fwd3);

        float sunH = sinf((hour - 6.0f) / 24.0f * 2.0f * PI);
        float day = Clamp(sunH * 2.2f + 0.35f, 0.0f, 1.0f);
        Color sky = Col((int)(6 + 119 * day), (int)(10 + 175 * day), (int)(26 + 214 * day));

        BeginDrawing();
        ClearBackground(sky);
        BeginMode3D(cam);
        rlDisableBackfaceCulling();
        DrawModel(terrain, Vector3{ 0, 0, 0 }, 1.0f, WHITE);
        rlEnableBackfaceCulling();
        for (const Node& n : nodes) if (n.alive && Dist2(n.pos, pos) < 170.0f * 170.0f) DrawNode(n);
        DrawBoxes();
        for (const Animal& a : animals) if (a.alive && Dist2(a.pos, pos) < 170.0f * 170.0f) DrawAnimal(a);
        if (ghostShow) {
            std::vector<Box> gb; PieceBoxes(ghost, 0, gb);
            for (const Box& b : gb) {
                Vector3 c = { (b.bb.min.x + b.bb.max.x) * 0.5f, (b.bb.min.y + b.bb.max.y) * 0.5f, (b.bb.min.z + b.bb.max.z) * 0.5f };
                Vector3 s = { b.bb.max.x - b.bb.min.x + 0.02f, b.bb.max.y - b.bb.min.y + 0.02f, b.bb.max.z - b.bb.min.z + 0.02f };
                DrawCubeV(c, s, Fade(ghostOk ? GREEN : RED, 0.45f));
                DrawCubeWiresV(c, s, ghostOk ? GREEN : RED);
            }
        }
        DrawPlane(Vector3{ 0, WATER_Y, 0 }, Vector2{ WORLD * 2, WORLD * 2 }, Col(40, 105, 175, 170));
        EndMode3D();

        // --- ніч / під водою / шкода
        DrawRectangle(0, 0, sw, sh, Col(4, 8, 30, (int)((1.0f - day) * 165)));
        if (cam.position.y < WATER_Y) DrawRectangle(0, 0, sw, sh, Col(20, 70, 150, 90));
        if (hurtFlash > 0) DrawRectangle(0, 0, sw, sh, Col(200, 0, 0, (int)(Clamp(hurtFlash, 0, 1) * 110)));

        // --- приціл
        float cxs = sw * 0.5f, cys = sh * 0.5f;
        if (!dead && !craftOpen) {
            DrawCircle((int)cxs, (int)cys, 2.5f, Fade(WHITE, 0.9f));
            if (hitMark > 0) {
                DrawLineEx(Vector2{ cxs - 9, cys - 9 }, Vector2{ cxs - 4, cys - 4 }, 2, RED); DrawLineEx(Vector2{ cxs + 9, cys - 9 }, Vector2{ cxs + 4, cys - 4 }, 2, RED);
                DrawLineEx(Vector2{ cxs - 9, cys + 9 }, Vector2{ cxs - 4, cys + 4 }, 2, RED); DrawLineEx(Vector2{ cxs + 9, cys + 9 }, Vector2{ cxs + 4, cys + 4 }, 2, RED);
            }
        }

        // --- в'юмодель (інструмент у руці)
        if (!dead && slot <= 3 && !craftOpen) {
            float sw01 = Clamp(swing, 0, 1);
            float ang = -28.0f + sinf(sw01 * PI) * -55.0f + (1 - sw01) * 0.0f;
            float px = sw * 0.80f, py = sh * 1.02f;
            Color wood = Col(120, 84, 52), metal = Col(150, 150, 158);
            if (slot == 0) DrawCircle((int)(px - 40 - sinf(sw01 * PI) * 70), (int)(py - 140 - sinf(sw01 * PI) * 50), 52, Col(112, 112, 116));
            else {
                float len = slot == 3 ? 330.0f : 190.0f;
                DrawRectanglePro(Rectangle{ px, py, 16, len }, Vector2{ 8, len }, ang, wood);
                Vector2 tip = { px + sinf(ang * DEG2RAD) * len, py - cosf(ang * DEG2RAD) * len };
                if (slot == 1) DrawRectanglePro(Rectangle{ tip.x, tip.y, 76, 30 }, Vector2{ 18, 15 }, ang, metal);
                if (slot == 2) DrawRectanglePro(Rectangle{ tip.x, tip.y, 110, 16 }, Vector2{ 55, 8 }, ang, metal);
                if (slot == 3) DrawRectanglePro(Rectangle{ tip.x, tip.y, 20, 60 }, Vector2{ 10, 55 }, ang, metal);
            }
        }
        if (!dead && slot == 4 && !craftOpen)
            DrawRectanglePro(Rectangle{ sw * 0.80f, sh * 0.98f, 150, 200 }, Vector2{ 75, 200 }, -12, Col(60, 90, 140, 200));

        // --- бари стану
        auto bar = [&](float x, float y, float v, Color c, const char* label) {
            DrawRectangle((int)x, (int)y, 230, 18, Fade(BLACK, 0.55f));
            DrawRectangle((int)x + 2, (int)y + 2, (int)(226 * Clamp(v, 0, 100) / 100.0f), 14, c);
            Txt(label, x + 8, y - 1, 16, WHITE);
        };
        bar(20, sh - 92, hp, Col(200, 50, 50), "Здоров'я");
        bar(20, sh - 66, hunger, Col(210, 140, 40), "Їжа");
        bar(20, sh - 40, thirst, Col(60, 140, 220), "Вода");

        // --- хотбар
        for (int i = 0; i < 5; i++) {
            float x = sw * 0.5f - 5 * 36 + i * 72 + 4, y = sh - 84;
            DrawRectangle((int)x, (int)y, 64, 64, Fade(BLACK, has[i] ? 0.6f : 0.3f));
            DrawRectangleLinesEx(Rectangle{ x, y, 64, 64 }, i == slot ? 3 : 1, i == slot ? Col(255, 200, 60) : Fade(WHITE, 0.4f));
            Txt(std::to_string(i + 1), x + 4, y + 2, 14, Fade(WHITE, 0.7f));
            TxtC(SLOT_NAME[i], x + 32, y + 24, 15, has[i] ? WHITE : Fade(WHITE, 0.25f));
        }

        // --- ресурси
        const char* RN[4] = { "Дерево", "Камінь", "Метал", "М'ясо" };
        for (int i = 0; i < 4; i++) {
            std::string s = std::string(RN[i]) + ": " + std::to_string(inv[i]);
            Txt(s, sw - TxtW(s, 20) - 20, 18 + i * 24, 20, Col(240, 230, 200));
        }
        char tbuf[32]; snprintf(tbuf, sizeof tbuf, "%02d:%02d", (int)hour, (int)((hour - floorf(hour)) * 60));
        Txt(tbuf, 20, 16, 22, WHITE);
        Txt(std::to_string(GetFPS()) + " FPS", 20, 42, 16, Fade(WHITE, 0.6f));

        // --- спливаючі повідомлення
        for (size_t i = 0; i < gPops.size(); i++)
            Txt(gPops[i].s, cxs + 40, cys - 20 - (float)(gPops.size() - 1 - i) * 24, 20, Fade(Col(255, 240, 160), Clamp(gPops[i].t, 0, 1)));

        // --- панель будівництва
        if (slot == 4 && has[4] && !dead && !craftOpen) {
            std::string a = std::string("[Q] Деталь: ") + PT_NAME[planType] + "    [T] Матеріал: " + TIER_NAME[planTier]
                          + "    Вартість: " + std::to_string(PieceCost((PT)planType, planTier)) + " (" + std::to_string(inv[planTier]) + ")";
            TxtC(a, cxs, 70, 20, WHITE);
            TxtC("ЛКМ - поставити   ПКМ - розібрати   (стіни потребують фундамент, дах - стіни)", cxs, 96, 16, Fade(WHITE, 0.7f));
        }

        // --- підказки
        if (helpT > 0 && !dead) {
            const char* H[] = { "WASD - рух, Shift - біг, Ctrl - присісти, Пробіл - стрибок",
                                "ЛКМ - бити/добувати,  1-5 - предмет,  Tab - крафт",
                                "E - пити воду (дивіться на воду),  F - їсти м'ясо",
                                "Esc - пауза,  F10 - вихід" };
            for (int i = 0; i < 4; i++) Txt(H[i], 20, 80 + i * 22, 17, Fade(WHITE, Clamp(helpT, 0, 1) * 0.9f));
        }

        // --- крафт
        if (craftOpen) {
            float pw = 560, ph = 90 + 4 * 84, px = (sw - pw) * 0.5f, py = (sh - ph) * 0.5f;
            DrawRectangle((int)px, (int)py, (int)pw, (int)ph, Fade(Col(20, 22, 26), 0.92f));
            DrawRectangleLinesEx(Rectangle{ px, py, pw, ph }, 2, Fade(WHITE, 0.5f));
            TxtC("КРАФТ   (Tab - закрити)", sw * 0.5f, py + 14, 26, WHITE);
            for (int i = 0; i < 4; i++) {
                const Recipe& r = REC[i];
                float y = py + 62 + i * 84;
                DrawRectangle((int)px + 14, (int)y, (int)pw - 28, 76, Fade(WHITE, 0.07f));
                Txt(r.name, px + 28, y + 10, 24, WHITE);
                std::string cost = "Дерево " + std::to_string(r.wood);
                if (r.stone) cost += "   Камінь " + std::to_string(r.stone);
                if (r.metal) cost += "   Метал " + std::to_string(r.metal);
                bool afford = inv[0] >= r.wood && inv[1] >= r.stone && inv[2] >= r.metal;
                Txt(cost, px + 28, y + 44, 17, afford ? Col(190, 230, 170) : Col(230, 150, 150));
                Rectangle btn = { px + pw - 168, y + 16, 138, 44 };
                bool hover = CheckCollisionPointRec(GetMousePosition(), btn);
                const char* label = has[r.slot] ? "Є" : "Створити";
                Color bc = has[r.slot] ? Col(70, 70, 70) : (afford ? (hover ? Col(80, 170, 80) : Col(60, 140, 60)) : Col(110, 60, 60));
                DrawRectangleRec(btn, bc);
                TxtC(label, btn.x + btn.width * 0.5f, btn.y + 10, 22, WHITE);
                if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && afford && !has[r.slot]) {
                    inv[0] -= r.wood; inv[1] -= r.stone; inv[2] -= r.metal; has[r.slot] = true;
                    AddPop(std::string("Створено: ") + r.name);
                }
            }
        }

        if (dead) {
            DrawRectangle(0, 0, sw, sh, Col(120, 0, 0, 140));
            TxtC("ВИ ЗАГИНУЛИ", sw * 0.5f, sh * 0.4f, 60, WHITE);
            TxtC("Натисніть R, щоб відродитися", sw * 0.5f, sh * 0.4f + 80, 28, WHITE);
        }
        if (paused) {
            DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.6f));
            TxtC("ПАУЗА", sw * 0.5f, sh * 0.4f, 56, WHITE);
            TxtC("Esc - продовжити     F10 - вийти", sw * 0.5f, sh * 0.4f + 76, 24, WHITE);
        }
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
