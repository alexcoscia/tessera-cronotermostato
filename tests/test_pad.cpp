// Tests of components/cronotermostato/cronotermostato_pad.h: the geometry of the edit panel, the zones of a tap and the
// keys of the texts. Build and run: tests/run.sh
#include "../components/cronotermostato/cronotermostato_pad.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
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
#define TEST(name)                                       \
  static void name();                                    \
  static struct Reg_##name {                             \
    Reg_##name() { tests().push_back({#name, name}); }   \
  } reg_##name;                                          \
  static void name()

struct Entry {
  const char *name;
  void (*fn)();
};
static std::vector<Entry> &tests() {
  static std::vector<Entry> t;
  return t;
}

// ------------------------------------------------------------------------------------------------ the boards

// The rooms of the card as the bench measured them (docs/SPIKE_MODIFICA.md, 7 mm in pixels) and a plausible line.
struct Board {
  const char *name;
  int w, h, mm7, line, gap;
  int rows_with_header;   // rows per page when the header fits
  bool header;
};
static const Board BOARDS[] = {
    {"cyd", 300, 176, 40, 17, 3, 3, false},
    {"hosyond", 460, 255, 41, 20, 3, 4, true},
    {"guition4", 440, 372, 48, 22, 3, 5, true},
    {"waveshare7b", 700, 492, 48, 24, 3, 8, true},
    {"guition10", 615, 704, 42, 24, 3, 13, true},
};

static PadInput input_of(const Board &b) {
  PadInput in;
  in.w = b.w;
  in.h = b.h;
  in.key_h = std::max(b.mm7, b.line + 6);
  in.key_min_w = b.mm7;
  in.gap = b.gap;
  in.line_h = b.line;
  in.foot_w = b.mm7 * 9 / 7;
  in.time_w = b.line * 3;
  in.cap_w = b.line * 5;
  in.mode_w = b.line * 4;
  in.open_w = b.line * 5;
  in.key_max_w = b.mm7 * 26 / 7;
  in.key_max_h = b.mm7 * 12 / 7;
  in.wide_gap = b.mm7 * 8 / 7;
  return in;
}

// Every rectangle that is a key in a view (what a tap can hit), with its zone.
struct Keyed {
  Rect r;
  EZone z;
};
static std::vector<Keyed> keys_of(const PadLayout &L, EView v, int shown) {
  std::vector<Keyed> k;
  auto add = [&](const Rect &r, EZ kind, int idx = -1) { k.push_back({r, {kind, idx}}); };
  if (v == EView::READ) {
    add(L.open, EZ::OPEN);
  } else if (v == EView::LIST) {
    for (int i = 0; i < std::min(shown, L.rows); ++i) add(L.row[i], EZ::ROW, i);
    add(L.prev, EZ::PREV);
    add(L.next, EZ::NEXT);
    add(L.add, EZ::ADD);
  } else if (v == EView::CONFIRM) {
    add(L.conf_no, EZ::CONFIRM_NO);
    add(L.conf_yes, EZ::CONFIRM_YES);
  } else {
    static const EZ s[4] = {EZ::S_M60, EZ::S_M15, EZ::S_P15, EZ::S_P60};
    static const EZ e[4] = {EZ::E_M60, EZ::E_M15, EZ::E_P15, EZ::E_P60};
    for (int i = 0; i < 4; ++i) {
      add(L.s_key[i], s[i]);
      add(L.e_key[i], e[i]);
    }
    add(L.mode_key, EZ::MODE);
    add(L.temp_m, EZ::TEMP_M);
    add(L.temp_p, EZ::TEMP_P);
    add(L.del, EZ::DELETE);
    add(L.save, EZ::SAVE);
    add(L.back, EZ::BACK);
  }
  return k;
}

static bool inside_room(const PadLayout &L, const Rect &r) { return r.x >= 0 && r.y >= 0 && r.right() <= L.in.w && r.bottom() <= L.in.h; }

// For every pixel of the room: at most one key claims it (the key grown by half the gap), and pad_zone_at says that key.
static void check_partition(const PadLayout &L, EView v, int shown) {
  const std::vector<Keyed> keys = keys_of(L, v, shown);
  const int g = L.gap / 2;
  for (int y = -2; y < L.in.h + 2; ++y) {
    for (int x = -2; x < L.in.w + 2; ++x) {
      int hits = 0;
      EZone want;
      for (const Keyed &k : keys)
        if (k.r.has(x, y, g)) {
          ++hits;
          want = k.z;
        }
      const EZone got = pad_zone_at(L, v, shown, x, y);
      const bool in_room = x >= 0 && y >= 0 && x < L.in.w && y < L.in.h;
      if (hits > 1) {
        CHECK(!"two keys claim the same pixel");
        return;
      }
      if (!in_room) {
        if (got.kind != EZ::NONE) {
          CHECK(!"a tap outside the room hits a key");
          return;
        }
        continue;
      }
      if (hits == 0 ? got.kind != EZ::NONE : !(got == want)) {
        std::printf("   at %d,%d: hits=%d got=%d/%d\n", x, y, hits, (int) got.kind, got.index);
        CHECK(!"pad_zone_at disagrees with the rectangles");
        return;
      }
    }
  }
  CHECK(true);
}

// ------------------------------------------------------------------------------------------------ geometry

TEST(rows_per_page_follow_the_height) {
  for (const Board &b : BOARDS) {
    const PadLayout L = make_pad(input_of(b));
    CHECK(L.list_header == b.header);
    CHECK(L.rows >= (int) EDIT_MIN_ROWS && L.rows <= (int) EDIT_MAX_ROWS);
    if (b.header) CHECK(L.rows == std::min(b.rows_with_header, (int) EDIT_MAX_ROWS));
    else CHECK(L.rows == 3);
    CHECK(!L.list_small);
    CHECK(L.row_h >= b.mm7);   // 7 mm
    // the footer is below the last row and inside the room
    CHECK(L.prev.y >= L.row[L.rows - 1].bottom() + L.gap);
    CHECK(L.prev.bottom() <= b.h);
  }
}

