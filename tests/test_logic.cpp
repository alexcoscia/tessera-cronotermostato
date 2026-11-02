// Tests of components/cronotermostato/cronotermostato_logic.h. Build and run: tests/run.sh
#include "../components/cronotermostato/cronotermostato_logic.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace esphome::cronotermostato;

static int g_checks = 0, g_failed = 0, g_tests = 0;
static const char *g_test = "";
#define CHECK(cond)                                                                      \
  do {                                                                                   \
    ++g_checks;                                                                          \
    if (!(cond)) {                                                                       \
      ++g_failed;                                                                        \
      std::printf("FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond);          \
    }                                                                                    \
  } while (0)
#define CHECK_STR(a, b)                                                                  \
  do {                                                                                   \
    ++g_checks;                                                                          \
    const std::string sa_ = (a), sb_ = (b);                                              \
    if (sa_ != sb_) {                                                                    \
      ++g_failed;                                                                        \
      std::printf("FAIL [%s] %s:%d: \"%s\" != \"%s\"\n", g_test, __FILE__, __LINE__, sa_.c_str(), sb_.c_str()); \
    }                                                                                    \
  } while (0)
#define TEST(name)                      \
  static void name();                   \
  static struct Reg_##name {            \
    Reg_##name() { tests().push_back({#name, name}); } \
  } reg_##name;                         \
  static void name()

struct Entry {
  const char *name;
  void (*fn)();
};
static std::vector<Entry> &tests() {
  static std::vector<Entry> t;
  return t;
}

static const double NaN = std::numeric_limits<double>::quiet_NaN();
static const double INF = std::numeric_limits<double>::infinity();

using V = std::vector<double>;

// ---------------------------------------------------------------- 1. bands and bar

static std::vector<Band> bands_of(const V &s, const V &e, const V &m, const V &t, const V &a, int focus = -1) {
  DayLists l;
  l.start = s;
  l.end = e;
  l.mode = m;
  l.temp = t;
  l.autom = a;
  return normalize_bands(l, focus);
}

TEST(bands_basic) {
  auto b = bands_of({0, 390, 1380}, {390, 1380, 1440}, {0, 1, 0}, {-100, 21.5, -100}, {1, 0, 1});
  CHECK(b.size() == 3);
  CHECK(b[0].start == 0 && b[0].end == 390 && b[0].mode == Mode::OFF && !b[0].has_temp && b[0].autom);
  CHECK(b[1].start == 390 && b[1].end == 1380 && b[1].mode == Mode::HEAT && b[1].has_temp && b[1].temp == 21.5 && !b[1].autom);
  CHECK(b[2].start == 1380 && b[2].end == 1440 && b[2].autom);
}

TEST(bands_empty_and_mismatched_lengths) {
  CHECK(bands_of({}, {}, {}, {}, {}).empty());
  // the shortest list wins
  auto b = bands_of({0, 600, 900}, {600, 900, 1440}, {1, 1}, {20, 21, 22}, {0, 0, 0});
  CHECK(b.size() == 2);
  CHECK(bands_of({0}, {}, {1}, {20}, {0}).empty());
  CHECK(bands_of({0}, {100}, {1}, {20}, {}).empty());
}

TEST(bands_temp_sentinel) {
  auto b = bands_of({0, 100, 200, 300}, {100, 200, 300, 400}, {1, 1, 1, 1}, {-100, -100.5, -99.9, NaN}, {0, 0, 0, 0});
  CHECK(b.size() == 4);
  CHECK(!b[0].has_temp);          // exactly -100: absent
  CHECK(!b[1].has_temp);          // below: absent
  CHECK(b[2].has_temp && b[2].temp == -99.9);   // above: a (silly) value, kept
  CHECK(!b[3].has_temp);          // NaN: absent
  CHECK(b[3].mode == Mode::HEAT);
}

TEST(bands_hostile_values_are_clamped_or_dropped) {
  // out of 0-1440 is clamped; start >= end after clamping is dropped
  auto b = bands_of({-50, 1500, 700, 800, 900}, {100, 1600, 600, 800, 5000}, {1, 1, 1, 1, 2}, {20, 20, 20, 20, 25}, {0, 0, 0, 0, 0});
  CHECK(b.size() == 2);
  CHECK(b[0].start == 0 && b[0].end == 100);
  CHECK(b[1].start == 900 && b[1].end == 1440 && b[1].mode == Mode::COOL);
  CHECK(bands_of({NaN, 0, 0}, {100, NaN, INF}, {1, 1, 1}, {20, 20, 20}, {0, 0, 0}).empty());
  CHECK(bands_of({-INF}, {INF}, {1}, {20}, {0}).empty());   // not finite: dropped, not trusted
  CHECK(bands_of({1e300}, {2e300}, {1}, {20}, {0}).empty());
  CHECK(bands_of({0}, {1e300}, {1}, {20}, {0}).size() == 1);   // finite: clamped to 24:00
}

TEST(bands_sorted_and_overlaps_trimmed) {
  auto b = bands_of({600, 0, 300}, {900, 400, 700}, {1, 2, 0}, {20, 25, -100}, {0, 0, 0});
  // sorted: 0-400 cool, 300-700 off (trimmed to 400-700), 600-900 heat (trimmed to 700-900)
  CHECK(b.size() == 3);
  CHECK(b[0].start == 0 && b[0].end == 400 && b[0].mode == Mode::COOL);
  CHECK(b[1].start == 400 && b[1].end == 700 && b[1].mode == Mode::OFF);
  CHECK(b[2].start == 700 && b[2].end == 900 && b[2].mode == Mode::HEAT);
  // fully covered band disappears
  auto c = bands_of({0, 100}, {500, 200}, {1, 2}, {20, 25}, {0, 0});
  CHECK(c.size() == 1 && c[0].end == 500);
  // same start: stable
  auto d = bands_of({0, 0}, {100, 200}, {1, 2}, {20, 25}, {0, 0});
  CHECK(d.size() == 2 && d[0].end == 100 && d[1].start == 100 && d[1].end == 200);
}

TEST(bands_mode_codes) {
  CHECK(mode_from_code(0) == Mode::OFF);
  CHECK(mode_from_code(1) == Mode::HEAT);
  CHECK(mode_from_code(2) == Mode::COOL);
  CHECK(mode_from_code(3) == Mode::OFF);
  CHECK(mode_from_code(-1) == Mode::OFF);
  CHECK(mode_from_code(1.5) == Mode::OFF);
  CHECK(mode_from_code(NaN) == Mode::OFF);
  CHECK(mode_from_code(INF) == Mode::OFF);
  CHECK(mode_from_code(1.0) == Mode::HEAT);
}

TEST(bands_max_16_window) {
  V s, e, m, t, a;
  for (int i = 0; i < 40; ++i) {
    s.push_back(i * 30);
    e.push_back(i * 30 + 30);
    m.push_back(1);
    t.push_back(20);
    a.push_back(0);
  }
  auto first = bands_of(s, e, m, t, a);
  CHECK(first.size() == MAX_BANDS && MAX_BANDS == 16);
  CHECK(first[0].start == 0);
  // with a focus the window contains the band of that minute
  auto w = bands_of(s, e, m, t, a, 30 * 25 + 5);
  CHECK(w.size() == 16);
  bool found = false;
  for (auto &x : w) found = found || (x.start <= 755 && 755 < x.end);
  CHECK(found);
  auto end = bands_of(s, e, m, t, a, 1439);
  CHECK(end.size() == 16 && end.back().end == 1200);   // the list covers only 40*30 = 1200 minutes: focus has no band, first 16
  auto late = bands_of(s, e, m, t, a, 1190);
  CHECK(late.size() == 16 && late.back().end == 1200);
  auto early = bands_of(s, e, m, t, a, 0);
  CHECK(early.size() == 16 && early[0].start == 0);
  // absurdly many entries do not blow up, and only the first MAX_LIST_ITEMS are ever looked at
  V big(100000, 0.0);
  CHECK(bands_of(big, big, big, big, big).empty());
  V zero(1000, 0.0), st(1000, 0.0), en(1000, 0.0), one(1000, 1.0);
  st[500] = 100;
  en[500] = 200;
  CHECK(bands_of(st, en, one, one, zero).empty());
  st[5] = 100;
  en[5] = 200;
  CHECK(bands_of(st, en, one, one, zero).size() == 1);
}

static void check_bar_invariants(const std::vector<Segment> &s, int width) {
  int prev = 0;
  for (auto &g : s) {
    CHECK(g.x0 >= prev);          // no overlap
    CHECK(g.x1 >= g.x0 + 1);      // at least one pixel
    CHECK(g.x1 <= width);
    prev = g.x1;
  }
  CHECK(s.size() <= MAX_BANDS);
}

TEST(bar_basic_full_day) {
  auto b = bands_of({0, 390, 510, 1020, 1380}, {390, 510, 1020, 1380, 1440}, {1, 1, 1, 1, 0}, {17, 21, 18, 21.5, -100}, {0, 0, 0, 0, 0});
  auto s = bar_segments(b, 288);   // 0.2 px per minute
  CHECK(s.size() == 5);
  CHECK(s[0].x0 == 0);
  CHECK(s[4].x1 == 288);          // last one runs to the end
  for (size_t i = 1; i < s.size(); ++i) CHECK(s[i].x0 == s[i - 1].x1);   // no holes between contiguous bands
  CHECK(s[1].x0 == 78 && s[1].x1 == 102);
  CHECK(s[1].mode == Mode::HEAT && s[1].has_temp && s[1].temp == 21);
  CHECK(s[4].mode == Mode::OFF && !s[4].has_temp);
  check_bar_invariants(s, 288);
}

TEST(bar_gap_stays_a_gap) {
  auto b = bands_of({0, 720}, {360, 1440}, {1, 1}, {20, 20}, {0, 0});
  auto s = bar_segments(b, 1440);
  CHECK(s.size() == 2 && s[0].x1 == 360 && s[1].x0 == 720 && s[1].x1 == 1440);
}

TEST(bar_min_one_pixel_and_no_overlap) {
  // 1-minute bands on a 100 px bar; neighbours must not overlap
  auto b = bands_of({0, 500, 501, 502, 1439}, {500, 501, 502, 1439, 1440}, {1, 2, 0, 1, 2}, {20, 25, -100, 20, 25}, {0, 0, 1, 0, 0});
  auto s = bar_segments(b, 100);
  CHECK(s.size() == 5);
  check_bar_invariants(s, 100);
  CHECK(s.back().x1 == 100 && s.back().x0 == 99);   // a last minute still gets its pixel
}

TEST(bar_tiny_and_bad_widths) {
  auto b = bands_of({0, 100, 200}, {100, 200, 300}, {1, 2, 0}, {20, 25, -100}, {0, 0, 0});
  CHECK(bar_segments(b, 0).empty());
  CHECK(bar_segments(b, -5).empty());
  auto one = bar_segments(b, 1);
  CHECK(one.size() == 1 && one[0].x0 == 0 && one[0].x1 == 1);   // no room for the rest, never overlaps
  auto two = bar_segments(b, 2);
  check_bar_invariants(two, 2);
  CHECK(two.size() == 2);
  CHECK(bar_segments({}, 300).empty());
  auto huge = bar_segments(b, 2000000000);   // no integer overflow
  check_bar_invariants(huge, 2000000000);
  CHECK(huge.size() == 3);
}

TEST(bar_many_segments_with_narrow_bar) {
  V s, e, m, t, a;
  for (int i = 0; i < 16; ++i) {
    s.push_back(i * 90);
    e.push_back(i * 90 + 90);
    m.push_back(i % 3);
    t.push_back(20);
    a.push_back(0);
  }
  auto b = bands_of(s, e, m, t, a);
  for (int w : {16, 17, 20, 50, 302, 1024}) {
    auto g = bar_segments(b, w);
    CHECK(g.size() == 16);
    check_bar_invariants(g, w);
    CHECK(g.back().x1 == w);
  }
  auto g = bar_segments(b, 8);   // fewer pixels than segments: the ones that do not fit are left out
  CHECK(g.size() == 8);
  check_bar_invariants(g, 8);
}

// Cases computed by the editor's own stripSegments (custom_components/.../lib/day-editing.js) on the valid days of
// tests/frontend/cases.json: {startMin, endMin, left %, width %}.
struct JsSeg {
  int start, end;
  double left, width;
};
struct JsCase {
  const char *name;
  std::vector<JsSeg> segs;
};
static const std::vector<JsCase> &js_cases() {
  static const std::vector<JsCase> c = {
#include "parity.inc"
  };
  return c;
}

TEST(bar_parity_with_editor_strip_segments) {
  CHECK(js_cases().size() >= 10);
  for (int width : {1440, 2880, 1000, 302, 480}) {
    for (auto &c : js_cases()) {
      V s, e, m, t, a;
      for (auto &g : c.segs) {
        s.push_back(g.start);
        e.push_back(g.end);
        m.push_back(1);
        t.push_back(20);
        a.push_back(0);
      }
      auto segs = bar_segments(bands_of(s, e, m, t, a), width);
      CHECK(segs.size() == c.segs.size());
      if (segs.size() != c.segs.size()) continue;
      // the editor lists in the order given; we sort by start: compare by start minute
      for (auto &g : c.segs) {
        bool found = false;
        for (auto &x : segs) {
          if (x.start != g.start) continue;
          found = true;
          const double jl = g.left * width / 100.0, jr = (g.left + g.width) * width / 100.0;
          CHECK(std::fabs(x.x0 - jl) <= 1.0 + 1e-9);   // same position within a pixel
          CHECK(std::fabs(x.x1 - jr) <= 1.0 + 1e-9);
          if (width == 1440 || width == 2880) {         // exact: whole pixels per minute
            CHECK(x.x0 == std::lround(jl) && x.x1 == std::lround(jr));
          }
        }
        CHECK(found);
      }
    }
  }
}

TEST(boundary_rounds_to_nearest_pixel) {
  CHECK(boundary_x(0, 100) == 0);
  CHECK(boundary_x(500, 100) == 35);    // 34.72
  CHECK(boundary_x(490, 100) == 34);    // 34.03
  CHECK(boundary_x(1440, 100) == 100);
  CHECK(boundary_x(720, 1) == 1);       // 0.5 rounds up
  CHECK(boundary_x(719, 1) == 0);
}

TEST(now_marker) {
  CHECK(now_x(0, 288) == 0);
  CHECK(now_x(720, 288) == 144);
  CHECK(now_x(1439, 288) == 287);
  CHECK(now_x(1440, 288) == 287);     // clamp: never outside the bar
  CHECK(now_x(5000, 288) == 287);
  CHECK(now_x(-10, 288) == 0);
  CHECK(now_x(100, 0) == 0);
  CHECK(now_x(100, -3) == 0);
  CHECK(now_x(1439, 1) == 0);
  CHECK(now_x(1439, 2000000000) > 1990000000 && now_x(1439, 2000000000) < 2000000000);
  CHECK(now_x(61, 1440) == 61);
  // the marker lies over the band of that minute
  auto b = bands_of({0, 720}, {720, 1440}, {1, 2}, {20, 25}, {0, 0});
  auto s = bar_segments(b, 300);
  const int x = now_x(721, 300);
  CHECK(x >= s[1].x0 && x < s[1].x1);
  const int y = now_x(719, 300);
  CHECK(y >= s[0].x0 && y < s[0].x1);
}

// ---------------------------------------------------------------- 2. tones

TEST(tones) {
  CHECK(tone_of(Mode::HEAT) == BarTone::AMBER);
  CHECK(tone_of(Mode::COOL) == BarTone::BLUE);
  CHECK(tone_of(Mode::OFF) == BarTone::GREY);
  CHECK(tone_of(mode_from_code(7)) == BarTone::GREY);
}

// ---------------------------------------------------------------- 3. text

TEST(clock_text) {
  CHECK_STR(format_clock(0), "00:00");
  CHECK_STR(format_clock(5), "00:05");
  CHECK_STR(format_clock(390), "06:30");
  CHECK_STR(format_clock(1439), "23:59");
  CHECK_STR(format_clock(1440), "24:00");     // end of a band that runs to midnight
  CHECK_STR(format_clock(-1), "00:00");
  CHECK_STR(format_clock(99999), "24:00");
  CHECK_STR(format_clock(NaN), "--:--");
  CHECK_STR(format_clock(INF), "--:--");
  CHECK_STR(format_clock(61.4), "01:01");
}

TEST(clock_text_from_wall_clock_fields) {
  CHECK_STR(format_hm(7, 5), "07:05");
  CHECK_STR(format_hm(23, 59), "23:59");
  CHECK_STR(format_hm(24, 0), "24:00");
  CHECK_STR(format_hm(-1, 5), "--:--");
  CHECK_STR(format_hm(25, 0), "--:--");
  CHECK_STR(format_hm(3, 60), "--:--");
}

TEST(decimals_of_step) {
  CHECK(decimals_of_step(1) == 0);
  CHECK(decimals_of_step(2) == 0);
  CHECK(decimals_of_step(0.5) == 1);
  CHECK(decimals_of_step(0.1) == 1);
  CHECK(decimals_of_step(0.25) == 2);
  CHECK(decimals_of_step(0.05) == 2);
  CHECK(decimals_of_step(0) == 1);          // unknown: one decimal
  CHECK(decimals_of_step(-1) == 1);
  CHECK(decimals_of_step(NaN) == 1);
  CHECK(decimals_of_step(INF) == 1);
  CHECK(decimals_of_step(0.3333) == 1);     // not a clean decimal step: fallback
  CHECK(decimals_of_step(0.001) == 1);
  CHECK(decimals_of_step(0.51) == 2);       // 5.1 is not an integer: not one decimal
  CHECK(decimals_of_step(0.01) == 2);
  CHECK(decimals_of_step(0.1 + 0.2) == 1);  // 0.30000000000000004
}

TEST(format_temperature) {
  CHECK_STR(format_temp(21.5, 0.5), "21.5");
  CHECK_STR(format_temp(21, 0.5), "21.0");
  CHECK_STR(format_temp(21, 1), "21");
  CHECK_STR(format_temp(21.4, 1), "21");
  CHECK_STR(format_temp(21.6, 1), "22");
  CHECK_STR(format_temp(21.25, 0.25), "21.25");
  CHECK_STR(format_temp(21.5, 0.5, ','), "21,5");
  CHECK_STR(format_temp(21.5, 0), "21.5");        // unknown step: one decimal
  CHECK_STR(format_temp(21.5, NaN), "21.5");
  CHECK_STR(format_temp(0.1 + 0.2, 0.1), "0.3");
  CHECK_STR(format_temp(19.95, 0.1), "20.0");
  CHECK_STR(format_temp(5, 0.5), "5.0");
  CHECK_STR(format_temp(-3, 1), "-3");
  CHECK_STR(format_temp(-12.4, 1), "-12");
  CHECK_STR(format_temp(0.145, 0.01), "0.15");    // 14.499999999999998 in floating point
  CHECK_STR(format_temp(-0.145, 0.01), "-0.15");
  CHECK_STR(format_temp(-3.5, 0.5), "-3.5");
  CHECK_STR(format_temp(-0.04, 0.1), "0.0");      // never "-0.0"
  CHECK_STR(format_temp(21.5, 0.5, '.', true), "21.5\xC2\xB0");
  CHECK_STR(format_temp(21, 1, '.', true), "21\xC2\xB0");
  CHECK_STR(format_temp(NaN, 0.5), "-");
  CHECK_STR(format_temp(INF, 0.5), "-");
  CHECK_STR(format_temp(1e300, 0.5), "-");
  CHECK_STR(format_temp(-1e300, 0.5), "-");
  CHECK_STR(format_temp(1000, 0.5), "-");
  CHECK_STR(format_temp(NaN, 0.5, '.', true), "-");
}

TEST(format_temperature_only_font_digits_chars) {
  const char *ok = "0123456789-.,\xC2\xB0%";
  for (double v : {21.5, -3.5, 0.0, 999.0, NaN, INF, -INF, 1e300}) {
    for (double st : {0.5, 1.0, 0.1, 0.0, NaN}) {
      for (char sep : {'.', ','}) {
        std::string s = format_temp(v, st, sep, true);
        for (size_t i = 0; i < s.size(); ++i) {
          CHECK(std::strchr(ok, s[i]) != nullptr);
        }
        CHECK(!s.empty() && s.size() <= 12);
      }
    }
  }
}

// ---------------------------------------------------------------- 4. setpoint

TEST(range_and_snap) {
  SetpointRange r = make_range(7, 35, 0.5);
  CHECK(r.lo == 700 && r.hi == 3500 && r.step == 50);
  CHECK(snap(2150, r) == 2150);
  CHECK(snap(2120, r) == 2100);     // 21.2 -> 21.0
  CHECK(snap(2130, r) == 2150);     // 21.3 -> 21.5
  CHECK(snap(2125, r) == 2150);     // halfway: up
  CHECK(snap(0, r) == 700);
  CHECK(snap(99999, r) == 3500);
  CHECK(snap(-99999, r) == 700);
  // a maximum that is not on the grid: the highest step below it
  SetpointRange odd = make_range(7, 35.3, 0.5);
  CHECK(snap(9999, odd) == 3500 && snap(3530, odd) == 3500);
  // fallback for bad data
  CHECK(make_range(7, 35, 0).step == 50);
  CHECK(make_range(7, 35, -1).step == 50);
  CHECK(make_range(7, 35, NaN).step == 50);
  CHECK(make_range(7, 35, INF).step == 50);
  CHECK(make_range(7, 35, 0.001).step == 50);
  CHECK(make_range(30, 10, 0.5).lo == 500 && make_range(30, 10, 0.5).hi == 3500);   // integration's 5-35
  CHECK(make_range(NaN, 35, 0.5).lo == 500);
  CHECK(make_range(7, NaN, 0.5).hi == 3500);
  CHECK(make_range(7, 7, 0.5).hi > make_range(7, 7, 0.5).lo);
  // step bigger than the span still leaves one step
  SetpointRange w = make_range(10, 12, 5);
  CHECK(w.hi - w.lo >= w.step);
  // whole degrees
  SetpointRange whole = make_range(10, 30, 1);
  CHECK(whole.step == 100 && snap(2150, whole) == 2200 && snap(2149, whole) == 2100);
  // a range that starts off-grid snaps relative to its minimum
  SetpointRange off = make_range(7.5, 30, 1);
  CHECK(snap(1000, off) == 950 || snap(1000, off) == 1050);
  CHECK((snap(1234, off) - off.lo) % off.step == 0);
}

TEST(to_centi_avoids_float_errors) {
  CHECK(to_centi(0.1 + 0.2) == 30);
  CHECK(to_centi(21.5) == 2150);
  CHECK(to_centi(19.95) == 1995);
  CHECK(to_centi(0.285) == 29);   // 28.499999999999996 in floating point: half rounds away from zero
  CHECK(to_centi(-0.285) == -29);
  CHECK(to_centi(-0.5) == -50);
  CHECK(to_centi(NaN) == NO_VALUE);
  CHECK(to_centi(INF) == NO_VALUE);
  CHECK(to_centi(1e300) == NO_VALUE);
  CHECK(to_degrees(2150) == 21.5);
  CHECK(to_degrees(1995) == 19.95);
}

static Setpoint make_sp() {
  Setpoint sp;
  sp.set_range(make_range(7, 35, 0.5));
  return sp;
}

TEST(setpoint_tap_moves_one_step_locally) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 1000);
  CHECK(sp.shown() == 2100 && !sp.pending());
  CHECK(sp.on_tap(+1, 1100));
  CHECK(sp.shown() == 2150 && sp.pending());
  CHECK(sp.on_tap(+1, 1200));
  CHECK(sp.on_tap(+1, 1250));
  CHECK(sp.shown() == 2250);
  CHECK(sp.on_tap(-1, 1300));
  CHECK(sp.shown() == 2200);
  CHECK(sp.reported() == 2100);   // the real one is untouched
}

