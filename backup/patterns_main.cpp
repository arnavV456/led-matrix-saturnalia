#include <Adafruit_NeoPixel.h>
#include <math.h>

#define LED_PIN 5
#define W 16
#define H 16
#define N (W * H)
#define BRIGHTNESS 255
#define PATTERN_MS 18000
#define FADE_MS 2500

Adafruit_NeoPixel strip(N, LED_PIN, NEO_GRB + NEO_KHZ800);

struct RGB { uint8_t r, g, b; };
struct F3 { float r, g, b; };

// Serpentine layout: even rows left->right, odd rows right->left.
// If the picture looks scrambled, flip the (y & 1) test or swap x/y here.
static inline uint16_t XY(int x, int y) {
  return y * W + ((y & 1) ? (W - 1 - x) : x);
}

// ---------- helpers ----------
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline uint8_t clamp8(float v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }
static inline float smooth(float a, float b, float x) {
  float t = clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline F3 mix3(F3 a, F3 b, float t) {
  return {lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t)};
}
static inline void put(RGB &o, F3 c, float b) {
  o.r = clamp8(c.r * b * 255);
  o.g = clamp8(c.g * b * 255);
  o.b = clamp8(c.b * b * 255);
}

// Cosine palettes: col = a + b * cos(2*pi*(c*t + d))
static const float PAL[5][4][3] = {
    {{.5, .5, .5}, {.5, .5, .5}, {1, 1, 1}, {0, .33, .67}},      // 0 rainbow
    {{.5, .5, .5}, {.5, .5, .5}, {1, 1, .5}, {.8, .9, .3}},      // 1 sunset
    {{.5, .5, .5}, {.5, .5, .5}, {1, .7, .4}, {0, .15, .2}},     // 2 candy
    {{.5, .5, .5}, {.5, .5, .5}, {2, 1, 0}, {.5, .2, .25}},      // 3 neon
    {{.2, .5, .6}, {.3, .4, .4}, {1, 1, 1}, {0, .1, .25}},       // 4 ocean
};
static F3 pal(float t, int p) {
  const float(*P)[3] = PAL[p];
  return {P[0][0] + P[1][0] * cosf(6.28318f * (P[2][0] * t + P[3][0])),
          P[0][1] + P[1][1] * cosf(6.28318f * (P[2][1] * t + P[3][1])),
          P[0][2] + P[1][2] * cosf(6.28318f * (P[2][2] * t + P[3][2]))};
}

// Smooth 3D value noise (x, y, time)
static inline float hash3(int x, int y, int z) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (h & 0xFFFF) / 65535.0f;
}
static float vnoise(float x, float y, float z) {
  int xi = floorf(x), yi = floorf(y), zi = floorf(z);
  float xf = x - xi, yf = y - yi, zf = z - zi;
  float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf), w = zf * zf * (3 - 2 * zf);
  float c00 = lerpf(hash3(xi, yi, zi), hash3(xi + 1, yi, zi), u);
  float c10 = lerpf(hash3(xi, yi + 1, zi), hash3(xi + 1, yi + 1, zi), u);
  float c01 = lerpf(hash3(xi, yi, zi + 1), hash3(xi + 1, yi, zi + 1), u);
  float c11 = lerpf(hash3(xi, yi + 1, zi + 1), hash3(xi + 1, yi + 1, zi + 1), u);
  return lerpf(lerpf(c00, c10, v), lerpf(c01, c11, v), w);
}

// Sub-pixel additive splat (gives smooth moving dots)
static void splat(RGB *buf, float x, float y, F3 c, float w) {
  int ix = floorf(x), iy = floorf(y);
  float fx = x - ix, fy = y - iy;
  for (int j = 0; j < 2; j++)
    for (int i = 0; i < 2; i++) {
      int X = ix + i, Y = iy + j;
      if (X < 0 || X >= W || Y < 0 || Y >= H) continue;
      float wt = (i ? fx : 1 - fx) * (j ? fy : 1 - fy) * w;
      RGB &p = buf[Y * W + X];
      p.r = clamp8(p.r + c.r * wt * 255);
      p.g = clamp8(p.g + c.g * wt * 255);
      p.b = clamp8(p.b + c.b * wt * 255);
    }
}
static void fadeBuf(RGB *buf, int k256) {
  for (int i = 0; i < N; i++) {
    buf[i].r = buf[i].r * k256 >> 8;
    buf[i].g = buf[i].g * k256 >> 8;
    buf[i].b = buf[i].b * k256 >> 8;
  }
}