TEST(rows_known_values_of_the_spike) {
  // CYD 3 (no header: it would leave 2), Hosyond 4, Guition 5, 7B 8, 10.1" 13, each with a thin line for the title (the spike
  // counted a header as high as a key: CYD 2, Hosyond 3, Guition 5, 7B 7, 10.1" 13).
  const int want[] = {3, 4, 5, 8, 13};
  for (size_t i = 0; i < 5; ++i) CHECK(make_pad(input_of(BOARDS[i])).rows == want[i]);
}

TEST(the_header_line_is_kept_while_three_rows_still_fit) {
  Board b = {"edge", 460, 202, 41, 20, 3, 3, true};   // 202 - (20+6+3) - 41 = 132 = 3 rows of 44 exactly
  PadLayout L = make_pad(input_of(b));
  CHECK(L.list_header && L.rows == 3);
  b.h = 201;   // one pixel less: two rows with a header, so the header goes and three rows stay
  L = make_pad(input_of(b));
  CHECK(!L.list_header && L.rows == 3);
}

TEST(everything_inside_the_room) {
  for (const Board &b : BOARDS) {
    const PadLayout L = make_pad(input_of(b));
    for (EView v : {EView::READ, EView::LIST, EView::DETAIL, EView::CONFIRM})
      for (const Keyed &k : keys_of(L, v, L.rows)) {
        CHECK(inside_room(L, k.r));
        CHECK(!k.r.empty());
      }
    CHECK(inside_room(L, L.info));
    CHECK(inside_room(L, L.temp_val));
    CHECK(inside_room(L, L.s_cap));
    CHECK(inside_room(L, L.e_cap));
    if (L.list_header) CHECK(inside_room(L, L.list_head));
    if (L.detail_header) CHECK(inside_room(L, L.detail_head));
  }
}

TEST(keys_are_seven_millimetres_where_the_glass_has_them) {
  for (const Board &b : BOARDS) {
    const PadLayout L = make_pad(input_of(b));
    CHECK(!L.detail_small);
    CHECK(L.detail_fits);
    CHECK(L.detail_key_h >= b.mm7);
    CHECK(L.detail_key_h <= input_of(b).key_max_h);   // never taller than 12 mm: the room left over stays empty
    for (EView v : {EView::READ, EView::LIST, EView::DETAIL, EView::CONFIRM})
      for (const Keyed &k : keys_of(L, v, L.rows)) {
        CHECK(k.r.h >= b.mm7);
        CHECK(k.r.w >= b.mm7);
      }
  }
}

TEST(the_cyd_detail_has_no_status_line_the_others_do) {
  CHECK(!make_pad(input_of(BOARDS[0])).detail_header);
  for (int i = 1; i < 5; ++i) CHECK(make_pad(input_of(BOARDS[i])).detail_header);
  // the detail of the CYD fills the room: 4 keys and 3 gaps
  const PadLayout L = make_pad(input_of(BOARDS[0]));
  CHECK(L.detail_bottom <= 176);
  CHECK(L.detail_bottom >= 4 * 40 + 3 * 3);
}

TEST(footer_and_info_of_the_list) {
  for (const Board &b : BOARDS) {
    const PadLayout L = make_pad(input_of(b));
    CHECK(L.prev.right() + L.gap <= L.next.x);
    CHECK(L.next.right() + L.gap <= L.info.x);
    CHECK(L.info.right() + L.gap <= L.add.x);
    CHECK(L.add.right() == b.w);
    CHECK(L.info.w > 0);
  }
  // the CYD carries the title and the message in the footer's middle: it must be wide enough for a few words
  CHECK(make_pad(input_of(BOARDS[0])).info.w >= 100);
}

TEST(a_short_glass_makes_the_keys_smaller_and_says_so) {
  Board b = {"tiny", 240, 110, 40, 14, 2, 3, false};
  const PadLayout L = make_pad(input_of(b));
  CHECK(L.list_small);
  CHECK(L.rows == 3);
  CHECK(L.row_h < 40);
  CHECK(L.prev.bottom() <= 110);
  CHECK(L.detail_small);
  CHECK(L.detail_fits);
  check_partition(L, EView::LIST, 3);
  check_partition(L, EView::DETAIL, 0);
}

TEST(captions_only_when_the_keys_keep_seven_millimetres) {
  // CYD: 300 wide, caption "Start 88:88" 85 px: 4 keys of 40 + 4 gaps leave 128 for it
  PadInput in = input_of(BOARDS[0]);
  CHECK(make_pad(in).captions);
  in.cap_w = 200;   // a language with a long word: no room for it, the time alone
  const PadLayout L = make_pad(in);
  CHECK(!L.captions);
  CHECK(L.s_key[0].w >= 40);
  in.cap_w = 0;
  CHECK(!make_pad(in).captions);
}

TEST(the_mode_key_and_the_value_never_overlap_the_temperature_keys) {
  for (const Board &b : BOARDS) {
    const PadLayout L = make_pad(input_of(b));
    CHECK(L.mode_key.right() + L.gap <= L.temp_val.x);
    CHECK(L.temp_val.right() + L.gap <= L.temp_m.x);
    CHECK(L.temp_m.right() + L.gap <= L.temp_p.x);
    CHECK(L.temp_val.w >= input_of(b).time_w);
    CHECK(L.s_cap.right() + L.gap <= L.s_key[0].x);
    CHECK(L.s_key[3].right() <= b.w);
  }
}

// ------------------------------------------------------------------------------------------------ zones