TEST(setpoint_debounce_600ms) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  CHECK(!sp.due(0));
  sp.on_tap(+1, 1000);
  CHECK(!sp.due(1000));
  CHECK(!sp.due(1599));
  CHECK(sp.due(1600));
  // another tap restarts the wait
  sp.on_tap(+1, 1500);
  CHECK(!sp.due(1600));
  CHECK(!sp.due(2099));
  CHECK(sp.due(2100));
  int v = 0;
  CHECK(!sp.take(2099, v));         // too early: nothing is taken
  CHECK(sp.take(2100, v));
  CHECK(v == 2200);
  CHECK(!sp.due(2100));             // sent once only
  CHECK(!sp.take(2200, v));
}

TEST(setpoint_confirm_by_home_assistant) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  sp.on_tap(+1, 1000);
  int v = 0;
  CHECK(sp.take(1600, v) && v == 2150);
  CHECK(sp.pending());              // waiting for HA
  sp.on_state(21.5, 2000);          // HA agrees
  CHECK(!sp.pending());
  CHECK(sp.shown() == 2150);
  CHECK(!sp.tick(7000));            // nothing to revert
  CHECK(sp.shown() == 2150);
}

TEST(setpoint_revert_after_5s_without_confirmation) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  sp.on_tap(+1, 1000);
  int v = 0;
  CHECK(sp.take(1600, v));
  CHECK(!sp.tick(6599));            // 4999 ms after the send
  CHECK(sp.shown() == 2150);
  CHECK(sp.tick(6600));             // 5000 ms
  CHECK(sp.shown() == 2100);
  CHECK(!sp.pending());
  CHECK(!sp.tick(7000));
}

