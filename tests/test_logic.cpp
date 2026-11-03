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
  auto b = bands_of({0, 200, 400, 600}, {100, 300, 500, 700}, {1, 1, 1, 1}, {-100, -100.5, -99.9, NaN}, {0, 0, 0, 0});   // gaps: not merged
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
    t.push_back(20 + (i % 2));
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

static const std::vector<JsCase> &overlap_cases() {
  static const std::vector<JsCase> c = {
#include "overlap.inc"
  };
  return c;
}

TEST(bar_parity_with_editor_strip_segments) {
  CHECK(js_cases().size() >= 10);
  for (int width : {1440, 2880, 1000, 302, 480, 3, 7}) {
    for (auto &c : js_cases()) {
      V s, e, m, t, a;
      for (auto &g : c.segs) {
        s.push_back(g.start);
        e.push_back(g.end);
        m.push_back(1);
        t.push_back(20 + (int) t.size());
        a.push_back(0);
      }
      if ((size_t) width < c.segs.size()) continue;
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

static DayLists distinct_day(int n, int minutes_each, bool reversed = false) {
  DayLists l;
  for (int k = 0; k < n; ++k) {
    const int i = reversed ? n - 1 - k : k;
    l.start.push_back(i * minutes_each);
    l.end.push_back(i * minutes_each + minutes_each);
    l.mode.push_back(1);
    l.temp.push_back(20 + (i % 2));
    l.autom.push_back(0);
  }
  return l;
}

TEST(bands_adjacent_equal_are_merged) {
  auto b = bands_of({0, 100, 200, 300, 400, 500}, {100, 200, 300, 400, 500, 600}, {1, 1, 1, 1, 2, 0}, {20, 20, 21, 21, 21, -100}, {0, 0, 0, 0, 0, 0});
  CHECK(b.size() == 4);
  CHECK(b[0].start == 0 && b[0].end == 200);      // heat 20 + heat 20
  CHECK(b[1].start == 200 && b[1].end == 400);    // heat 21 + heat 21
  CHECK(b[2].start == 400 && b[2].mode == Mode::COOL);   // other mode: not merged
  CHECK(b[3].mode == Mode::OFF);
  // not merged: a gap, another temperature, another auto flag, temperature present vs absent
  CHECK(bands_of({0, 150}, {100, 200}, {1, 1}, {20, 20}, {0, 0}).size() == 2);
  CHECK(bands_of({0, 100}, {100, 200}, {1, 1}, {20, 20.5}, {0, 0}).size() == 2);
  CHECK(bands_of({0, 100}, {100, 200}, {0, 0}, {-100, -100}, {0, 1}).size() == 2);
  CHECK(bands_of({0, 100}, {100, 200}, {0, 0}, {-100, -100}, {1, 1}).size() == 1);
  CHECK(bands_of({0, 100}, {100, 200}, {1, 1}, {-100, 20}, {0, 0}).size() == 2);
  // bands made adjacent by trimming an overlap are merged too
  CHECK(bands_of({0, 50}, {100, 200}, {1, 1}, {20, 20}, {0, 0}).size() == 1);
}

TEST(bands_merge_frees_the_limit) {
  DayLists l;
  for (int i = 0; i < 40; ++i) {
    l.start.push_back(i * 30);
    l.end.push_back(i * 30 + 30);
    l.mode.push_back(1);
    l.temp.push_back(20);
    l.autom.push_back(0);
  }
  Day d = normalize_day(l, 600);
  CHECK(d.bands.size() == 1 && d.bands[0].start == 0 && d.bands[0].end == 1200);
  CHECK(!d.truncated_before && !d.truncated_after);
  // 20 raw bands that merge to 10
  DayLists m;
  for (int i = 0; i < 20; ++i) {
    m.start.push_back(i * 60);
    m.end.push_back(i * 60 + 60);
    m.mode.push_back(1);
    m.temp.push_back(20 + ((i / 2) % 2));
    m.autom.push_back(0);
  }
  Day e = normalize_day(m, 0);
  CHECK(e.bands.size() == 10 && !e.truncated_before && !e.truncated_after);
}

TEST(day_window_offset_and_truncation_flags) {
  const DayLists l = distinct_day(40, 30);
  // focus in band 25 (minutes 750-779): the window starts 7 bands before it
  Day d = normalize_day(l, 755);
  CHECK(d.bands.size() == 16);
  CHECK(d.bands[0].start == 18 * 30 && d.bands[15].end == 34 * 30);
  CHECK(d.truncated_before && d.truncated_after);
  // the very first bands: window at 0, only the end is cut
  d = normalize_day(l, 3 * 30 + 1);
  CHECK(d.bands[0].start == 0 && !d.truncated_before && d.truncated_after);
  d = normalize_day(l, 7 * 30);        // band 7: still window 0
  CHECK(d.bands[0].start == 0);
  d = normalize_day(l, 8 * 30);        // band 8: window starts at band 1
  CHECK(d.bands[0].start == 30 && d.truncated_before);
  // the last bands: window ends at the last band, only the start is cut
  d = normalize_day(l, 1190);
  CHECK(d.bands[0].start == 24 * 30 && d.bands.back().end == 1200 && d.truncated_before && !d.truncated_after);
  // end == focus: that band is over, the next one is the current one
  d = normalize_day(l, 26 * 30);       // end of band 25 = start of band 26
  CHECK(d.bands[0].start == 19 * 30);
  d = normalize_day(l, 26 * 30 - 1);   // last minute of band 25
  CHECK(d.bands[0].start == 18 * 30);
  // no focus: the first 16
  d = normalize_day(l, -1);
  CHECK(d.bands[0].start == 0 && !d.truncated_before && d.truncated_after);
  // a day with 16 bands or fewer is never truncated
  d = normalize_day(distinct_day(16, 60), 500);
  CHECK(d.bands.size() == 16 && !d.truncated_before && !d.truncated_after);
  d = normalize_day(distinct_day(17, 60), 100);
  CHECK(d.bands.size() == 16 && !d.truncated_before && d.truncated_after);
  CHECK(normalize_day(DayLists{}, 5).bands.empty());
}

TEST(day_window_with_input_in_reverse_order) {
  Day a = normalize_day(distinct_day(40, 30, false), 755);
  Day b = normalize_day(distinct_day(40, 30, true), 755);
  CHECK(a.bands.size() == b.bands.size() && b.bands.size() == 16);
  for (size_t i = 0; i < a.bands.size(); ++i) CHECK(a.bands[i].start == b.bands[i].start && a.bands[i].end == b.bands[i].end);
  // equal starts among many bands: the first given keeps its minutes, the second is trimmed (stable order)
  DayLists l = distinct_day(20, 60);
  l.start.push_back(0);
  l.end.push_back(500);
  l.mode.push_back(2);
  l.temp.push_back(25);
  l.autom.push_back(0);
  Day c = normalize_day(l, 0);
  CHECK(c.bands[0].start == 0 && c.bands[0].end == 60 && c.bands[0].mode == Mode::HEAT);
  CHECK(c.bands[1].start == 60);
}

TEST(now_marker_is_always_on_a_drawn_band) {
  const DayLists l = distinct_day(40, 30);
  for (int width : {1, 2, 17, 100, 288, 302, 1024}) {
    for (int minute = 0; minute < 1200; ++minute) {
      Day d = normalize_day(l, minute);
      auto segs = bar_segments(d.bands, width);
      const int x = now_marker(segs, minute, width);
      bool on = false;
      for (auto &g : segs)
        if (g.start <= minute && minute < g.end) on = x >= g.x0 && x < g.x1;
      bool drawn = false;
      for (auto &g : segs) drawn = drawn || (g.start <= minute && minute < g.end);
      if (!drawn) continue;                 // narrower than the number of bands: that band is not drawn
      if (!on) std::printf("w %d minute %d x %d\n", width, minute, x);
      CHECK(on);
    }
  }
  // outside any band the marker is where now_x puts it
  auto segs = bar_segments(normalize_bands(l, 5000), 100);
  CHECK(now_marker(segs, 1439, 100) == now_x(1439, 100));
  CHECK(now_marker({}, 700, 100) == now_x(700, 100));
  CHECK(now_marker(segs, 5000, 100) == now_x(5000, 100));
}

TEST(now_x_may_fall_one_pixel_left_of_a_band_without_now_marker) {
  // why now_marker exists: floor(minute) can lie left of round(start)
  auto b = bands_of({0, 400}, {400, 1440}, {1, 2}, {20, 25}, {0, 0});
  auto s = bar_segments(b, 100);
  CHECK(s[1].x0 == 28);                        // 27.78 rounds up
  CHECK(now_x(400, 100) == 27);                // floor: one pixel off
  CHECK(now_marker(s, 400, 100) == 28);
}

TEST(bar_overlap_differs_from_the_editor_on_purpose) {
  // the editor draws overlapping bands over each other; the tile trims the later one so that nothing overlaps
  CHECK(!overlap_cases().empty());
  for (auto &c : overlap_cases()) {
    V s, e, m, t, a;
    for (auto &g : c.segs) {
      s.push_back(g.start);
      e.push_back(g.end);
      m.push_back(1);
      t.push_back(20 + (int) t.size());
      a.push_back(0);
    }
    auto segs = bar_segments(bands_of(s, e, m, t, a), 1440);
    CHECK(segs.size() == c.segs.size());
    int prev = 0;
    for (auto &x : segs) {
      CHECK(x.x0 >= prev);
      prev = x.x1;
    }
    // the first band is drawn as in the editor; the overlapping part of the second is cut
    CHECK(segs[0].x0 == c.segs[0].start && segs[0].x1 == c.segs[0].end);
    CHECK(segs[1].x0 == c.segs[0].end && segs[1].x1 == c.segs[1].end);
    CHECK(segs[1].x0 > c.segs[1].start);
  }
}

TEST(bar_half_pixel_boundaries_round_up) {
  // 720 minutes on a 3 px bar is 1.5 px: the shared boundary goes to 2, for both neighbours
  auto b = bands_of({0, 720}, {720, 1440}, {1, 2}, {20, 25}, {0, 0});
  auto s = bar_segments(b, 3);
  CHECK(s.size() == 2 && s[0].x1 == 2 && s[1].x0 == 2 && s[1].x1 == 3);
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
  CHECK_STR(format_temp(999.96, 0.1), "-");       // rounds to 1000.0: out of the format
  CHECK_STR(format_temp(-999.96, 0.1), "-");
  CHECK_STR(format_temp(999.4, 1), "999");
  CHECK_STR(format_temp(999.5, 1), "-");
  CHECK_STR(format_temp(999.94, 0.1), "999.9");
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

TEST(setpoint_back_to_reported_value_while_a_send_is_in_flight_is_sent) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  sp.on_tap(+1, 1000);
  int v = 0;
  CHECK(sp.take(1600, v) && v == 2150);     // sent 21.5, HA has not reported it yet
  sp.on_tap(-1, 1700);                      // back to 21.0, which is still what HA says
  CHECK(sp.shown() == 2100);
  CHECK(!sp.tick(1700 + 5000));             // not reverting while the user is mid-way
  CHECK(sp.take(2300, v) && v == 2100);     // 21.0 equals the reported value but 21.5 is on its way: send it
  sp.on_state(21.5, 2400);                  // late report of the first send
  CHECK(sp.pending() && sp.shown() == 2100);
  CHECK(!sp.tick(2300 + 4999));
  sp.on_state(21.0, 2500);                  // HA now at 21.0
  CHECK(!sp.pending() && sp.shown() == 2100);
}

TEST(setpoint_intermediate_report_does_not_lose_the_value) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  sp.on_tap(+1, 1000);
  int v = 0;
  CHECK(sp.take(1600, v) && v == 2150);
  sp.on_tap(-1, 1700);                      // pending 21.0, not sent yet
  sp.on_state(21.5, 1800);                  // HA confirms the first send before our second goes out
  CHECK(sp.pending() && sp.shown() == 2100);
  CHECK(sp.take(2300, v) && v == 2100);     // 21.0 differs from 21.5: sent
  sp.on_state(21.0, 2400);
  CHECK(!sp.pending());
}

TEST(setpoint_stale_report_equal_to_pending_does_not_cancel_it) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  sp.on_tap(+1, 1000);
  int v = 0;
  CHECK(sp.take(1600, v));                  // 21.5 in flight
  sp.on_tap(-1, 1700);                      // pending 21.0
  sp.on_state(21.0, 1800);                  // a stale report of the old value: must not settle
  CHECK(sp.pending());
  CHECK(sp.take(2300, v) && v == 2100);
}

TEST(setpoint_after_confirmation_nothing_is_in_flight) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  sp.on_tap(+1, 1000);
  int v = 0;
  CHECK(sp.take(1600, v));
  sp.on_tap(+1, 1650);                      // a newer value is pending when the confirmation comes
  sp.on_state(21.5, 1700);                  // confirmed: nothing in flight any more
  CHECK(sp.pending() && sp.shown() == 2200);
  sp.on_tap(-1, 2100);                      // back to what HA has
  CHECK(!sp.take(2700, v));
  CHECK(!sp.pending() && sp.shown() == 2150);   // settled at once, no wait for a timeout
}