TEST(a_long_mode_word_leaves_the_value_its_room) {
  PadInput in = input_of(BOARDS[0]);
  in.mode_w = 250;   // a language with a long word for a mode
  const PadLayout L = make_pad(in);
  CHECK(L.mode_key.right() + L.gap <= L.temp_val.x);
  CHECK(L.temp_val.w >= in.time_w);
  CHECK(L.temp_val.right() + L.gap <= L.temp_m.x);
  check_partition(L, EView::DETAIL, 0);
}

TEST(zones_partition_every_view_on_every_board) {
  for (const Board &b : BOARDS) {
    const PadLayout L = make_pad(input_of(b));
    check_partition(L, EView::READ, 0);
    check_partition(L, EView::LIST, L.rows);
    check_partition(L, EView::LIST, 1);   // a page with one row: the others are empty
    check_partition(L, EView::LIST, 0);   // no bands
    check_partition(L, EView::DETAIL, 0);
    check_partition(L, EView::CONFIRM, 0);
  }
}

TEST(the_centre_of_every_key_hits_that_key) {
  for (const Board &b : BOARDS) {
    const PadLayout L = make_pad(input_of(b));
    for (EView v : {EView::READ, EView::LIST, EView::DETAIL, EView::CONFIRM})
      for (const Keyed &k : keys_of(L, v, L.rows)) {
        const EZone z = pad_zone_at(L, v, L.rows, k.r.x + k.r.w / 2, k.r.y + k.r.h / 2);
        CHECK(z == k.z);
      }
  }
}

TEST(corners_of_a_key_hit_that_key) {
  const PadLayout L = make_pad(input_of(BOARDS[1]));
  for (const Keyed &k : keys_of(L, EView::DETAIL, 0)) {
    CHECK(pad_zone_at(L, EView::DETAIL, 0, k.r.x, k.r.y) == k.z);
    CHECK(pad_zone_at(L, EView::DETAIL, 0, k.r.right() - 1, k.r.bottom() - 1) == k.z);
  }
}

TEST(a_finger_between_two_keys_hits_one_of_them) {
  const PadLayout L = make_pad(input_of(BOARDS[1]));   // gap 3: 1 px on each side + 1 dead
  const Rect &a = L.s_key[0], &b = L.s_key[1];
  const int between = a.right();   // first pixel of the gap
  const EZone left = pad_zone_at(L, EView::DETAIL, 0, between, a.y + 5);
  CHECK(left.kind == EZ::S_M60);
  const EZone right = pad_zone_at(L, EView::DETAIL, 0, b.x - 1, b.y + 5);
  CHECK(right.kind == EZ::S_M15);
}

TEST(hidden_rows_take_no_tap) {
  const PadLayout L = make_pad(input_of(BOARDS[2]));   // 5 rows
  const Rect &r2 = L.row[2];
  CHECK(pad_zone_at(L, EView::LIST, 5, r2.x + 10, r2.y + 10) == (EZone{EZ::ROW, 2}));
  CHECK(pad_zone_at(L, EView::LIST, 2, r2.x + 10, r2.y + 10).kind == EZ::NONE);
  CHECK(pad_zone_at(L, EView::LIST, 0, L.row[0].x + 10, L.row[0].y + 10).kind == EZ::NONE);
  CHECK(pad_zone_at(L, EView::LIST, -3, L.row[0].x + 10, L.row[0].y + 10).kind == EZ::NONE);
  CHECK(pad_zone_at(L, EView::LIST, 99, r2.x + 10, r2.y + 10) == (EZone{EZ::ROW, 2}));   // never more rows than the page has
}

TEST(a_tap_in_one_view_never_hits_a_key_of_another) {
  const PadLayout L = make_pad(input_of(BOARDS[1]));
  // the key Edit of the READ view is not a zone of the list or the detail
  const Rect &o = L.open;
  CHECK(pad_zone_at(L, EView::READ, 0, o.x + 2, o.y + 2).kind == EZ::OPEN);
  CHECK(pad_zone_at(L, EView::LIST, 3, o.x + 2, o.y + 2).kind != EZ::OPEN);
  CHECK(pad_zone_at(L, EView::DETAIL, 0, o.x + 2, o.y + 2).kind != EZ::OPEN);
  // the footer of the list is not in the detail, and Save of the detail is not in the list
  CHECK(pad_zone_at(L, EView::DETAIL, 0, L.save.x + 2, L.save.y + 2).kind == EZ::SAVE);
  CHECK(pad_zone_at(L, EView::LIST, 3, L.add.x + 2, L.add.y + 2).kind == EZ::ADD);
  CHECK(pad_zone_at(L, EView::READ, 0, L.add.x + 2, L.add.y + 2).kind == EZ::NONE);
}

TEST(outside_the_room_and_unknown_point_do_nothing) {
  const PadLayout L = make_pad(input_of(BOARDS[0]));
  CHECK(pad_zone_at(L, EView::DETAIL, 0, -1, -1).kind == EZ::NONE);   // tap_point() when the screen cannot say
  CHECK(pad_zone_at(L, EView::LIST, 3, -1, 10).kind == EZ::NONE);
  CHECK(pad_zone_at(L, EView::LIST, 3, 10, -1).kind == EZ::NONE);
  CHECK(pad_zone_at(L, EView::LIST, 3, 300, 10).kind == EZ::NONE);
  CHECK(pad_zone_at(L, EView::LIST, 3, 10, 176).kind == EZ::NONE);
  CHECK(pad_zone_at(L, EView::DETAIL, 0, 100000, 100000).kind == EZ::NONE);
}

TEST(the_key_edit_stands_at_the_top_right_of_the_read_view) {
  for (const Board &b : BOARDS) {
    const PadLayout L = make_pad(input_of(b));
    CHECK(L.open.right() == b.w);
    CHECK(L.open.y == 0);
    CHECK(L.open.h >= b.mm7);
    CHECK(L.read_head.right() + L.gap <= L.open.x);
    CHECK(L.read_top >= L.open.bottom() + L.gap);
  }
}