TEST(setpoint_stale_report_does_not_override_taps) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  sp.on_tap(+1, 1000);
  int v = 0;
  CHECK(sp.take(1600, v) && v == 2150);
  sp.on_tap(+1, 1700);              // user keeps tapping while the first command is in flight
  sp.on_state(21.5, 1800);          // confirmation of the first value
  CHECK(sp.shown() == 2200);        // the local value stays
  CHECK(sp.pending());
  CHECK(sp.take(2300, v) && v == 2200);
  sp.on_state(22.0, 2500);
  CHECK(!sp.pending() && sp.shown() == 2200);
}

TEST(setpoint_reports_while_not_touched_follow_ha) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  sp.on_state(18.0, 10);
  CHECK(sp.shown() == 1800);
  sp.on_state(NaN, 20);             // no target any more
  CHECK(sp.shown() == NO_VALUE);
  CHECK(!sp.on_tap(+1, 30));        // nothing to move
  CHECK(!sp.pending());
}

TEST(setpoint_limits_and_noop_taps) {
  Setpoint sp = make_sp();
  sp.on_state(35.0, 0);
  CHECK(!sp.on_tap(+1, 10));        // at the maximum: no change, no pending, nothing to send
  CHECK(!sp.pending() && !sp.due(1000));
  CHECK(sp.on_tap(-1, 20));
  CHECK(sp.shown() == 3450);
  sp.on_state(7.0, 100000);
  // still pending from the tap above (HA did not confirm): shown stays
  CHECK(sp.shown() == 3450);
  Setpoint lo = make_sp();
  lo.on_state(7.0, 0);
  CHECK(!lo.on_tap(-1, 1));
  CHECK(lo.shown() == 700);
  CHECK(lo.on_tap(+10, 2));
  CHECK(lo.shown() == 1200);
  CHECK(lo.on_tap(-1000, 3));       // huge delta: clamped
  CHECK(lo.shown() == 700);
  CHECK(lo.on_tap(+100000, 4));
  CHECK(lo.shown() == 3500);
  CHECK(!lo.on_tap(0, 5));
}