// ---------- 0: liquid flow (domain-warped noise) ----------
void flow(float t, RGB *o) {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float fx = x * 0.16f, fy = y * 0.16f;
      float qx = vnoise(fx, fy, t * 0.25f);
      float qy = vnoise(fx + 5.2f, fy + 1.3f, t * 0.25f);
      float n = vnoise(fx + 2.6f * qx, fy + 2.6f * qy, t * 0.15f + 10);
      put(o[y * W + x], pal(n * 1.3f + t * 0.04f, 3), 0.2f + 0.8f * smooth(0.2f, 0.8f, n));
    }
}

// ---------- 1: hypnotic tunnel ----------
void tunnel(float t, RGB *o) {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float dx = x - 7.5f, dy = y - 7.5f;
      float r = hypotf(dx, dy), a = atan2f(dy, dx);
      float depth = 6.0f / (r + 0.8f);
      float v = 0.5f + 0.5f * sinf(a * 4.0f + depth * 2.0f - t * 3.0f);
      float b = v * v * smooth(0.3f, 3.0f, r);
      put(o[y * W + x], pal(depth * 0.12f + t * 0.1f, 0), b);
    }
}

// ---------- 2: spirograph with glowing trails ----------
static RGB spiro[N];
void spirograph(float t, RGB *o) {
  fadeBuf(spiro, 225);
  for (int k = 0; k < 4; k++)
    for (int s = 0; s < 4; s++) {
      float tt = (t - (3 - s) * 0.012f) * 1.2f + k * 1.5708f;
      float m = sinf(t * 0.2f) * 2.0f;
      float x = 7.5f + 7.0f * sinf(tt * 3.0f + m);
      float y = 7.5f + 7.0f * sinf(tt * 2.0f + cosf(t * 0.15f) * 2.0f);
      splat(spiro, x, y, pal(tt * 0.08f + k * 0.25f, 3), 0.7f);
    }
  memcpy(o, spiro, sizeof(spiro));
}

// ---------- 3: aurora ----------
void aurora(float t, RGB *o) {
  F3 green = {0.1f, 1.0f, 0.45f}, magenta = {0.75f, 0.15f, 1.0f};
  for (int x = 0; x < W; x++) {
    float yb = 3.0f + 9.0f * vnoise(x * 0.22f, t * 0.3f, 0);
    float ray = vnoise(x * 0.8f, t * 0.6f, 3);
    float wdt = 3.0f + 3.0f * ray;
    for (int y = 0; y < H; y++) {
      float band = smooth(0, 1, 1.0f - fabsf(y - yb) / wdt);
      float m = clamp01((y - yb) / 5.0f * 0.5f + 0.5f);
      put(o[y * W + x], mix3(green, magenta, m), band * (0.45f + 0.55f * ray));
    }
  }
}

// ---------- 4: metaballs / lava lamp ----------
void metaballs(float t, RGB *o) {
  float bx[5], by[5];
  for (int i = 0; i < 5; i++) {
    bx[i] = 7.5f + 6.0f * sinf(t * (0.5f + i * 0.17f) + i * 1.7f);
    by[i] = 7.5f + 6.0f * cosf(t * (0.43f + i * 0.13f) + i * 2.3f);
  }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float f = 0;
      for (int i = 0; i < 5; i++) {
        float dx = x - bx[i], dy = y - by[i];
        f += 3.0f / (dx * dx + dy * dy + 1.0f);
      }
      put(o[y * W + x], pal(f * 0.3f + t * 0.05f, 1), smooth(0.3f, 1.8f, f));
    }
}