TEST(a_grid_of_geometries_keeps_the_invariants) {
  for (int w = 160; w <= 1300; w += 97)
    for (int h = 90; h <= 800; h += 53)
      for (int gap = 2; gap <= 5; ++gap) {
        PadInput in;
        in.w = w;
        in.h = h;
        in.gap = gap;
        in.line_h = 12 + w / 90;
        in.key_h = std::max(30 + w / 40, in.line_h + 6);
        in.key_min_w = 30 + w / 40;
        in.foot_w = in.key_min_w * 9 / 7;
        in.time_w = in.line_h * 3;
        in.cap_w = in.line_h * 5;
        in.mode_w = in.line_h * 4;
        in.open_w = in.line_h * 5;
        const PadLayout L = make_pad(in);
        CHECK(L.rows >= 3 && L.rows <= 14);
        for (EView v : {EView::READ, EView::LIST, EView::DETAIL}) {
          const auto keys = keys_of(L, v, L.rows);
          for (const Keyed &k : keys)
            if (!inside_room(L, k.r)) {
              std::printf("   w=%d h=%d gap=%d view=%d zone=%d\n", w, h, gap, (int) v, (int) k.z.kind);
              CHECK(!"a key outside the room");
            }
          for (size_t i = 0; i < keys.size(); ++i)
            for (size_t j = i + 1; j < keys.size(); ++j) {
              const Rect &a = keys[i].r, &b = keys[j].r;
              const bool apart = a.right() + gap <= b.x || b.right() + gap <= a.x || a.bottom() + gap <= b.y || b.bottom() + gap <= a.y;
              if (!apart) {
                std::printf("   w=%d h=%d gap=%d view=%d keys %zu/%zu too close\n", w, h, gap, (int) v, i, j);
                CHECK(!"two keys closer than a gap");
              }
            }
        }
      }
}

TEST(the_partition_holds_on_odd_gaps_and_odd_sizes) {
  Board b = {"odd", 301, 177, 41, 17, 5, 3, false};
  const PadLayout L = make_pad(input_of(b));
  check_partition(L, EView::LIST, L.rows);
  check_partition(L, EView::DETAIL, 0);
  Board c = {"odd2", 463, 259, 39, 19, 2, 3, true};
  const PadLayout M = make_pad(input_of(c));
  check_partition(M, EView::LIST, M.rows);
  check_partition(M, EView::DETAIL, 0);
  check_partition(M, EView::READ, 0);
}

// ------------------------------------------------------------------------------------------------ the keys of the detail

TEST(time_keys_move_by_the_steps_of_the_plan) {
  KeyStep k;
  CHECK(time_key(EZ::S_M60, k) && k.start && k.delta == -60);
  CHECK(time_key(EZ::S_M15, k) && k.start && k.delta == -15);
  CHECK(time_key(EZ::S_P15, k) && k.start && k.delta == 15);
  CHECK(time_key(EZ::S_P60, k) && k.start && k.delta == 60);
  CHECK(time_key(EZ::E_M60, k) && !k.start && k.delta == -60);
  CHECK(time_key(EZ::E_M15, k) && !k.start && k.delta == -15);
  CHECK(time_key(EZ::E_P15, k) && !k.start && k.delta == 15);
  CHECK(time_key(EZ::E_P60, k) && !k.start && k.delta == 60);
  CHECK(!time_key(EZ::MODE, k));
  CHECK(!time_key(EZ::SAVE, k));
  CHECK(!time_key(EZ::NONE, k));
  CHECK(!time_key(EZ::ROW, k));
}


// ------------------------------------------------------------------------------------------------ the confirmation of Delete

static const Board CYD320 = {"cyd320", 320, 240, 40, 17, 3, 5, true};   // the 320x240 glass at its own 7 mm

static std::vector<Board> confirm_boards() {
  std::vector<Board> v(std::begin(BOARDS), std::end(BOARDS));
  v.push_back(CYD320);
  return v;
}

TEST(confirm_keys_are_big_apart_and_inside_the_room) {
  for (const Board &b : confirm_boards()) {
    const PadInput in = input_of(b);
    const PadLayout L = make_pad(in);
    CHECK(!L.conf_no.empty() && !L.conf_yes.empty());
    CHECK(inside_room(L, L.conf_no) && inside_room(L, L.conf_yes));
    CHECK(L.conf_key_h >= b.mm7);                   // 7 mm like the other keys, on every board of the bench
    CHECK(L.conf_no.h == L.conf_key_h && L.conf_yes.h == L.conf_key_h);
    CHECK(L.conf_no.w >= b.mm7 && L.conf_yes.w >= b.mm7);
    CHECK(L.conf_no.x == 0 && L.conf_yes.right() == b.w);        // one at the left edge, one at the right
    CHECK(L.conf_no.right() < L.conf_yes.x);
    CHECK(L.conf_yes.x - L.conf_no.right() == L.conf_gap);        // the gap between them is the wide one
    CHECK(L.conf_gap >= in.wide_gap || L.conf_gap == b.w / 3);
    CHECK(L.conf_gap >= 4 * L.gap);
    CHECK(!L.conf_small);
    CHECK(L.conf_no.w == L.conf_yes.w);
  }
}

TEST(confirm_gap_defaults_to_four_gaps_and_never_exceeds_a_third) {
  PadInput in = input_of(BOARDS[1]);
  in.wide_gap = 0;
  for (int gap : {1, 2, 3, 6}) {
    in.gap = gap;
    const PadLayout L = make_pad(in);
    CHECK(L.conf_gap >= 4 * gap && L.conf_gap <= 4 * gap + 1);
  }
  in.gap = 3;
  in.wide_gap = 100000;   // absurd: a third of the width at most
  const PadLayout T = make_pad(in);
  CHECK(T.conf_gap <= in.w / 3 + 1);
  CHECK(T.conf_no.w >= 1 && T.conf_yes.w >= 1);
  in.w = 12;              // so narrow that nothing fits: still no overlap, no negative size
  in.wide_gap = 0;
  const PadLayout N = make_pad(in);
  CHECK(N.conf_no.w >= 1 && N.conf_yes.w >= 1 && N.conf_no.right() <= N.conf_yes.x);
}