TEST(setpoint_sending_the_reported_value_is_not_sent) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  sp.on_tap(+1, 100);
  sp.on_tap(-1, 200);               // back to where HA is
  int v = 0;
  CHECK(!sp.take(900, v));
  CHECK(!sp.pending());
  CHECK(sp.shown() == 2100);
}

TEST(setpoint_off_grid_report_snaps_on_tap) {
  Setpoint sp = make_sp();
  sp.on_state(21.3, 0);
  CHECK(sp.shown() == 2130);        // shows what HA says
  CHECK(sp.on_tap(+1, 10));
  CHECK(sp.shown() == 2200);        // 21.3 -> 21.5 on the grid, plus one step
}

TEST(setpoint_millis_wraparound) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  const uint32_t t0 = 0xFFFFFF00u;
  sp.on_tap(+1, t0);
  CHECK(!sp.due(t0 + 599));
  CHECK(sp.due(t0 + 600));          // wraps past zero
  int v = 0;
  CHECK(sp.take(t0 + 600, v));
  CHECK(!sp.tick(t0 + 600 + 4999));
  CHECK(sp.tick(t0 + 600 + 5000));
}

TEST(setpoint_new_tap_cancels_the_old_confirmation_wait) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  sp.on_tap(+1, 1000);
  int v = 0;
  CHECK(sp.take(1600, v));          // sent at 1600; HA is silent
  sp.on_tap(+1, 6000);              // the user taps again after 4.4 s
  CHECK(!sp.tick(6700));            // 5.1 s after the first send: must NOT snap back under the user's finger
  CHECK(sp.shown() == 2200);
  CHECK(sp.take(6600, v) && v == 2200);
  CHECK(!sp.tick(6600 + 4999));
  CHECK(sp.tick(6600 + 5000));
  CHECK(sp.shown() == 2100);
}