TEST(setpoint_same_value_as_in_flight_is_not_sent_twice_but_rearms_the_wait) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  sp.on_tap(+1, 1000);
  int v = 0;
  CHECK(sp.take(1600, v) && v == 2150);
  sp.on_tap(+1, 1700);
  sp.on_tap(-1, 1800);                      // 21.5 again
  CHECK(!sp.take(2400, v));                 // already on its way
  CHECK(sp.pending());
  CHECK(!sp.tick(2400 + 4999));
  CHECK(sp.tick(2400 + 5000));              // still no answer: back to the real value
  CHECK(sp.shown() == 2100 && !sp.pending());
  // and now a plain "back to reported" is skipped again
  sp.on_tap(+1, 9000);
  sp.on_tap(-1, 9100);
  CHECK(!sp.take(9700, v) && !sp.pending());
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

struct StatusRow {
  const char *status;
  bool manual;
  Pill pill;
  bool controls;       // also bar_visible
  bool can_resume;
};

TEST(pill_every_status_of_the_integration) {
  const StatusRow rows[] = {
      {"ok", false, Pill::AUTO, true, false},
      {"ok", true, Pill::MANUAL, true, true},
      {"paused", false, Pill::PAUSED, false, false},
      {"paused", true, Pill::PAUSED, false, false},
      {"unassigned", false, Pill::NO_PROGRAM, false, false},
      {"unassigned", true, Pill::NO_PROGRAM, false, false},
      {"not_running", false, Pill::NO_PROGRAM, false, false},
      {"not_running", true, Pill::NO_PROGRAM, false, false},
      {"unavailable", false, Pill::UNAVAILABLE, false, false},
      {"unavailable", true, Pill::UNAVAILABLE, false, false},
      {"missing", false, Pill::UNAVAILABLE, false, false},
      {"missing", true, Pill::UNAVAILABLE, false, false},
      {"unsupported_mode", false, Pill::PROBLEM, true, false},
      {"unsupported_mode", true, Pill::PROBLEM, true, true},
      {"unsupported_preset", false, Pill::PROBLEM, true, false},
      {"unsupported_preset", true, Pill::PROBLEM, true, true},
      {"out_of_range", false, Pill::PROBLEM, true, false},
      {"out_of_range", true, Pill::PROBLEM, true, true},
      {"error", false, Pill::PROBLEM, true, false},
      {"error", true, Pill::PROBLEM, true, true},
  };
  for (const auto &r : rows) {
    TileInput in = base_input();
    in.status = r.status;
    in.manual = r.manual;
    TileView v = tile_view(in);
    if (v.pill != r.pill) std::printf("status %s manual %d\n", r.status, (int) r.manual);
    CHECK(v.pill == r.pill);
    CHECK(v.controls == r.controls);
    CHECK(v.bar_visible == r.controls);
    CHECK(v.can_resume == r.can_resume);
    if (r.pill == Pill::NO_PROGRAM) CHECK(v.program.empty() && !v.show_until && !v.has_next);
  }
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
    l.temp.push_back(20 + (i % 2));
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

// ---------------------------------------------------------------- 5. step 4b: numbers, dot, layout

TEST(clamp_to_hostile_values) {
  CHECK(clamp_to(NaN, 0, MAX_EPOCH, 7) == 7);
  CHECK(clamp_to(INF, 0, MAX_EPOCH, 7) == 7);
  CHECK(clamp_to(-INF, 0, MAX_EPOCH, 7) == 7);
  CHECK(clamp_to(1e30, 0, MAX_EPOCH, 7) == MAX_EPOCH);
  CHECK(clamp_to(-5, 0, MAX_EPOCH, 7) == 0);
  CHECK(clamp_to(1792389600, 0, MAX_EPOCH, 7) == 1792389600);
  CHECK((int64_t) clamp_to(1e30, 0, MAX_EPOCH, 0) == 4000000000LL);
  CHECK((int) clamp_to(1e9, -1, 99, -1) == 99);
}

TEST(hvac_dot_needs_the_action) {
  CHECK(hvac_dot("heating") == 1);
  CHECK(hvac_dot("cooling") == 2);
  CHECK(hvac_dot("idle") == 0);
  CHECK(hvac_dot("off") == 0);
  CHECK(hvac_dot("") == 0);       // no hvac_action: no dot, whatever the state
  CHECK(hvac_dot("Heating") == 0);
}

TEST(now_marker_pointer_matches_vector) {
  const auto segs = bar_segments(bands_of({0, 360, 1020}, {360, 1020, 1440}, {1, 0, 1}, {17, 0, 21}, {0, 1, 0}), 302);
  for (int m : {0, 1, 359, 360, 700, 1019, 1020, 1439, 2000, -4})
    CHECK(now_marker(segs, m, 302) == now_marker(segs.data(), segs.size(), m, 302));
  CHECK(now_marker(nullptr, 0, 720, 302) == now_x(720, 302));
}

static LayoutInput li(int h, bool name = true) {
  LayoutInput in;
  in.width = 1000;
  in.height = h;
  in.digits_h = 48;
  in.value_h = 40;
  in.digits_w = 130;
  in.value_w = 100;
  in.key_w = 60;
  in.dot_w = 14;
  in.touch_h = 40;
  in.body_h = 17;
  in.tick_h = 14;
  in.gap = 4;
  in.has_name = name;
  return in;
}

static void check_layout_sane(const LayoutPlan &p, const LayoutInput &in) {
  CHECK(p.name_y >= 0 && p.then_y >= 0 && p.pill_y >= 0 && p.bar_y >= 0 && p.region_top >= 0 && p.mid_y >= 0);
  CHECK(p.region_h >= 1);
  if (!p.fits) return;
  if (p.family != Family::FULL) {
    // The compact plans: the target (and the "now" line when drawn) inside the region, the bar (COLUMN) under it.
    CHECK(p.mid_y + p.value_h + (p.now_line ? in.body_h : 0) <= p.region_top + p.region_h || p.region_h < p.value_h);
    CHECK(!p.name && !p.then && !p.pill);
    CHECK(p.keys == (p.family == Family::ROW) && p.bar == (p.family == Family::COLUMN));
    if (p.bar) CHECK(p.mid_y + p.value_h <= p.bar_y && p.bar_y + in.tick_h == in.height);
    return;
  }
  // Nothing overlaps from the top: name, then the digits and the "now" line, then "then", the pill and the bar.
  CHECK(p.mid_y >= p.region_top);
  CHECK(p.mid_y + p.value_h + in.body_h <= (p.then ? p.then_y : p.pill_y));
  if (p.then) CHECK(p.then_y + in.body_h <= p.pill_y);
  CHECK(p.pill_y + in.body_h <= p.bar_y);
  CHECK(p.bar_y + in.tick_h == in.height);
  if (p.name) CHECK(in.body_h <= p.region_top);
}

TEST(choose_layout_monotone_in_height) {
  for (bool name : {true, false}) {
    int last = 99;
    for (int h = 0; h <= 400; ++h) {
      const LayoutInput in = li(h, name);
      const LayoutPlan p = choose_layout(in);
      CHECK(p.rank <= last);   // taller never gives a poorer plan
      last = p.rank;
      check_layout_sane(p, in);
    }
    CHECK(choose_layout(li(400, name)).rank == (name ? 0 : 2));
  }
  // Narrow tiles too (no room for the keys, a room for the digits, a room for neither): taller never gives a poorer plan.
  for (int width : {20, 60, 120, 151, 200, 250, 302, 1000}) {
    int last = 99;
    for (int h = 0; h <= 400; ++h) {
      LayoutInput in = li(h);
      in.width = width;
      const LayoutPlan p = choose_layout(in);
      CHECK(p.rank <= last);
      last = p.rank;
      check_layout_sane(p, in);
    }
  }
}

TEST(choose_layout_picks_the_richest_that_fits) {
  // digits 48 + now 17 + pill 17 + bar 14 + 2 gaps of 4 = 104: the plan with the large digits and nothing else.
  LayoutPlan p = choose_layout(li(104));
  CHECK(p.fits && p.digits && !p.name && !p.then && p.rank == 3);
  p = choose_layout(li(104 + 17 + 4));   // room for the name
  CHECK(p.fits && p.digits && p.name && !p.then && p.rank == 1);
  p = choose_layout(li(104 + 2 * (17 + 4)));
  CHECK(p.fits && p.digits && p.name && p.then && p.rank == 0);
  p = choose_layout(li(103));   // the large digits do not fit: the smaller face
  CHECK(p.fits && !p.digits && p.value_h == 40 && p.rank == 7);
  p = choose_layout(li(104 + 17 + 4, false));   // no name: the "then" line takes the room
  CHECK(p.fits && p.digits && !p.name && p.then && p.rank == 2);
}

TEST(choose_layout_degrades_without_negative_positions) {
  for (int h : {-20, 0, 1, 10, 30, 39}) {
    for (bool name : {true, false}) {
      const LayoutInput in = li(h, name);
      const LayoutPlan p = choose_layout(in);
      CHECK(!p.fits && p.family == Family::TINY && p.rank == 10 && !p.digits && !p.name && !p.then && !p.keys && !p.bar);
      check_layout_sane(p, in);
    }
  }
  LayoutInput in = li(0);
  in.gap = -3;
  in.body_h = 0;
  in.tick_h = 0;
  check_layout_sane(choose_layout(in), in);
  in.width = -5;
  check_layout_sane(choose_layout(in), in);
}

// The families by pixels. Tile sizes are those of the bench: cyd 151 x 55 per cell, guition 224 x 124.
static LayoutInput tile_px(int w, int h, int key_w = 62) {
  LayoutInput in = li(h);
  in.width = w;
  in.key_w = key_w;
  in.digits_w = 140;
  in.value_w = 85;
  in.dot_w = 14;
  in.now_w = 90;
  in.touch_h = 39;
  return in;
}

TEST(choose_layout_full_look_needs_the_width_for_the_keys_too) {
  // Tall enough for the poorest FULL plan (100) but 2 keys + gaps (132) and the target (85) do not fit in 200 px.
  LayoutPlan p = choose_layout(tile_px(200, 124));
  CHECK(p.family != Family::FULL);
  p = choose_layout(tile_px(302, 110));   // cyd 2x2
  CHECK(p.family == Family::FULL && p.keys && p.bar && p.pill && p.now_line && p.digits);
  p = choose_layout(tile_px(448, 248));   // guition 2x2
  CHECK(p.family == Family::FULL && p.rank <= 1);
  // Digits need 2*(62+4) + 140 = 272 across; the VALUE face 217.
  p = choose_layout(tile_px(250, 124));
  CHECK(p.family == Family::FULL && !p.digits);
  p = choose_layout(tile_px(272, 124));
  CHECK(p.family == Family::FULL && p.digits);
}

TEST(choose_layout_tiny_is_the_target_alone) {
  LayoutInput in = tile_px(151, 55);   // cyd 1x1
  LayoutPlan p = choose_layout(in);
  CHECK(p.family == Family::TINY && p.rank == 10 && p.fits);
  CHECK(!p.keys && !p.bar && !p.pill && !p.name && !p.then && !p.now_line);
  CHECK(!p.digits);                                   // the digits (140) and the dot (14) and the gaps do not fit 151
  CHECK(p.mid_y == (55 - 40) / 2 && p.value_h == 40);
  p = choose_layout(tile_px(224, 52, 74));            // wide enough for the digits and the dot, no room for a bar under them
  CHECK(p.family == Family::TINY && p.digits && p.value_h == 48 && p.mid_y == 2);
  p = choose_layout(tile_px(100, 52));                // too narrow for the digits with a dot
  CHECK(p.family == Family::TINY && !p.digits);
  // The VALUE face does not fit the height: still TINY, flagged as not fitting, nothing negative.
  p = choose_layout(tile_px(151, 30));
  CHECK(p.family == Family::TINY && !p.fits && p.mid_y == 0);
}

TEST(choose_layout_row_keeps_the_keys_and_drops_the_rest) {
  LayoutInput in = tile_px(302, 55);   // cyd 2x1
  LayoutPlan p = choose_layout(in);
  CHECK(p.family == Family::ROW && p.rank == 8 && p.fits && p.keys);
  CHECK(!p.bar && !p.pill && !p.name && !p.then && !p.now_line);   // 55 < 40 + 4 + 17
  CHECK(p.digits);                                                 // 302 >= 272 and 55 >= 48
  CHECK(p.mid_y == (55 - 48) / 2);
  p = choose_layout(tile_px(302, 38));   // lower than a key can be pressed (39): no keys
  CHECK(p.family != Family::ROW);
  p = choose_layout(tile_px(302, 39));   // nor lower than the VALUE face (40)
  CHECK(p.family != Family::ROW);
  p = choose_layout(tile_px(302, 40));
  CHECK(p.family == Family::ROW && !p.digits && p.value_h == 40);
  p = choose_layout(tile_px(250, 70));   // narrower: the VALUE face between the keys
  CHECK(p.family == Family::ROW && !p.digits);
  p = choose_layout(tile_px(302, 70));   // the "now" line fits below the target (48 + 4 + 17 = 69)
  CHECK(p.family == Family::ROW && p.now_line && p.mid_y == (70 - (48 + 17)) / 2);
  LayoutInput tall_keys = tile_px(302, 55);   // keys that need 7 mm = 60 px: a tile 55 high has none
  tall_keys.touch_h = 60;
  CHECK(choose_layout(tall_keys).family != Family::ROW);
  tall_keys.height = 60;
  CHECK(choose_layout(tall_keys).family == Family::ROW);
  p = choose_layout(tile_px(220, 70));   // keys and the target fit (217) but not the "now" line (222): no line, the dot beside
  CHECK(p.family == Family::ROW && !p.now_line);
  p = choose_layout(tile_px(220, 124));  // the same for the full look, which needs the "now" line: the row
  CHECK(p.family == Family::ROW && !p.now_line);
  p = choose_layout(tile_px(210, 70));   // too narrow for the keys and the target: the column
  CHECK(p.family != Family::ROW);
}

TEST(choose_layout_column_is_target_dot_and_bar) {
  LayoutInput in = tile_px(151, 110);   // cyd 1x2
  LayoutPlan p = choose_layout(in);
  CHECK(p.family == Family::COLUMN && p.rank == 9 && p.fits && p.bar && !p.keys && !p.pill && !p.now_line);
  CHECK(p.bar_y == 110 - 14);
  CHECK(p.mid_y + p.value_h <= p.bar_y);
  p = choose_layout(tile_px(151, 40 + 4 + 14));   // exactly the VALUE face, a gap and the bar
  CHECK(p.family == Family::COLUMN && !p.digits && p.fits);
  p = choose_layout(tile_px(151, 40 + 4 + 14 - 1));   // one pixel less: no bar
  CHECK(p.family == Family::TINY);
  p = choose_layout(tile_px(224, 248, 74));   // guition 1x2: keys (2 x 78) and the target do not fit 224; digits + dot do
  CHECK(p.family == Family::COLUMN && p.digits);
}

TEST(choose_layout_never_picks_a_poorer_plan_for_a_bigger_tile) {
  // Across widths and heights of the real boards, a tile at least as wide and as tall never gets a higher rank.
  for (int w = 40; w <= 520; w += 7)
    for (int h = 0; h <= 300; h += 5) {
      const int r = choose_layout(tile_px(w, h)).rank;
      CHECK(choose_layout(tile_px(w, h + 5)).rank <= r);
      CHECK(choose_layout(tile_px(w + 7, h)).rank <= r);
      check_layout_sane(choose_layout(tile_px(w, h)), tile_px(w, h));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Step 5: the command mode, the tap zones, the single resume, cancel

TEST(command_mode_is_only_the_thermostats_own_heat_or_cool) {
  // The keys never switch a room on and never change its mode: only a climate that heats or cools is commanded, and with its
  // own mode; the band plays no part.
  CHECK(command_mode_code("heat") == 1 && command_mode_code("cool") == 2);
  for (const char *state : {"off", "auto", "heat_cool", "dry", "fan_only", "unavailable", ""}) CHECK(command_mode_code(state) == -1);
  for (const char *state : {"off", "auto", "heat_cool", "dry", "fan_only", "unavailable", "unknown", "", "HEAT"}) {
    TileInput in = base_input();
    in.climate_state = state;
    TileView v = tile_view(in);
    const bool down = std::string(state).empty() || std::string(state) == "unavailable" || std::string(state) == "unknown";
    CHECK(!v.can_set);
    if (!down) CHECK(v.controls);                       // the room is commandable in principle, the mode is not
  }
  TileInput heat = base_input();
  heat.climate_state = "heat";
  TileView h = tile_view(heat);
  CHECK(h.can_set && h.command_mode == Mode::HEAT);
  TileInput cool = base_input();
  cool.climate_state = "cool";
  TileView c = tile_view(cool);
  CHECK(c.can_set && c.command_mode == Mode::COOL);
  CHECK_STR(mode_name(Mode::HEAT), "heat");
  CHECK_STR(mode_name(Mode::COOL), "cool");
  CHECK_STR(mode_name(Mode::OFF), "off");
}

TEST(tile_view_can_set_needs_controls_and_heat_or_cool) {
  TileInput in = base_input();
  TileView v = tile_view(in);
  CHECK(v.controls && v.can_set && v.command_mode == Mode::HEAT);
  // no command where the room cannot be commanded, whatever the climate does
  for (const char *status : {"unassigned", "not_running", "paused", "unavailable", "missing"}) {
    TileInput x = base_input();
    x.status = status;
    TileView w = tile_view(x);
    CHECK(!w.controls && !w.can_set);
  }
  TileInput none = base_input();
  none.has_room = false;
  CHECK(!tile_view(none).can_set);
  TileInput down = base_input();
  down.climate_state = "unavailable";
  CHECK(!tile_view(down).can_set);
  // a manual override can be resumed, and + and - still work during it
  TileInput man = base_input();
  man.manual = true;
  TileView m = tile_view(man);
  CHECK(m.pill == Pill::MANUAL && m.can_resume && m.can_set);
  CHECK(!tile_view(base_input()).can_resume);
}

TEST(pill_is_a_key_when_the_override_can_be_resumed_also_in_problem) {
  TileInput in = base_input();
  in.manual = true;
  in.status = "error";
  TileView v = tile_view(in);
  CHECK(v.pill == Pill::PROBLEM && v.can_resume);
  CHECK(pill_is_key(v, true));                          // "Problem x": the cross and the zone, as for MANUAL
  CHECK(!pill_is_key(v, false));                        // no climate id: inert
  in.status = "ok";
  CHECK(pill_is_key(tile_view(in), true));
  in.manual = false;
  CHECK(!pill_is_key(tile_view(in), true));             // automatic: not a key
  for (const char *status : {"paused", "unassigned", "unavailable"}) {
    TileInput x = base_input();
    x.manual = true;
    x.status = status;
    CHECK(!pill_is_key(tile_view(x), true));            // nothing to resume where the room is not commanded
  }
}

TEST(setpoint_cancel_drops_the_wait_and_shows_the_real_value) {
  Setpoint sp = make_sp();
  sp.on_state(21.0, 0);
  sp.on_tap(+1, 1000);
  CHECK(sp.pending() && sp.shown() == 2150);
  sp.cancel();
  CHECK(!sp.pending() && sp.shown() == 2100);
  int v = 0;
  CHECK(!sp.due(5000) && !sp.take(5000, v));            // nothing goes out after a cancel
  // also after the send: nothing is waited for, and no revert later
  sp.on_tap(+1, 6000);
  CHECK(sp.take(6600, v) && v == 2150);
  sp.cancel();
  CHECK(sp.shown() == 2100 && !sp.pending());
  CHECK(!sp.tick(20000));
  sp.on_state(21.5, 21000);                             // a late report is just the real value
  CHECK(sp.shown() == 2150);
}

TEST(setpoint_full_sequence_one_send_for_three_taps_and_a_return) {
  Setpoint sp = make_sp();
  sp.on_state(20.0, 0);
  int sends = 0, last = 0, v = 0;
  const uint32_t taps[] = {1000, 1200, 1400};
  for (uint32_t at : taps) CHECK(sp.on_tap(+1, at));
  for (uint32_t now = 1400; now <= 4000; now += 250) {    // the 250 ms interval, from the last tap on
    if (sp.take(now, v)) { ++sends; last = v; }
    sp.tick(now);
    if (now < 2000) CHECK(sends == 0);                    // 600 ms after the LAST tap (1400): not before 2000
  }
  CHECK(sends == 1 && last == 2150);
  CHECK(sp.shown() == 2150);                              // still shown: the 5 s have not passed
  for (uint32_t now = 4000; now <= 9000; now += 250) sp.tick(now);
  CHECK(sp.shown() == 2000 && !sp.pending());             // no answer: back to the real value
}

TEST(setpoint_on_limits_does_not_send_twice) {
  Setpoint sp = make_sp();
  sp.on_state(35.0, 0);
  CHECK(!sp.on_tap(+1, 100));                             // already at the top: nothing changes, nothing is due
  CHECK(!sp.due(2000));
  int v = 0;
  CHECK(!sp.take(2000, v));
}

static TapGeometry zones_for(int w, int h, int key_w, int key_y, int key_h, bool pill, bool bar) {
  TapGeometry g;
  g.w = w;
  g.h = h;
  g.key_w = key_w;
  g.key_y = key_y;
  g.key_h = key_h;
  g.gap = 8;
  g.key_reach = 40;
  g.pill_tappable = pill;
  g.pill_x0 = 80;
  g.pill_x1 = 220;
  g.bar_tappable = bar;
  g.keys_live = true;
  return g;
}

TEST(pill_span_is_the_chip_plus_a_margin_inside_the_tile) {
  int x0 = -1, x1 = -1;
  pill_span(300, 100, 10, x0, x1);
  CHECK(x0 == 90 && x1 == 210);                           // 100 wide chip centred, 10 px each side
  pill_span(300, 290, 20, x0, x1);
  CHECK(x0 == 0 && x1 == 300);                            // never beyond the tile
  pill_span(300, 0, 10, x0, x1);
  CHECK(x0 == 140 && x1 == 160);
  pill_span(0, 50, 10, x0, x1);
  CHECK(x0 == 0 && x1 == 0);
  pill_span(301, 100, 10, x0, x1);
  CHECK(x0 <= 91 && x1 >= 209 && x1 <= 301 && x0 >= 0);
}

TEST(lower_zones_cyd_like_small_tile) {
  // the CYD 2x2: 302x110, keys end at 82, pill text at 71 (17 high), bar 18 high at the bottom
  int pt = 0, pb = 0, bt = 0;
  lower_zones(110, 82, 71, 17, 68, 18, 40, false, pt, pb, bt);
  CHECK(bt == 82 && pt == pb && pb == bt);                // no pill key: the bar zone takes the strip under the keys
  lower_zones(110, 82, 71, 17, 68, 18, 40, true, pt, pb, bt);
  CHECK(pb == 92 && bt == pb);                            // 110 - 18: the bar keeps its own strip
  CHECK(pt >= 68 && pt < pb);
  CHECK(pb - pt <= 40);
  CHECK(110 - bt >= 18);
}

TEST(lower_zones_large_tile_gives_the_pill_the_full_height) {
  int pt = 0, pb = 0, bt = 0;
  lower_zones(277, 150, 215, 24, 140, 20, 66, true, pt, pb, bt);
  CHECK(pb - pt == 66);                                   // 7 mm at 149 dpi is about 41 px; 66 stands for "wanted"
  CHECK(pt >= 140 && pb <= 277 - 20 && bt == pb);
  lower_zones(277, 150, 215, 24, 140, 20, 66, false, pt, pb, bt);
  CHECK(bt == 277 - 66);
}

TEST(lower_zones_degenerate_inputs_stay_inside_the_tile) {
  for (int h : {0, 1, 5, 20, 110}) {
    for (bool pill : {false, true}) {
      int pt = -1, pb = -1, bt = -1;
      lower_zones(h, 0, 0, 0, 0, 0, 0, pill, pt, pb, bt);
      CHECK(pt >= 0 && pt <= pb && pb <= std::max(h, 1) && bt >= 0 && bt <= std::max(h, 1));
      lower_zones(h, 500, 900, 30, 800, 500, 400, pill, pt, pb, bt);
      CHECK(pt >= 0 && pt <= pb && bt >= 0 && bt <= std::max(h, 1) + 0);
    }
  }
}

TEST(zone_at_partitions_the_tile) {
  TapGeometry g = zones_for(300, 110, 60, 10, 64, true, true);
  g.pill_top = 60;
  g.pill_bottom = 90;
  g.bar_top = 90;
  CHECK(zone_at(g, 10, 30) == Zone::PLUS);
  CHECK(zone_at(g, 289, 30) == Zone::MINUS);
  CHECK(zone_at(g, 150, 30) == Zone::NONE);               // the numbers: nothing
  CHECK(zone_at(g, 63, 30) == Zone::PLUS);                // half the gap beside the key
  CHECK(zone_at(g, 64, 30) == Zone::NONE);
  CHECK(zone_at(g, 235, 30) == Zone::NONE);
  CHECK(zone_at(g, 236, 30) == Zone::MINUS);
  CHECK(zone_at(g, 150, 60) == Zone::PILL);
  CHECK(zone_at(g, 80, 75) == Zone::PILL);                // the pill zone is the chip and a margin: x 80..219
  CHECK(zone_at(g, 219, 75) == Zone::PILL);
  CHECK(zone_at(g, 79, 75) == Zone::NONE);                // beside it nothing: a stray touch does not drop the override
  CHECK(zone_at(g, 220, 75) == Zone::NONE);
  CHECK(zone_at(g, 10, 75) == Zone::NONE);
  CHECK(zone_at(g, 290, 75) == Zone::NONE);
  CHECK(zone_at(g, 150, 89) == Zone::PILL);
  CHECK(zone_at(g, 150, 90) == Zone::BAR);
  CHECK(zone_at(g, 10, 109) == Zone::BAR);
  CHECK(zone_at(g, 150, 59) == Zone::NONE);
  // outside the tile and unknown points
  CHECK(zone_at(g, -1, -1) == Zone::NONE);
  CHECK(zone_at(g, 300, 30) == Zone::NONE);
  CHECK(zone_at(g, 100, 110) == Zone::NONE);
  CHECK(zone_at(g, -1, 30) == Zone::NONE);
}

TEST(zone_at_every_point_has_exactly_one_answer_and_pill_is_off_unless_manual) {
  TapGeometry g = zones_for(300, 110, 60, 10, 64, false, true);
  g.bar_top = 82;
  g.pill_top = g.pill_bottom = 82;
  g.keys_live = true;
  for (int y = 0; y < 110; ++y)
    for (int x = 0; x < 300; ++x) {
      const Zone z = zone_at(g, x, y);
      CHECK(z != Zone::PILL);                              // not a key while the room is in automatic
      if (y >= 82) CHECK(z == Zone::BAR);
      else CHECK(z == Zone::NONE || z == Zone::PLUS || z == Zone::MINUS);
    }
}

TEST(zone_at_inert_keys_and_no_bar) {
  TapGeometry g = zones_for(300, 110, 60, 10, 64, true, false);
  g.pill_top = 60;
  g.pill_bottom = 90;
  g.bar_top = 90;
  g.keys_live = false;
  CHECK(zone_at(g, 10, 30) == Zone::NONE);                // inert keys: a tap on them is nothing, never the bar's card
  CHECK(zone_at(g, 289, 30) == Zone::NONE);
  CHECK(zone_at(g, 150, 100) == Zone::NONE);              // no bar to open
  CHECK(zone_at(g, 150, 70) == Zone::PILL);
}

TEST(zone_at_small_keys_grow_to_the_reach_but_never_into_the_pill) {
  TapGeometry g = zones_for(300, 110, 60, 20, 20, true, true);   // keys 20 high, reach 40: 10 more above and below
  g.pill_top = 45;
  g.pill_bottom = 90;
  g.bar_top = 90;
  CHECK(zone_at(g, 10, 10) == Zone::PLUS);
  CHECK(zone_at(g, 10, 9) == Zone::NONE);
  CHECK(zone_at(g, 10, 44) == Zone::PLUS);
  CHECK(zone_at(g, 100, 45) == Zone::PILL);               // the pill zone cuts the key's reach
  CHECK(zone_at(g, 10, 45) == Zone::NONE);
}

TEST(compact_zones_tiny_and_column_open_the_list_everywhere_and_never_command) {
  for (Family f : {Family::TINY, Family::COLUMN}) {
    for (bool live : {true, false}) {
      TapGeometry g = zones_for(151, 55, 62, 0, 55, true, false);   // stale full-look values must not leak through
      g.centre_opens = true;
      compact_zones(g, f, live);
      for (int y = -1; y <= 55; ++y)
        for (int x = -1; x <= 151; ++x) {
          const Zone z = zone_at(g, x, y);
          if (x < 0 || y < 0 || x >= 151 || y >= 55) CHECK(z == Zone::NONE);
          else CHECK(z == Zone::BAR);   // the list, never PLUS, MINUS or PILL
        }
    }
  }
}

TEST(compact_zones_row_keys_at_the_sides_list_between) {
  TapGeometry g = zones_for(302, 55, 62, 0, 55, true, true);
  g.bar_top = 20;
  compact_zones(g, Family::ROW, true);
  CHECK(zone_at(g, 0, 0) == Zone::PLUS);
  CHECK(zone_at(g, 65, 54) == Zone::PLUS);                // half the gap (4) beside the key: 62 + 4 = 66
  CHECK(zone_at(g, 66, 0) == Zone::BAR);
  CHECK(zone_at(g, 150, 30) == Zone::BAR);
  CHECK(zone_at(g, 235, 54) == Zone::BAR);
  CHECK(zone_at(g, 236, 0) == Zone::MINUS);
  CHECK(zone_at(g, 301, 54) == Zone::MINUS);
  CHECK(zone_at(g, 302, 10) == Zone::NONE);
  // Inert keys (the thermostat is not in heat or cool): their columns do nothing, the middle still lists.
  compact_zones(g, Family::ROW, false);
  CHECK(zone_at(g, 10, 10) == Zone::NONE && zone_at(g, 290, 10) == Zone::NONE);
  CHECK(zone_at(g, 150, 10) == Zone::BAR);
  // Every point has one answer, and no PILL.
  for (int y = 0; y < 55; ++y)
    for (int x = 0; x < 302; ++x) CHECK(zone_at(g, x, y) != Zone::PILL);
}

TEST(resume_gate_sends_once_until_ha_answers) {
  ResumeGate gate;
  CHECK(gate.press(1000));
  CHECK(gate.waiting(1001));
  CHECK(!gate.press(1100));                               // a second press while waiting: nothing
  CHECK(!gate.press(5999));
  gate.on_state(true);                                    // HA still says manual: the wait goes on
  CHECK(!gate.press(3000));
  gate.on_state(false);                                   // the override is gone
  CHECK(!gate.waiting(3001));
  CHECK(gate.press(3002));                                // a new override can be resumed again
  CHECK(!gate.tick(7000));
  CHECK(gate.tick(8002));                                 // 5 s without an answer: the tile repaints once
  CHECK(!gate.tick(8003));
  CHECK(gate.press(8004));                                // and the key works again
}

TEST(resume_gate_handles_the_millisecond_wrap) {
  ResumeGate gate;
  const uint32_t near_wrap = 0xFFFFFF00u;
  CHECK(gate.press(near_wrap));
  CHECK(!gate.press(near_wrap + 1000u));                  // wraps past zero inside the 5 s
  CHECK(gate.press(near_wrap + 5000u));
}


// ---------------------------------------------------------------- 13. presets, tints, joints, labels (plan 4, version 0.2.2)

static std::vector<Band> bands_with_presets(const V &s, const V &e, const V &t, const std::vector<std::string> &presets, int mode = 1) {
  DayLists l;
  l.start = s;
  l.end = e;
  for (size_t i = 0; i < s.size(); ++i) {
    l.mode.push_back(mode);
    l.autom.push_back(0);
  }
  l.temp = t;
  l.preset = presets;
  return normalize_bands(l);
}

TEST(adjacent_bands_with_other_preset_stay_apart) {
  // "Casa" on a Tuesday: heat away 00:00-15:00, home 15:00-22:30, away 22:30-24:00, none with a temperature.
  auto b = bands_with_presets({0, 900, 1350}, {900, 1350, 1440}, {-100, -100, -100}, {"away", "home", "away"});
  CHECK(b.size() == 3);
  CHECK(b[0].preset == "away" && b[1].preset == "home" && b[2].preset == "away");
  CHECK(b[0].end == 900 && b[1].start == 900 && b[1].end == 1350 && b[2].start == 1350);
  CHECK(b[0].tint == 0 && b[1].tint == 1 && b[2].tint == 0);
  // the same preset in two adjacent bands is one band, as before
  b = bands_with_presets({0, 600}, {600, 1440}, {-100, -100}, {"eco", "eco"});
  CHECK(b.size() == 1 && b[0].start == 0 && b[0].end == 1440 && b[0].preset == "eco");
  // with and without a preset are two bands; the blanks around a preset do not count
  b = bands_with_presets({0, 600}, {600, 1440}, {-100, -100}, {"eco", ""});
  CHECK(b.size() == 2);
  b = bands_with_presets({0, 600}, {600, 1440}, {-100, -100}, {" eco\t", "eco"});
  CHECK(b.size() == 1 && b[0].preset == "eco");
  // a list of presets shorter than the others: the bands past it have none
  b = bands_with_presets({0, 600}, {600, 1440}, {-100, -100}, {"eco"});
  CHECK(b.size() == 2 && b[1].preset.empty());
  // the same temperature and the same preset merge; the same temperature with another preset does not
  b = bands_with_presets({0, 600}, {600, 1440}, {20, 20}, {"eco", "comfort"});
  CHECK(b.size() == 2);
  b = bands_with_presets({0, 600}, {600, 1440}, {20, 20}, {"eco", "eco"});
  CHECK(b.size() == 1);
  // another temperature with the same preset does not merge (it never did)
  b = bands_with_presets({0, 600}, {600, 1440}, {20, 21}, {"eco", "eco"});
  CHECK(b.size() == 2);
}

TEST(preset_is_clean_and_short) {
  CHECK_STR(detail::clean_preset("  away \n"), "away");
  CHECK_STR(detail::clean_preset(" \t "), "");
  CHECK_STR(detail::clean_preset(""), "");
  const std::string long40(40, 'a'), long41(41, 'a');
  CHECK_STR(detail::clean_preset(long40), long40);
  CHECK_STR(detail::clean_preset(long41), long40);   // cut at 40 bytes
  // never in the middle of a character: 39 letters and a two-byte one end at byte 41, so the character goes
  const std::string accent = std::string(39, 'a') + "\xC3\xA9" + "z";
  CHECK_STR(detail::clean_preset(accent), std::string(39, 'a'));
  CHECK_STR(detail::clean_preset(std::string(38, 'a') + "\xC3\xA9"), std::string(38, 'a') + "\xC3\xA9");
}

struct TintBand {
  int mode;
  bool has_temp;
  double temp;
  const char *preset;
  int tint;
};
struct TintCase {
  const char *name;
  std::vector<TintBand> bands;
};
static const std::vector<TintCase> &tint_cases() {
  static const std::vector<TintCase> c = {
#include "tints.inc"
  };
  return c;
}

TEST(tints_parity_with_editor_band_tints) {
  CHECK(tint_cases().size() >= 20);
  for (auto &c : tint_cases()) {
    // one minute between bands: nothing merges, so band i of the day is band i of the case
    DayLists l;
    for (size_t i = 0; i < c.bands.size(); ++i) {
      l.start.push_back((double) (i * 30));
      l.end.push_back((double) (i * 30 + 20));
      l.mode.push_back(c.bands[i].mode);
      l.temp.push_back(c.bands[i].has_temp ? c.bands[i].temp : NO_TEMP);
      l.autom.push_back(0);
      l.preset.push_back(c.bands[i].preset);
    }
    const auto b = normalize_bands(l);
    CHECK(b.size() == c.bands.size());
    if (b.size() != c.bands.size()) continue;
    // the same through TintBook asked one band after the other
    TintBook book;
    for (size_t i = 0; i < b.size(); ++i) {
      if (b[i].tint != c.bands[i].tint) std::printf("  case \"%s\", band %zu: %d, editor %d\n", c.name, i, b[i].tint, c.bands[i].tint);
      CHECK(b[i].tint == c.bands[i].tint);
      CHECK(book.tint(mode_from_code(c.bands[i].mode), c.bands[i].has_temp, c.bands[i].temp, c.bands[i].preset) == c.bands[i].tint);
    }
  }
}

TEST(tint_book_rules) {
  TintBook t;
  CHECK(t.tint(Mode::HEAT, true, 20, "") == 0);
  CHECK(t.tint(Mode::HEAT, true, 21, "") == 1);
  CHECK(t.tint(Mode::HEAT, true, 20, "") == 0);           // the same value, the same tint
  CHECK(t.tint(Mode::COOL, true, 20, "") == 0);           // modes count apart
  CHECK(t.tint(Mode::COOL, true, 26, "") == 1);
  CHECK(t.tint(Mode::HEAT, true, 22, "") == 2);
  CHECK(t.tint(Mode::HEAT, true, 23, "") == 3);
  CHECK(t.tint(Mode::HEAT, true, 24, "") == 0);           // the fifth value goes round
  CHECK(t.tint(Mode::HEAT, true, 25, "") == 1);
  CHECK(t.tint(Mode::HEAT, true, 24, "") == 0);           // and stays what it was
  TintBook u;
  CHECK(u.tint(Mode::HEAT, false, 0, "away") == 0);       // no temperature: the preset is the value
  CHECK(u.tint(Mode::HEAT, false, 0, "home") == 1);
  CHECK(u.tint(Mode::HEAT, false, 0, " away ") == 0);     // blanks do not count
  CHECK(u.tint(Mode::HEAT, true, 20, "away") == 2);       // a temperature is a value of its own; the preset beside it does not count
  CHECK(u.tint(Mode::HEAT, true, 20, "home") == 2);
  CHECK(u.tint(Mode::HEAT, false, 0, "") == 0);           // neither: tint 0, and it takes no place
  CHECK(u.tint(Mode::HEAT, false, 0, "   ") == 0);
  CHECK(u.tint(Mode::HEAT, true, 21, "") == 3);
  CHECK(u.tint(Mode::OFF, false, 0, "") == 0);
  // a temperature of 20 is not the preset "20"
  TintBook w;
  CHECK(w.tint(Mode::HEAT, true, 20, "") == 0);
  CHECK(w.tint(Mode::HEAT, false, 0, "20") == 1);
}

TEST(tints_are_of_the_whole_day_before_a_window_is_taken) {
  // 20 bands of 20 minutes with a minute between them, each with its own temperature: more than the 16 the bar keeps
  DayLists l;
  for (int i = 0; i < 20; ++i) {
    l.start.push_back(i * 30);
    l.end.push_back(i * 30 + 20);
    l.mode.push_back(1);
    l.temp.push_back(10 + i);
    l.autom.push_back(0);
  }
  const Day last = normalize_day(l, 20 * 30 - 5);
  CHECK(last.bands.size() == MAX_BANDS && last.truncated_before);
  for (size_t i = 0; i < last.bands.size(); ++i) CHECK(last.bands[i].tint == (int) ((i + 4) % TINT_COUNT));   // band 4.. of the day
  const Day first = normalize_day(l, 0);
  for (size_t i = 0; i < first.bands.size(); ++i) CHECK(first.bands[i].tint == (int) (i % TINT_COUNT));
}

TEST(segments_and_rows_carry_preset_and_tint) {
  auto b = bands_with_presets({0, 900, 1350}, {900, 1350, 1440}, {-100, -100, -100}, {"away", "home", "away"});
  auto s = bar_segments(b, 300);
  CHECK(s.size() == 3);
  CHECK(s[0].preset == "away" && s[1].preset == "home" && s[2].preset == "away");
  CHECK(s[0].tint == 0 && s[1].tint == 1 && s[2].tint == 0);
  DayLists l;
  l.start = {0, 900, 1350};
  l.end = {900, 1350, 1440};
  l.mode = {1, 1, 1};
  l.temp = {-100, -100, -100};
  l.autom = {0, 0, 0};
  l.preset = {"away", "home", "away"};
  auto rows = card_rows(l, 1000);
  CHECK(rows.size() == 3);
  CHECK(rows[1].preset == "home" && rows[1].tint == 1 && rows[1].current);
  CHECK(rows[0].tint == 0 && rows[2].tint == 0 && rows[2].preset == "away");
}

static std::vector<Segment> touching(const std::vector<std::pair<int, int>> &xs) {
  std::vector<Segment> v;
  for (auto &x : xs) {
    Segment s;
    s.x0 = x.first;
    s.x1 = x.second;
    v.push_back(s);
  }
  return v;
}

TEST(separate_leaves_a_clear_joint) {
  const auto seg = touching({{0, 50}, {50, 120}, {120, 150}});
  auto sp = separate(seg, 2);
  CHECK(sp.size() == 3);
  CHECK(sp[0].x0 == 0 && sp[0].x1 == 49);        // the day's ends are left alone
  CHECK(sp[1].x0 == 51 && sp[1].x1 == 119);      // one pixel off each side of each joint
  CHECK(sp[2].x0 == 121 && sp[2].x1 == 150);
  CHECK(sp[1].x0 - sp[0].x1 == 2 && sp[2].x0 - sp[1].x1 == 2);
  sp = separate(seg, 3);                          // an odd gap: the left side gives the smaller half
  CHECK(sp[0].x1 == 49 && sp[1].x0 == 52 && sp[1].x1 == 119 && sp[2].x0 == 122);
  sp = separate(seg, 0);                          // no gap: as drawn
  CHECK(sp[0].x1 == 50 && sp[1].x0 == 50 && sp[2].x0 == 120);
  sp = separate(seg, -4);
  CHECK(sp[0].x1 == 50 && sp[1].x0 == 50);
  CHECK(separate(std::vector<Segment>(), 2).empty());
  sp = separate(touching({{0, 150}}), 2);          // one band: nothing to separate
  CHECK(sp[0].x0 == 0 && sp[0].x1 == 150);
}

TEST(separate_leaves_gaps_of_the_program_alone) {
  const auto seg = touching({{0, 40}, {60, 100}, {100, 150}});   // 40-60 is a gap between bands
  auto sp = separate(seg, 2);
  CHECK(sp[0].x1 == 40 && sp[1].x0 == 60);
  CHECK(sp[1].x1 == 99 && sp[2].x0 == 101);
}

TEST(separate_keeps_one_pixel_per_band) {
  // narrow bands: never less than one pixel, and the joint is as wide as the room allows
  auto sp = separate(touching({{0, 1}, {1, 2}, {2, 3}}), 2);
  for (auto &x : sp) CHECK(x.x1 - x.x0 == 1);
  CHECK(sp[0].x0 == 0 && sp[1].x0 == 1 && sp[2].x1 == 3);
  sp = separate(touching({{0, 1}, {1, 20}}), 2);   // the narrow side has no room: the wide side gives both pixels
  CHECK(sp[0].x0 == 0 && sp[0].x1 == 1 && sp[1].x0 == 3 && sp[1].x1 == 20);
  sp = separate(touching({{0, 20}, {20, 21}}), 2);
  CHECK(sp[0].x1 == 18 && sp[1].x0 == 20 && sp[1].x1 == 21);
  sp = separate(touching({{0, 2}, {2, 4}}), 2);   // two pixels each: one pixel each is left
  CHECK(sp[0].x1 - sp[0].x0 == 1 && sp[1].x1 - sp[1].x0 == 1 && sp[1].x0 == 3 && sp[0].x1 == 1);
}

TEST(separate_invariants_on_every_bar) {
  for (int width : {20, 151, 302, 480, 1000}) {
    for (auto &c : js_cases()) {
      V s, e, m, t, a;
      for (auto &g : c.segs) {
        s.push_back(g.start);
        e.push_back(g.end);
        m.push_back(1);
        t.push_back(20 + (int) t.size());
        a.push_back(0);
      }
      if (s.empty() || (size_t) width < s.size()) continue;
      const auto segs = bar_segments(bands_of(s, e, m, t, a), width);
      for (int gap : {0, 1, 2, 3, 5}) {
        const auto sp = separate(segs, gap);
        CHECK(sp.size() == segs.size());
        for (size_t i = 0; i < sp.size() && i < segs.size(); ++i) {
          CHECK(sp[i].x1 - sp[i].x0 >= 1);                                   // a band is never lost
          CHECK(sp[i].x0 >= segs[i].x0 && sp[i].x1 <= segs[i].x1);           // only ever smaller
          if (i > 0) CHECK(sp[i].x0 >= sp[i - 1].x1);                        // never over a neighbour
          if (i > 0 && segs[i].x0 == segs[i - 1].x1) {
            const int room = (segs[i].x1 - segs[i].x0 - 1) + (segs[i - 1].x1 - segs[i - 1].x0 - 1);
            CHECK(sp[i].x0 - sp[i - 1].x1 >= std::min(gap, std::max(room, 0)));   // the joint is the gap, or what the bands could give
          }
        }
        CHECK(sp.empty() || (sp.front().x0 >= 0 && sp.back().x1 <= width));
      }
    }
  }
}

struct LevelCase {
  int width, level;
};
struct SiglaCase {
  const char *preset, *sigla;
};

TEST(label_level_parity_with_editor_segment_detail) {
  static const std::vector<LevelCase> levels = {
#include "labels.inc"
  };
  CHECK(levels.size() >= 12);
  for (auto &c : levels) {
    const LabelLevel got = label_level(c.width, 1);
    const int g = got == LabelLevel::NONE ? 0 : got == LabelLevel::SHORT ? 1 : 2;
    if (g != c.level) std::printf("  width %d: %d, editor %d\n", c.width, g, c.level);
    CHECK(g == c.level);
  }
  // a unit of 2 pixels doubles every threshold; a unit of 0 or below is 1
  CHECK(label_level(23, 2) == LabelLevel::NONE && label_level(24, 2) == LabelLevel::SHORT);
  CHECK(label_level(67, 2) == LabelLevel::SHORT && label_level(68, 2) == LabelLevel::VALUE);
  CHECK(label_level(12, 0) == LabelLevel::SHORT && label_level(11, -3) == LabelLevel::NONE);
}

TEST(preset_sigla_parity_with_editor_preset_sigla) {
  static const std::vector<SiglaCase> siglas = {
#include "siglas.inc"
  };
  CHECK(siglas.size() >= 10);
  for (auto &c : siglas) {
    CHECK_STR(preset_sigla(c.preset), c.sigla);
  }
  CHECK_STR(preset_sigla("\xC3\xA9" "co"), "\xC3\xA9" "C");             // a letter of another alphabet is taken whole (its case is not changed)
  CHECK_STR(preset_sigla("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"), "\xE6\x97\xA5\xE6\x9C\xAC");
  CHECK_STR(preset_sigla(std::string("a") + "\xC3"), "A\xC3");           // a cut character does not run past the text
}

TEST(segment_sigla_and_value) {
  CHECK_STR(segment_sigla(true, 21.5, "eco", 0.5, '.'), "21.5");
  CHECK_STR(segment_value(true, 21.5, "eco", 0.5, '.'), "21.5\xC2\xB0");
  CHECK_STR(segment_sigla(true, 21.5, "", 0.5, ','), "21,5");
  CHECK_STR(segment_value(true, 22, "", 1, '.'), "22\xC2\xB0");
  CHECK_STR(segment_sigla(false, 0, "away", 0.5, '.'), "AW");
  CHECK_STR(segment_value(false, 0, "away", 0.5, '.'), "away");
  CHECK_STR(segment_sigla(false, 0, "", 0.5, '.'), "");
  CHECK_STR(segment_value(false, 0, "", 0.5, '.'), "");
}

TEST(row_value_ladder) {
  auto w = [](const std::string &t) { return (int) t.size() * 8; };
  auto v = row_value("21.5\xC2\xB0", "away", 200, w);          // 11 bytes with the degree sign: "21.5° away" is 10 chars + the sign
  CHECK_STR(v.text, "21.5\xC2\xB0 away");
  CHECK(!v.cut);
  v = row_value("21.5\xC2\xB0", "away", 8 * 11, w);             // "21.5° away" = 11 bytes = 88: fits exactly
  CHECK_STR(v.text, "21.5\xC2\xB0 away");
  v = row_value("21.5\xC2\xB0", "away", 8 * 11 - 1, w);         // one pixel short: the temperature alone
  CHECK_STR(v.text, "21.5\xC2\xB0");
  CHECK(!v.cut);
  v = row_value("21.5\xC2\xB0", "", 200, w);
  CHECK_STR(v.text, "21.5\xC2\xB0");
  v = row_value("21.5\xC2\xB0", "away", 10, w);                 // not even the temperature: the caller cuts it
  CHECK_STR(v.text, "21.5\xC2\xB0");
  CHECK(v.cut);
  v = row_value("", "comfort", 7 * 8, w);
  CHECK_STR(v.text, "comfort");
  CHECK(!v.cut);
  v = row_value("", "comfort", 7 * 8 - 1, w);
  CHECK_STR(v.text, "comfort");
  CHECK(v.cut);
  v = row_value("", "", 100, w);
  CHECK_STR(v.text, "");
  CHECK(!v.cut);
  v = row_value("", "", 0, w);
  CHECK(!v.cut && v.text.empty());
}

// CIEDE2000 between two colours 0xRRGGBB (D65), as the editor's test (tests/frontend/contrast.test.mjs) measures the tints apart.
static void to_lab(uint32_t rgb, double &L, double &a, double &b) {
  double c[3];
  for (int i = 0; i < 3; ++i) {
    const double v = (double) ((rgb >> (16 - 8 * i)) & 0xFF) / 255.0;
    c[i] = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  }
  const double x = (0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2]) / 0.95047;
  const double y = 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2];
  const double z = (0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2]) / 1.08883;
  auto f = [](double t) { return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0; };
  L = 116.0 * f(y) - 16.0;
  a = 500.0 * (f(x) - f(y));
  b = 200.0 * (f(y) - f(z));
}
static double delta_e2000(uint32_t c1, uint32_t c2) {
  const double pi = 3.14159265358979323846, rad = pi / 180.0;
  double L1, a1, b1, L2, a2, b2;
  to_lab(c1, L1, a1, b1);
  to_lab(c2, L2, a2, b2);
  const double Cb = (std::hypot(a1, b1) + std::hypot(a2, b2)) / 2.0;
  const double G = 0.5 * (1.0 - std::sqrt(std::pow(Cb, 7) / (std::pow(Cb, 7) + std::pow(25.0, 7))));
  const double a1p = (1 + G) * a1, a2p = (1 + G) * a2;
  const double C1 = std::hypot(a1p, b1), C2 = std::hypot(a2p, b2);
  auto hue = [&](double b, double a) { const double h = std::atan2(b, a) / rad; return (a == 0 && b == 0) ? 0.0 : (h < 0 ? h + 360 : h); };
  const double h1 = hue(b1, a1p), h2 = hue(b2, a2p);
  double dh = 0;
  if (C1 * C2 != 0) {
    dh = h2 - h1;
    if (dh > 180) dh -= 360;
    else if (dh < -180) dh += 360;
  }
  const double dL = L2 - L1, dC = C2 - C1, dH = 2 * std::sqrt(C1 * C2) * std::sin(dh * rad / 2);
  const double Lb = (L1 + L2) / 2, Cbp = (C1 + C2) / 2;
  double hb = h1 + h2;
  if (C1 * C2 != 0) hb = std::fabs(h1 - h2) <= 180 ? (h1 + h2) / 2 : (h1 + h2 < 360 ? (h1 + h2 + 360) / 2 : (h1 + h2 - 360) / 2);
  const double T = 1 - 0.17 * std::cos((hb - 30) * rad) + 0.24 * std::cos(2 * hb * rad) + 0.32 * std::cos((3 * hb + 6) * rad) - 0.20 * std::cos((4 * hb - 63) * rad);
  const double dth = 30 * std::exp(-std::pow((hb - 275) / 25, 2));
  const double Rc = 2 * std::sqrt(std::pow(Cbp, 7) / (std::pow(Cbp, 7) + std::pow(25.0, 7)));
  const double Sl = 1 + 0.015 * std::pow(Lb - 50, 2) / std::sqrt(20 + std::pow(Lb - 50, 2)), Sc = 1 + 0.045 * Cbp, Sh = 1 + 0.015 * Cbp * T;
  const double Rt = -std::sin(2 * dth * rad) * Rc;
  return std::sqrt(std::pow(dL / Sl, 2) + std::pow(dC / Sc, 2) + std::pow(dH / Sh, 2) + Rt * (dC / Sc) * (dH / Sh));
}

