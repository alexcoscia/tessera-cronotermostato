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
    for (EView v : {EView::READ, EView::LIST, EView::DETAIL})
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
    for (EView v : {EView::READ, EView::LIST, EView::DETAIL})
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
  }
}

TEST(the_centre_of_every_key_hits_that_key) {
  for (const Board &b : BOARDS) {
    const PadLayout L = make_pad(input_of(b));
    for (EView v : {EView::READ, EView::LIST, EView::DETAIL})
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