TEST(setpoint_range_change_clamps_the_value_to_send) {
  Setpoint sp = make_sp();
  sp.on_state(30.0, 0);
  sp.on_tap(+1, 1);                 // 30.5, not sent yet
  sp.set_range(make_range(10, 20, 0.5));
  int v = 0;
  CHECK(sp.shown() == 2000);
  CHECK(sp.take(700, v) && v == 2000);
}

TEST(setpoint_set_range_and_reset) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  sp.on_tap(+1, 10);
  sp.reset();
  CHECK(!sp.pending() && sp.shown() == NO_VALUE && sp.reported() == NO_VALUE && !sp.due(100000));
  sp.on_state(20.0, 5);
  CHECK(sp.shown() == 2000);
  // a new range re-clamps a local value
  sp.on_tap(+1, 6);
  sp.set_range(make_range(10, 20, 0.5));
  CHECK(sp.shown() <= 2000);
  Setpoint ns;                      // default-constructed: usable
  CHECK(ns.shown() == NO_VALUE);
  CHECK(!ns.on_tap(1, 0));
}

TEST(setpoint_float_accumulation) {
  Setpoint sp;
  sp.set_range(make_range(5, 35, 0.1));
  sp.on_state(0.1 + 0.2 + 20.0, 0);     // 20.3 with float noise
  for (int i = 0; i < 7; ++i) sp.on_tap(+1, 10 + i);
  CHECK(sp.shown() == 2100);            // 20.3 + 7 * 0.1, exactly 21.0
  int v = 0;
  CHECK(sp.take(1000, v));
  CHECK(to_degrees(v) == 21.0);
}