TEST(contrast_helpers) {
  CHECK(std::fabs(relative_luminance(0x000000) - 0.0) < 1e-9);
  CHECK(std::fabs(relative_luminance(0xFFFFFF) - 1.0) < 1e-9);
  CHECK(std::fabs(contrast_ratio(0xFFFFFF, 0x000000) - 21.0) < 1e-9);
  CHECK(std::fabs(contrast_ratio(0x000000, 0xFFFFFF) - 21.0) < 1e-9);   // either order
  CHECK(std::fabs(contrast_ratio(0x777777, 0x777777) - 1.0) < 1e-9);
  CHECK(contrast_ratio(0x767676, 0xFFFFFF) > 4.5 && contrast_ratio(0x777777, 0xFFFFFF) < 4.6);   // the known 4.5 grey
  // the weights of the channels (WCAG 2): pure red, green and blue, and two colours of the tile checked against known ratios
  CHECK(std::fabs(relative_luminance(0xFF0000) - 0.2126) < 1e-6 && std::fabs(relative_luminance(0x00FF00) - 0.7152) < 1e-6 &&
        std::fabs(relative_luminance(0x0000FF) - 0.0722) < 1e-6);
  CHECK(std::fabs(contrast_ratio(0xFFC107, 0x1B1B1B) - 10.5668) < 1e-3);
  CHECK(std::fabs(contrast_ratio(0x009FE3, 0xFFFFFF) - 2.9709) < 1e-3);
}