TEST(confirm_does_not_stand_where_delete_stood) {
  for (const Board &b : confirm_boards()) {
    const PadLayout L = make_pad(input_of(b));
    // Confirm is not under the finger that tapped Delete, even with the half gap the zones add
    const int g = L.gap / 2;
    CHECK(L.conf_yes.x > L.del.right() + g);
    CHECK(L.conf_yes.x > L.del.right() + L.gap);
    // the centre of Delete, tapped again in the confirmation, is not Confirm (Cancel is the safe answer)
    const EZone same = pad_zone_at(L, EView::CONFIRM, 0, L.del.x + L.del.w / 2, L.del.y + L.del.h / 2);
    CHECK(same.kind != EZ::CONFIRM_YES);
    // and no point of Delete's own zone is in Confirm's
    for (int y = L.del.y; y < L.del.bottom(); ++y)
      for (int x = L.del.x; x < L.del.right(); ++x) CHECK(pad_zone_at(L, EView::CONFIRM, 0, x, y).kind != EZ::CONFIRM_YES);
  }
}

TEST(confirm_zones_never_overlap_and_the_gap_is_dead) {
  for (const Board &b : confirm_boards()) {
    const PadLayout L = make_pad(input_of(b));
    const int y = L.conf_no.y + L.conf_no.h / 2;
    int cancel = 0, confirm = 0, none = 0;
    for (int x = 0; x < b.w; ++x) {
      const EZone z = pad_zone_at(L, EView::CONFIRM, 0, x, y);
      if (z.kind == EZ::CONFIRM_NO) ++cancel;
      else if (z.kind == EZ::CONFIRM_YES) ++confirm;
      else ++none;
    }
    CHECK(cancel > 0 && confirm > 0);
    CHECK(none >= L.conf_gap - L.gap);              // the middle of the gap is nobody's
    // the zones are in order: all of Cancel before the first pixel of Confirm
    int last_no = -1, first_yes = b.w;
    for (int x = 0; x < b.w; ++x) {
      const EZone z = pad_zone_at(L, EView::CONFIRM, 0, x, y);
      if (z.kind == EZ::CONFIRM_NO) last_no = x;
      if (z.kind == EZ::CONFIRM_YES) first_yes = std::min(first_yes, x);
    }
    CHECK(last_no < first_yes);
    CHECK(first_yes - last_no >= L.conf_gap - L.gap);
    // the midpoint between the keys is dead
    CHECK(pad_zone_at(L, EView::CONFIRM, 0, b.w / 2, y).kind == EZ::NONE);
  }
}

TEST(confirm_edges_of_the_keys) {
  const PadLayout L = make_pad(input_of(BOARDS[1]));
  const Rect &n = L.conf_no, &y = L.conf_yes;
  CHECK(pad_zone_at(L, EView::CONFIRM, 0, n.x, n.y).kind == EZ::CONFIRM_NO);
  CHECK(pad_zone_at(L, EView::CONFIRM, 0, n.right() - 1, n.bottom() - 1).kind == EZ::CONFIRM_NO);
  CHECK(pad_zone_at(L, EView::CONFIRM, 0, y.x, y.y).kind == EZ::CONFIRM_YES);
  CHECK(pad_zone_at(L, EView::CONFIRM, 0, y.right() - 1, y.bottom() - 1).kind == EZ::CONFIRM_YES);
  // the last row of pixels under the keys is the room's edge: still the key (h is the end of the room)
  CHECK(y.bottom() <= L.in.h);
  // above the keys and the text there is nothing to tap
  CHECK(pad_zone_at(L, EView::CONFIRM, 0, y.x + 3, y.y - L.gap - 2).kind == EZ::NONE);
  CHECK(pad_zone_at(L, EView::CONFIRM, 0, 5, 0).kind == EZ::NONE);
  // the keys of other views are not keys here
  CHECK(pad_zone_at(L, EView::CONFIRM, 0, L.save.x + 3, L.save.y + 3).kind == EZ::NONE || L.save.y + 3 >= y.y);
  CHECK(pad_zone_at(L, EView::CONFIRM, 0, -1, -1).kind == EZ::NONE);
  CHECK(pad_zone_at(L, EView::CONFIRM, 0, L.in.w, y.y + 2).kind == EZ::NONE);
  CHECK(pad_zone_at(L, EView::CONFIRM, 0, y.x + 2, L.in.h).kind == EZ::NONE);
}

TEST(confirm_text_lines_stand_over_the_keys) {
  for (const Board &b : confirm_boards()) {
    const PadLayout L = make_pad(input_of(b));
    CHECK(L.conf_lines >= 2);   // the band and one more line always have room
    CHECK(L.conf_lines <= 3);
    for (int i = 0; i < L.conf_lines; ++i) {
      const Rect &r = L.conf_line[i];
      CHECK(inside_room(L, r));
      CHECK(r.bottom() <= L.conf_no.y);                       // never over a key
      if (i > 0) CHECK(r.y >= L.conf_line[i - 1].bottom());   // never over each other
    }
  }
}

TEST(confirm_on_a_short_glass_makes_the_keys_smaller_and_keeps_two_lines) {
  PadInput in = input_of(BOARDS[0]);
  in.h = 70;
  const PadLayout L = make_pad(in);
  CHECK(L.conf_small);
  CHECK(L.conf_key_h < in.key_h);
  CHECK(L.conf_lines >= 2);
  CHECK(inside_room(L, L.conf_no) && inside_room(L, L.conf_yes));
  CHECK(L.conf_line[1].bottom() <= L.conf_no.y);
  // absurdly short: nothing falls outside, nothing is empty
  in.h = 20;
  const PadLayout S = make_pad(in);
  CHECK(!S.conf_no.empty() && inside_room(S, S.conf_no) && inside_room(S, S.conf_yes));
}