// ---------- 5: warp-speed starfield ----------
struct Star { float x, y, z; };
static Star stars[40];
static RGB warpbuf[N];
static void respawn(Star &s, bool far) {
  s.x = random(-1000, 1000) / 1000.0f;
  s.y = random(-1000, 1000) / 1000.0f;
  s.z = far ? 1.0f : random(300, 1000) / 1000.0f;
}
void warp(float t, RGB *o) {
  fadeBuf(warpbuf, 150);
  float speed = 0.014f + 0.010f * sinf(t * 0.5f);
  F3 tint = mix3({1, 1, 1}, pal(t * 0.05f, 0), 0.5f);
  for (auto &s : stars) {
    float pz = s.z;
    s.z -= speed;
    if (s.z < 0.06f) { respawn(s, true); continue; }
    float cx = 7.5f + s.x / s.z * 3.2f, cy = 7.5f + s.y / s.z * 3.2f;
    float px = 7.5f + s.x / pz * 3.2f, py = 7.5f + s.y / pz * 3.2f;
    if (fabsf(cx - 7.5f) > 8.5f || fabsf(cy - 7.5f) > 8.5f) { respawn(s, true); continue; }
    float b = 0.25f + 0.75f * (1.0f - s.z);
    for (int k = 0; k < 3; k++) {
      float u = k / 3.0f;
      splat(warpbuf, lerpf(px, cx, u), lerpf(py, cy, u), tint, b * 0.5f);
    }
  }
  memcpy(o, warpbuf, sizeof(warpbuf));
}

// ---------- 6: rotozoom ----------
void rotozoom(float t, RGB *o) {
  float ang = t * 0.35f, ca = cosf(ang), sa = sinf(ang);
  float sc = 0.55f + 0.25f * sinf(t * 0.5f);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float dx = x - 7.5f, dy = y - 7.5f;
      float u = (dx * ca - dy * sa) * sc, v = (dx * sa + dy * ca) * sc;
      float val = 0.5f + 0.5f * sinf(u * 1.6f) * sinf(v * 1.6f);
      put(o[y * W + x], pal(val * 0.5f + (u + v) * 0.05f + t * 0.08f, 2), smooth(0.1f, 0.9f, val));
    }
}

typedef void (*Pattern)(float, RGB *);
Pattern patterns[] = {flow, tunnel, spirograph, aurora, metaballs, warp, rotozoom};
const int NPAT = sizeof(patterns) / sizeof(patterns[0]);

static void onEnter(int p) {
  if (patterns[p] == spirograph) memset(spiro, 0, sizeof(spiro));
  if (patterns[p] == warp) {
    memset(warpbuf, 0, sizeof(warpbuf));
    for (auto &s : stars) respawn(s, false);
  }
}

static RGB bufA[N], bufB[N];

void setup() {
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show();
  randomSeed(esp_random());
  onEnter(0);
}

void loop() {
  static int cur = 0, nxt = 1;
  static uint32_t start = millis();
  static bool fading = false;

  uint32_t now = millis();
  uint32_t el = now - start;
  float t = now / 1000.0f;

  patterns[cur](t, bufA);
  if (el >= PATTERN_MS - FADE_MS) {
    if (!fading) { fading = true; onEnter(nxt); }
    patterns[nxt](t, bufB);
    float m = smooth(0, 1, (float)(el - (PATTERN_MS - FADE_MS)) / FADE_MS);
    for (int i = 0; i < N; i++) {
      bufA[i].r = bufA[i].r + (bufB[i].r - bufA[i].r) * m;
      bufA[i].g = bufA[i].g + (bufB[i].g - bufA[i].g) * m;
      bufA[i].b = bufA[i].b + (bufB[i].b - bufA[i].b) * m;
    }
    if (el >= PATTERN_MS) {
      cur = nxt;
      nxt = (nxt + 1) % NPAT;
      start = now;
      fading = false;
    }
  }

  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const RGB &c = bufA[y * W + x];
      strip.setPixelColor(XY(x, y), Adafruit_NeoPixel::gamma8(c.r),
                          Adafruit_NeoPixel::gamma8(c.g), Adafruit_NeoPixel::gamma8(c.b));
    }
  strip.show();
}