static double lab_hue(uint32_t rgb) {
  double L, a, b;
  to_lab(rgb, L, a, b);
  double h = std::atan2(b, a) * 180.0 / 3.14159265358979323846;
  return h < 0 ? h + 360.0 : h;
}

TEST(palette_matches_the_editor) {
  // tests/palette.inc comes from the editor's styles.js (tools/gen_parity.mjs): {look, mode, tint, fill, text}
  struct Row { int look, mode, tint; uint32_t fill, text; };
  static const Row rows[] = {
#include "palette.inc"
  };
  CHECK(sizeof(rows) / sizeof(rows[0]) == 24);
  const Mode modes[3] = {Mode::HEAT, Mode::COOL, Mode::OFF};
  for (const Row &r : rows) {
    const BandColors c = band_colors(modes[r.mode], r.tint, r.look == 1);
    if (c.fill != r.fill || c.text != r.text) std::printf("  look %d mode %d tint %d: %06X/%06X, the editor has %06X/%06X\n", r.look, r.mode, r.tint, (unsigned) c.fill, (unsigned) c.text, (unsigned) r.fill, (unsigned) r.text);
    CHECK(c.fill == r.fill && c.text == r.text);
  }
}

TEST(band_colors_clamp_the_tint_and_the_mode) {
  CHECK(band_colors(Mode::HEAT, -1, false).fill == band_colors(Mode::HEAT, 0, false).fill);
  CHECK(band_colors(Mode::COOL, TINT_COUNT + 7, true).fill == band_colors(Mode::COOL, TINT_COUNT - 1, true).fill);
  CHECK(band_colors(mode_from_code(7), 2, false).fill == band_colors(Mode::OFF, 2, false).fill);
}