TEST(confirm_grid_of_geometries_keeps_the_invariants) {
  for (int w = 120; w <= 800; w += 37)
    for (int h = 60; h <= 520; h += 29)
      for (int gap : {1, 2, 3, 5}) {
        PadInput in = input_of(BOARDS[1]);
        in.w = w;
        in.h = h;
        in.gap = gap;
        in.wide_gap = (w % 3) * 20;
        const PadLayout L = make_pad(in);
        CHECK(!L.conf_no.empty() && !L.conf_yes.empty());
        CHECK(inside_room(L, L.conf_no) && inside_room(L, L.conf_yes));
        CHECK(L.conf_no.right() < L.conf_yes.x);
        // zones: no pixel in both, Cancel's before Confirm's
        const int y = L.conf_no.y + L.conf_no.h / 2;
        int last_no = -1, first_yes = w + 1;
        for (int x = 0; x < w; ++x) {
          const EZone z = pad_zone_at(L, EView::CONFIRM, 0, x, y);
          if (z.kind == EZ::CONFIRM_NO) last_no = x;
          if (z.kind == EZ::CONFIRM_YES) first_yes = std::min(first_yes, x);
        }
        CHECK(last_no < first_yes);
        if (L.conf_lines > 0) CHECK(L.conf_line[L.conf_lines - 1].bottom() <= L.conf_no.y);
      }
}

TEST(confirm_check_partition_on_odd_sizes) {
  for (int w : {101, 160, 333}) {
    PadInput in = input_of(BOARDS[2]);
    in.w = w;
    in.h = 150;
    in.gap = 5;   // odd gap: half of it rounds down
    check_partition(make_pad(in), EView::CONFIRM, 0);
  }
}

// ---- the guard and what a tap does

TEST(confirm_ignores_the_first_600_ms_but_cancel_works_at_once) {
  CHECK(CONFIRM_GUARD_MS == 600);
  CHECK(confirm_tap(EZ::CONFIRM_NO, 1000, 1000) == ConfirmTap::CANCEL);
  CHECK(confirm_tap(EZ::CONFIRM_NO, 1001, 1000) == ConfirmTap::CANCEL);
  CHECK(confirm_tap(EZ::CONFIRM_YES, 1000, 1000) == ConfirmTap::NONE);
  CHECK(confirm_tap(EZ::CONFIRM_YES, 1599, 1000) == ConfirmTap::NONE);
  CHECK(confirm_tap(EZ::CONFIRM_YES, 1600, 1000) == ConfirmTap::DELETE);
  CHECK(confirm_tap(EZ::CONFIRM_YES, 90000, 1000) == ConfirmTap::DELETE);
  // the clock of the chip wraps: 300 ms across the wrap is still too soon, 700 ms is not
  CHECK(confirm_tap(EZ::CONFIRM_YES, 299, 0xFFFFFFFFu - 0) == ConfirmTap::NONE);
  CHECK(confirm_tap(EZ::CONFIRM_YES, 0xFFFFFF00u + 500, 0xFFFFFF00u) == ConfirmTap::NONE);
  CHECK(confirm_tap(EZ::CONFIRM_YES, 699u - 1u, 0xFFFFFFFFu - 0) == ConfirmTap::DELETE);
  CHECK(confirm_tap(EZ::CONFIRM_YES, 0xFFFFFF00u + 700, 0xFFFFFF00u) == ConfirmTap::DELETE);
  // no other zone means anything here
  for (EZ z : {EZ::NONE, EZ::OPEN, EZ::ROW, EZ::DELETE, EZ::SAVE, EZ::BACK, EZ::ADD, EZ::MODE})
    CHECK(confirm_tap(z, 99999, 0) == ConfirmTap::NONE);
}

TEST(confirm_lines_have_a_priority) {
  // not shared: question + band
  ConfirmLines c = confirm_lines(3, false);
  CHECK(c.question && c.band && !c.warning);
  c = confirm_lines(2, false);
  CHECK(c.question && c.band && !c.warning);
  c = confirm_lines(1, false);
  CHECK(!c.question && c.band && !c.warning);
  c = confirm_lines(0, false);
  CHECK(!c.question && !c.band && !c.warning);
  // shared: question + band + warning; the warning goes before the question
  c = confirm_lines(3, true);
  CHECK(c.question && c.band && c.warning);
  c = confirm_lines(2, true);
  CHECK(!c.question && c.band && c.warning);
  c = confirm_lines(1, true);
  CHECK(!c.question && c.band && !c.warning);
  c = confirm_lines(-4, true);
  CHECK(!c.question && !c.band && !c.warning);
  c = confirm_lines(99, true);
  CHECK(c.question && c.band && c.warning);
  // a board that shows only the two lines still says "shared"
  for (const Board &b : confirm_boards()) {
    const PadLayout L = make_pad(input_of(b));
    CHECK(confirm_lines(L.conf_lines, true).warning);
    CHECK(confirm_lines(L.conf_lines, true).band);
  }
}

TEST(the_warning_is_for_programs_of_more_than_one_room) {
  CHECK(!confirm_shared(-1));
  CHECK(!confirm_shared(0));
  CHECK(!confirm_shared(1));
  CHECK(confirm_shared(2));
  CHECK(confirm_shared(3));
  CHECK(confirm_shared(40));
}