// ---------------------------------------------------------------- 5. tile state

static TileInput base_input() {
  TileInput in;
  in.climate_state = "heat";
  in.has_room = true;
  in.room_state = "Settimana";
  in.status = "ok";
  return in;
}

TEST(pill_unavailable_climate) {
  for (const char *s : {"unavailable", "unknown", ""}) {
    TileInput in = base_input();
    in.climate_state = s;
    TileView v = tile_view(in);
    CHECK(v.pill == Pill::UNAVAILABLE);
    CHECK(!v.controls && !v.bar_visible && !v.show_until && !v.has_next);
  }
}

TEST(pill_choose_room) {
  TileInput in = base_input();
  in.has_room = false;
  TileView v = tile_view(in);
  CHECK(v.pill == Pill::CHOOSE_ROOM);
  CHECK(!v.controls && !v.bar_visible);
  // the climate being down wins over the missing option
  in.climate_state = "unavailable";
  CHECK(tile_view(in).pill == Pill::UNAVAILABLE);
}

TEST(pill_room_unavailable) {
  TileInput in = base_input();
  in.room_state = "unavailable";
  CHECK(tile_view(in).pill == Pill::UNAVAILABLE);
  in = base_input();
  in.status = "unavailable";
  CHECK(tile_view(in).pill == Pill::UNAVAILABLE);
  in = base_input();
  in.status = "missing";
  CHECK(tile_view(in).pill == Pill::UNAVAILABLE);
}