TEST(band_text_is_readable_in_both_looks) {
  // 4.5 to 1 of the text on its own fill, every tint of every mode, light and dark
  for (int dark = 0; dark < 2; ++dark)
    for (Mode m : {Mode::HEAT, Mode::COOL, Mode::OFF})
      for (int t = 0; t < TINT_COUNT; ++t) {
        const BandColors c = band_colors(m, t, dark == 1);
        const double ratio = contrast_ratio(c.fill, c.text);
        if (ratio < 4.5) std::printf("  dark %d mode %d tint %d: %.2f\n", dark, (int) m, t, ratio);
        CHECK(ratio >= 4.5);
      }
}

TEST(tints_of_one_mode_differ_in_both_looks) {
  for (int dark = 0; dark < 2; ++dark)
    for (Mode m : {Mode::HEAT, Mode::COOL, Mode::OFF})
      for (int a = 0; a < TINT_COUNT; ++a)
        for (int b = a + 1; b < TINT_COUNT; ++b) {
          const double d = delta_e2000(band_colors(m, a, dark == 1).fill, band_colors(m, b, dark == 1).fill);
          if (d < 15) std::printf("  dark %d mode %d tints %d/%d: %.1f\n", dark, (int) m, a, b, d);
          CHECK(d >= 15);   // the editor's own bar (CIEDE2000 >= 15 between any two fills of a mode)
        }
}