TEST(band_summary_joins_the_parts) {
  CHECK_STR(band_summary("08:30-10:30", "Heating", "21.5\xC2\xB0"), "08:30-10:30 Heating 21.5\xC2\xB0");
  CHECK_STR(band_summary("00:00-06:00", "Off", ""), "00:00-06:00 Off");
  CHECK_STR(band_summary("08:30-10:30", "", "21.5"), "08:30-10:30 21.5");
  CHECK_STR(band_summary("", "Heat", "20"), "Heat 20");
  CHECK_STR(band_summary("", "", ""), "");
}

TEST(the_words_of_the_confirmation_have_long_and_short_forms) {
  const ConfirmWords w = confirm_words();
  CHECK_STR(w.question.full, "confirm_q");
  CHECK_STR(w.question.shortly, "confirm_q_s");
  CHECK_STR(w.shared.full, "confirm_shared");
  CHECK_STR(w.shared.shortly, "confirm_shared_s");
  CHECK_STR(w.yes, "confirm_yes");
  CHECK_STR(w.no, "confirm_no");
  CHECK(std::strcmp(w.question.full, w.question.shortly) != 0);
  CHECK(std::strcmp(w.shared.full, w.shared.shortly) != 0);
}

// ---- may Delete ask, and when does the question close

static EditInput gate_input(const std::vector<std::vector<double>> &rows, const char *info = "mon;3;r1;") {
  EditInput in;
  for (const auto &r : rows) {   // start, end, mode, temp, flags
    in.lists.start.push_back(r[0]);
    in.lists.end.push_back(r[1]);
    in.lists.mode.push_back(r[2]);
    in.lists.temp.push_back(r[3]);
    in.flags.push_back(r[4]);
    in.preset.push_back("");
  }
  in.info = parse_edit_info(info);
  in.info.n = (int) rows.size();
  in.program_name = "Casa";
  return in;
}

TEST(delete_gate_says_yes_for_a_plain_band_and_why_not_otherwise) {
  EditModel m;
  m.load(gate_input({{0, 360, 1, 17, 0}, {360, 480, 1, 21, 0}, {480, 1440, 0, -100, 1}}));
  CHECK(!delete_gate(m).ok);                 // nothing selected: no, and nothing to say
  CHECK(!delete_gate(m).why.any());
  CHECK(m.select(1));
  DeleteGate g = delete_gate(m);
  CHECK(g.ok);
  CHECK(!g.why.any());
  CHECK(m.size() == 2);                      // the gate changes nothing
  CHECK(m.selected() == 1);
  m.set_frozen(true);                        // a save under way
  g = delete_gate(m);
  CHECK(!g.ok);
  CHECK_STR(g.why.full, "msg_edit_blocked");
  m.set_frozen(false);
  CHECK(delete_gate(m).ok);
  CHECK(m.select(5) == false);
  CHECK(delete_gate(m).ok);                  // the failed select left the old one
  m.deselect();
  CHECK(!delete_gate(m).ok);
}

TEST(delete_gate_refuses_a_locked_band_and_a_day_with_one) {
  EditModel m;
  m.load(gate_input({{0, 360, 1, 17, 2}, {360, 480, 1, 21, 0}, {480, 1440, 0, -100, 1}}));
  CHECK(m.select(0));
  DeleteGate g = delete_gate(m);
  CHECK(!g.ok);
  CHECK_STR(g.why.full, "msg_locked");       // this band is locked
  CHECK(m.select(1));
  g = delete_gate(m);
  CHECK(!g.ok);
  CHECK_STR(g.why.full, "block_locked");     // another band is locked: the day cannot be saved from here
}

TEST(delete_gate_refuses_damaged_data) {
  EditModel m;
  EditInput in = gate_input({{0, 360, 1, 17, 0}, {300, 480, 1, 21, 0}});   // overlapping: repaired and flagged
  m.load(in);
  CHECK(m.damaged());
  m.select(0);
  DeleteGate g = delete_gate(m);
  CHECK(!g.ok);
  CHECK_STR(g.why.full, "block_damaged");
  EditModel n;
  EditInput cut = gate_input({{0, 360, 1, 17, 0}});
  cut.info.n = 9;                            // the core cut the lists: never delete from a cut day
  n.load(cut);
  CHECK(n.damaged());
  n.select(0);
  CHECK(!delete_gate(n).ok);
}

TEST(only_silence_keeps_the_question_open) {
  CHECK(confirm_survives(EditEvent::NONE));
  for (EditEvent e : {EditEvent::RELOADED, EditEvent::CHANGED_ELSEWHERE, EditEvent::DAY_CHANGED, EditEvent::SAVED, EditEvent::SAVE_FAILED})
    CHECK(!confirm_survives(e));
}

TEST(the_session_events_that_close_the_question_are_the_ones_the_session_raises) {
  // The question is asked on the selected band of an open session; each kind of news from the sensor must give an event that
  // closes it, and a sensor that says the same thing must not.
  const EditInput base = gate_input({{0, 360, 1, 17, 0}, {360, 480, 1, 21, 0}, {480, 1440, 0, -100, 1}});
  auto opened = [&](EditSession &s) {
    s.open(base, SetpointRange(), 3, "climate.x");
    s.model().select(1);
  };
  {  // the same data again (the temperature moved): the question stays
    EditSession s;
    opened(s);
    CHECK(confirm_survives(s.on_state(base, 10)));
    CHECK(s.model().selected() == 1);
  }
  {  // another revision of the program: closes
    EditSession s;
    opened(s);
    EditInput n = gate_input({{0, 360, 1, 17, 0}, {360, 480, 1, 21, 0}, {480, 1440, 0, -100, 1}}, "mon;3;r2;");
    CHECK(!confirm_survives(s.on_state(n, 10)));
  }
  {  // the band under the question moved elsewhere: closes
    EditSession s;
    opened(s);
    EditInput n = gate_input({{0, 360, 1, 17, 0}, {360, 450, 1, 21, 0}, {450, 1440, 0, -100, 1}});
    CHECK(!confirm_survives(s.on_state(n, 10)));
  }
  {  // midnight: another day closes it
    EditSession s;
    opened(s);
    EditInput n = gate_input({{0, 360, 1, 17, 0}}, "tue;3;r1;");
    CHECK(!confirm_survives(s.on_state(n, 10)));
  }
  {  // unreadable edit_info: the session does not even answer (the panel is hidden by the card)
    EditSession s;
    opened(s);
    EditInput n = base;
    n.info = parse_edit_info("garbage");
    CHECK(confirm_survives(s.on_state(n, 10)));
    CHECK(!panel_allowed(true, TileView{}, n.info));
  }
}