TEST(pill_paused) {
  TileInput in = base_input();
  in.status = "paused";
  in.manual = true;     // paused wins
  in.slot_end = 1000;
  TileView v = tile_view(in);
  CHECK(v.pill == Pill::PAUSED);
  CHECK(!v.show_until && !v.has_next && !v.bar_visible && !v.controls);
}

TEST(pill_manual_with_until) {
  TileInput in = base_input();
  in.manual = true;
  in.override_until = 1762106400;
  in.slot_end = 1762110000;
  TileView v = tile_view(in);
  CHECK(v.pill == Pill::MANUAL);
  CHECK(v.show_until && v.until == 1762106400);   // the override's end, not the slot's
  CHECK(v.can_resume && v.controls && v.bar_visible);
  in.override_until = 0;
  v = tile_view(in);
  CHECK(v.pill == Pill::MANUAL && !v.show_until);
  in.override_until = -5;
  CHECK(!tile_view(in).show_until);
}

TEST(pill_auto_with_program_and_next) {
  TileInput in = base_input();
  in.slot_end = 1762106400;
  in.next_mode = 2;
  in.next_temp = 24.5;
  in.next_start = 1762106400;
  TileView v = tile_view(in);
  CHECK(v.pill == Pill::AUTO);
  CHECK(v.program == "Settimana");
  CHECK(v.show_until && v.until == 1762106400);
  CHECK(v.has_next && v.next_mode == Mode::COOL && v.next_has_temp && v.next_temp == 24.5 && v.next_start == 1762106400);
  CHECK(!v.can_resume && v.controls && v.bar_visible);
}