TEST(no_tint_of_a_mode_is_taken_for_another_mode) {
  // Every fill of the palette is at least 15 (CIEDE2000) from every fill of another mode, in both looks: no exception for tint 0 any more.
  for (int dark = 0; dark < 2; ++dark)
    for (int a = 0; a < TINT_COUNT; ++a)
      for (int b = 0; b < TINT_COUNT; ++b) {
        const uint32_t heat = band_colors(Mode::HEAT, a, dark == 1).fill, cool = band_colors(Mode::COOL, b, dark == 1).fill, off = band_colors(Mode::OFF, b, dark == 1).fill;
        const uint32_t coola = band_colors(Mode::COOL, a, dark == 1).fill;
        const double d[3] = {delta_e2000(heat, cool), delta_e2000(heat, off), delta_e2000(coola, off)};
        for (double v : d) {
          if (v < 15) std::printf("  dark %d tints %d/%d: %.1f\n", dark, a, b, v);
          CHECK(v >= 15);
        }
      }
}

TEST(heat_and_cool_hues_stay_in_their_family) {
  // heat: amber, orange, red, purple; cool: azure, deep blue, sky blue, aqua (hue angle of CIELAB, as the editor's contrast test does)
  struct Range { double lo, hi; };
  const Range heat[TINT_COUNT] = {{55, 95}, {40, 65}, {20, 45}, {300, 340}};
  const Range cool[TINT_COUNT] = {{255, 290}, {285, 305}, {215, 245}, {175, 205}};
  for (int dark = 0; dark < 2; ++dark)
    for (int t = 0; t < TINT_COUNT; ++t) {
      const double h = lab_hue(band_colors(Mode::HEAT, t, dark == 1).fill), c = lab_hue(band_colors(Mode::COOL, t, dark == 1).fill);
      if (h < heat[t].lo || h > heat[t].hi) std::printf("  dark %d heat tint %d: hue %.0f\n", dark, t, h);
      if (c < cool[t].lo || c > cool[t].hi) std::printf("  dark %d cool tint %d: hue %.0f\n", dark, t, c);
      CHECK(h >= heat[t].lo && h <= heat[t].hi);
      CHECK(c >= cool[t].lo && c <= cool[t].hi);
    }
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
