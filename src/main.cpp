#include <Adafruit_NeoPixel.h>
#include <math.h>

#define LED_PIN 5
#define W 16
#define H 16
#define N (W * H)
#define BRIGHTNESS 80   // 0-255

Adafruit_NeoPixel strip(N, LED_PIN, NEO_GRB + NEO_KHZ800);

// Hand-drawn 16x16 version of the logo. '#' = logo blue, '*' = twinkling star.
static const char *ART[H] = {
    "..........*..*..",
    ".......*........",
    "..............##",
    "..####....#####.",
    "##....##########",
    ".###......#..##.",
    "...#.....#......",
    "....##..#.......",
    "......#...#.....",
    ".......#..#.....",
    "........#..#....",
    ".........#..#...",
    "...........##...",
    "............##..",
    "................",
    "................",
};

// Serpentine layout: even rows left->right, odd rows right->left.
static inline uint16_t XY(int x, int y) {
  return y * W + ((y & 1) ? (W - 1 - x) : x);
}

void setup() {
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
}

void loop() {
  float t = millis() / 1000.0f;
  int s = 0;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      char c = ART[y][x];
      uint8_t r = 0, g = 0, b = 0;
      if (c == '#') {
        r = 0; g = 80; b = 255;
      } else if (c == '*') {
        float k = 0.35f + 0.65f * (0.5f + 0.5f * sinf(t * 2.5f + x * 1.7f));
        r = 200 * k; g = 220 * k; b = 255 * k;
      }
      strip.setPixelColor(XY(x, y), r, g, b);
    }
  strip.show();
  delay(30);
}