TEST(pill_auto_next_without_temp_or_time) {
  TileInput in = base_input();
  in.next_mode = 0;
  in.next_temp = NaN;
  in.next_start = 1762106400;
  TileView v = tile_view(in);
  CHECK(v.has_next && v.next_mode == Mode::OFF && !v.next_has_temp);
  in.next_start = 0;     // no next slot
  CHECK(!tile_view(in).has_next);
  in = base_input();
  in.next_mode = -1;     // no code
  in.next_start = 1762106400;
  v = tile_view(in);
  CHECK(!v.has_next);
}

TEST(pill_no_program) {
  for (const char *s : {"unknown", ""}) {
    TileInput in = base_input();
    in.room_state = s;
    TileView v = tile_view(in);
    CHECK(v.pill == Pill::NO_PROGRAM);
    CHECK(v.program.empty() && !v.controls && !v.bar_visible);
  }
  // a manual override with no program is still manual
  TileInput in = base_input();
  in.room_state = "unknown";
  in.manual = true;
  CHECK(tile_view(in).pill == Pill::MANUAL);
}

TEST(pill_problem_statuses) {
  for (const char *s : {"unsupported_mode", "unsupported_preset", "out_of_range", "error", "something_new"}) {
    TileInput in = base_input();
    in.status = s;
    TileView v = tile_view(in);
    CHECK(v.pill == Pill::PROBLEM);
    CHECK(v.program == "Settimana");
  }
  TileInput in = base_input();
  in.status = "";         // missing status behaves like ok
  CHECK(tile_view(in).pill == Pill::AUTO);
}

TEST(pill_program_name_is_cut_to_a_safe_length) {
  TileInput in = base_input();
  in.room_state = std::string(5000, 'x');
  TileView v = tile_view(in);
  CHECK(v.pill == Pill::AUTO && v.program.size() <= MAX_NAME_BYTES);
  // never cut in the middle of a UTF-8 character
  in.room_state = std::string(MAX_NAME_BYTES - 1, 'a') + "\xC3\xA8\xC3\xA8";   // e grave twice
  v = tile_view(in);
  CHECK(v.program.size() <= MAX_NAME_BYTES);
  CHECK(v.program == std::string(MAX_NAME_BYTES - 1, 'a'));
}

// ---------------------------------------------------------------- 6. card rows

TEST(card_rows_current_row) {
  DayLists l;
  l.start = {0, 390, 1380};
  l.end = {390, 1380, 1440};
  l.mode = {0, 1, 0};
  l.temp = {-100, 21.5, -100};
  l.autom = {1, 0, 1};
  auto rows = card_rows(l, 400);
  CHECK(rows.size() == 3);
  CHECK(!rows[0].current && rows[1].current && !rows[2].current);
  CHECK(rows[0].autom && !rows[1].autom && !rows[1].autom);
  CHECK(rows[1].has_temp && rows[1].temp == 21.5 && rows[1].mode == Mode::HEAT);
  CHECK(rows[1].start == 390 && rows[1].end == 1380);
  // boundaries: start inclusive, end exclusive
  rows = card_rows(l, 390);
  CHECK(rows[1].current && !rows[0].current);
  rows = card_rows(l, 389);
  CHECK(rows[0].current && !rows[1].current);
  rows = card_rows(l, 1379);
  CHECK(rows[1].current && !rows[2].current);
  rows = card_rows(l, 1380);
  CHECK(rows[2].current);
  rows = card_rows(l, 1439);
  CHECK(rows[2].current);
  rows = card_rows(l, 5000);       // clamped to 1439
  CHECK(rows[2].current);
  rows = card_rows(l, -5);         // clamped to 0
  CHECK(rows[0].current);
}

TEST(card_rows_no_current_in_a_gap) {
  DayLists l;
  l.start = {0, 720};
  l.end = {360, 1440};
  l.mode = {1, 1};
  l.temp = {20, 20};
  l.autom = {0, 0};
  auto rows = card_rows(l, 500);
  CHECK(rows.size() == 2 && !rows[0].current && !rows[1].current);
  CHECK(card_rows(DayLists{}, 500).empty());
}

TEST(card_rows_max_16_keeps_current_in_view) {
  DayLists l;
  for (int i = 0; i < 40; ++i) {
    l.start.push_back(i * 30);
    l.end.push_back(i * 30 + 30);
    l.mode.push_back(1);
    l.temp.push_back(20);
    l.autom.push_back(0);
  }
  for (int now : {0, 100, 600, 1000, 1199, 1300}) {
    auto rows = card_rows(l, now);
    CHECK(rows.size() == 16);
    int cur = 0;
    for (auto &r : rows) cur += r.current;
    CHECK(cur == (now < 1200 ? 1 : 0));
  }
  CHECK(card_rows(l, 0)[0].start == 0);
}

int main() {
  for (auto &t : tests()) {
    g_test = t.name;
    ++g_tests;
    t.fn();
  }
  std::printf("%d tests, %d checks, %d failed\n", g_tests, g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