// ------------------------------------------------------------------------------------------------ the texts

TEST(messages_have_a_long_and_a_short_key) {
  for (Msg m : {Msg::EDIT_BLOCKED, Msg::LOCKED, Msg::FULL, Msg::NO_SPACE}) {
    const TextKeys t = msg_text(m);
    CHECK(t.any());
    CHECK(std::strlen(t.shortly) > 0);
    CHECK(std::strcmp(t.full, t.shortly) != 0);
  }
  CHECK(!msg_text(Msg::NONE).any());
  CHECK_STR(msg_text(Msg::LOCKED).full, "msg_locked");
  CHECK_STR(msg_text(Msg::FULL).shortly, "msg_full_s");
}

TEST(notices_and_blocks) {
  CHECK_STR(notice_text(Notice::CHANGED_ELSEWHERE).full, "notice_elsewhere");
  CHECK_STR(notice_text(Notice::DAY_CHANGED).full, "notice_day");
  CHECK(!notice_text(Notice::NONE).any());
  for (SaveBlock b : {SaveBlock::LOCKED, SaveBlock::DAMAGED, SaveBlock::NOT_DIRTY, SaveBlock::TOO_LONG, SaveBlock::INVALID}) {
    const TextKeys t = block_text(b);
    CHECK(t.any());
    CHECK(std::strlen(t.shortly) > 0);
  }
  CHECK(!block_text(SaveBlock::NONE).any());
  CHECK_STR(block_text(SaveBlock::LOCKED).full, "block_locked");
  CHECK_STR(block_text(SaveBlock::NOT_DIRTY).full, "block_not_dirty");
}

TEST(every_code_of_the_integration_has_its_words) {
  CHECK_STR(error_text("stale_day").full, "err_stale_day");
  CHECK_STR(error_text("conflict").full, "err_conflict");
  CHECK_STR(error_text("day_locked").full, "err_day_locked");
  CHECK_STR(error_text("invalid_bands").full, "err_invalid_bands");
  CHECK_STR(error_text("temp_range").full, "err_temp_range");
  CHECK_STR(error_text("temperature_out_of_range").full, "err_temp_range");
  CHECK_STR(error_text("invalid_program").full, "err_invalid_program");
  CHECK_STR(error_text("room_not_ready").full, "err_room_not_ready");
  CHECK_STR(error_text("not_sent").full, "err_not_sent");
  CHECK_STR(error_text("something_new").full, "err_unknown");
  CHECK_STR(error_text("").full, "err_unknown");
  CHECK_STR(error_text("unknown").full, "err_unknown");
}

TEST(save_key_says_the_state) {
  CHECK_STR(save_key(SaveState::IDLE), "save");
  CHECK_STR(save_key(SaveState::SAVING), "saving");
  CHECK_STR(save_key(SaveState::SAVED), "saved");
  CHECK_STR(save_key(SaveState::ERROR), "error");
  CHECK_STR(save_key(SaveState::TIMEOUT), "not_saved");
}

TEST(the_status_line_has_an_order) {
  const TextKeys word = msg_text(Msg::FULL);
  // a save in motion beats a word from a key and a notice
  CHECK_STR(status_of(SaveState::SAVING, Notice::CHANGED_ELSEWHERE, "", word).full, "msg_saving");
  CHECK_STR(status_of(SaveState::SAVED, Notice::NONE, "", word).full, "msg_saved");
  CHECK_STR(status_of(SaveState::ERROR, Notice::NONE, "stale_day", word).full, "err_stale_day");
  CHECK_STR(status_of(SaveState::ERROR, Notice::NONE, "not_sent", TextKeys()).full, "err_not_sent");
  CHECK_STR(status_of(SaveState::TIMEOUT, Notice::NONE, "", word).full, "err_timeout");
  // idle: the word from a key, then the notice, then nothing
  CHECK_STR(status_of(SaveState::IDLE, Notice::CHANGED_ELSEWHERE, "", word).full, "msg_full");
  CHECK_STR(status_of(SaveState::IDLE, Notice::CHANGED_ELSEWHERE, "", TextKeys()).full, "notice_elsewhere");
  CHECK_STR(status_of(SaveState::IDLE, Notice::DAY_CHANGED, "", TextKeys()).full, "notice_day");
  CHECK(!status_of(SaveState::IDLE, Notice::NONE, "", TextKeys()).any());
  CHECK(!status_of(SaveState::IDLE, Notice::NONE, "stale_day", TextKeys()).any());   // an old error is not shown when idle
}

TEST(page_numbers) {
  CHECK_STR(page_numbers(0, 3), "1/3");
  CHECK_STR(page_numbers(2, 3), "3/3");
  CHECK_STR(page_numbers(0, 1), "1/1");
}

TEST(the_session_and_the_pad_agree_on_the_rows) {
  // The card gives the session the rows of the layout: the model keeps them in 3..14 and pages by them.
  for (const Board &b : BOARDS) {
    const PadLayout L = make_pad(input_of(b));
    EditSession s;
    EditInput in;
    in.info.valid = true;
    in.info.kind = "mon";
    in.info.rooms = 2;
    in.info.revision = "r1";
    in.info.n = 0;
    s.open(in, SetpointRange(), L.rows, "climate.x");
    CHECK(s.model().rows_per_page() == L.rows);
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
