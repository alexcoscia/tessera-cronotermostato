// Tests of components/cronotermostato/cronotermostato_edit.h. Build and run: tests/run.sh
#include "../components/cronotermostato/cronotermostato_edit.h"

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

// ------------------------------------------------------------------------------------------------ helpers

// One entry of the sensor lists.
struct S {
  double s, e, mode, temp;
  bool autom;
  std::string preset;
  double locked;
};
static S U(double s, double e, double mode, double temp, const std::string &preset = "", double locked = 0) {
  return {s, e, mode, temp, false, preset, locked};
}
static S A(int s, int e) { return {(double) s, (double) e, 0, -100, true, "", 0}; }

static EditInput input(const std::vector<S> &v, const char *info = "mon;3;r1;", const char *name = "Casa") {
  EditInput in;
  for (const S &x : v) {
    in.lists.start.push_back(x.s);
    in.lists.end.push_back(x.e);
    in.lists.mode.push_back(x.mode);
    in.lists.temp.push_back(x.temp);
    in.flags.push_back((x.autom ? 1 : 0) | (x.locked != 0 ? 2 : 0));
    in.preset.push_back(x.preset);
  }
  in.info = parse_edit_info(info);
  in.info.n = (int) v.size();
  in.program_name = name;
  return in;
}

static EditModel model_of(const std::vector<S> &v) {
  EditModel m;
  m.load(input(v));
  return m;
}

static std::string spec(const EditModel &m) {   // "0-360 h2000 | 360-480 c2150p=eco | ..."
  std::string out;
  for (const EditBand &b : m.bands()) {
    if (!out.empty()) out += " | ";
    out += std::to_string(b.start) + "-" + std::to_string(b.end) + " " + (b.mode == Mode::HEAT ? "h" : b.mode == Mode::COOL ? "c" : "o");
    if (b.has_temp) out += std::to_string(b.centi);
    if (!b.preset.empty()) out += "p=" + b.preset;
    if (b.locked) out += "L";
  }
  return out;
}

// A small reader of the grammar of set_day (the same rules as engine/bands_text.py), to test the round trip.
static bool decode(const std::string &text, std::vector<EditBand> &out) {
  out.clear();
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t semi = text.find(';', pos);
    if (semi == std::string::npos) semi = text.size();
    std::string seg = text.substr(pos, semi - pos);
    pos = semi + 1;
    if (seg.find_first_not_of(" \t") == std::string::npos) {
      if (semi >= text.size()) break;
      return false;
    }
    char buf[8][64];
    std::vector<std::string> tok;
    size_t i = 0;
    while (i < seg.size()) {
      while (i < seg.size() && seg[i] == ' ') ++i;
      size_t j = i;
      while (j < seg.size() && seg[j] != ' ') ++j;
      if (j > i) tok.push_back(seg.substr(i, j - i));
      i = j;
    }
    (void) buf;
    if (tok.size() < 2) return false;
    int h1, m1, h2, m2;
    if (std::sscanf(tok[0].c_str(), "%d:%d-%d:%d", &h1, &m1, &h2, &m2) != 4) return false;
    EditBand b;
    b.start = h1 * 60 + m1;
    b.end = h2 * 60 + m2;
    if (tok[1] == "heat") b.mode = Mode::HEAT;
    else if (tok[1] == "cool") b.mode = Mode::COOL;
    else if (tok[1] == "off") b.mode = Mode::OFF;
    else return false;
    for (size_t k = 2; k < tok.size(); ++k) {
      if (tok[k].rfind("preset=", 0) == 0) {
        b.preset = tok[k].substr(7);
      } else {
        b.has_temp = true;
        b.centi = (int) std::lround(std::atof(tok[k].c_str()) * 100.0);
      }
    }
    out.push_back(b);
  }
  return true;
}

static const double R = 0;   // unused marker

// ------------------------------------------------------------------------------------------------ parse_edit_info

TEST(edit_info_normal) {
  EditInfo i = parse_edit_info("mon;3;r12;");
  CHECK(i.valid);
  CHECK_STR(i.kind, "mon");
  CHECK(i.rooms == 3);
  CHECK_STR(i.revision, "r12");
  CHECK_STR(i.error, "");
  i = parse_edit_info("holiday;1;abc-1.2_x;stale_day");
  CHECK(i.valid);
  CHECK_STR(i.kind, "holiday");
  CHECK(i.rooms == 1);
  CHECK_STR(i.error, "stale_day");
  i = parse_edit_info("sun;0;0;conflict");
  CHECK(i.valid && i.rooms == 0 && i.revision == "0");
}

TEST(edit_info_hostile) {
  CHECK(!parse_edit_info("").valid);
  CHECK(!parse_edit_info(";;;").valid);               // empty kind, rooms, revision
  CHECK(!parse_edit_info("mon;3;r1").valid);          // missing the error field
  CHECK(parse_edit_info("mon;3;r1").kind.empty());    // a text that is not whole gives no kind, rooms, revision
  CHECK(parse_edit_info("mon;3;r1").rooms == -1 && parse_edit_info("mon;3;r1").revision.empty());
  CHECK(parse_edit_info("mon;x;r1;").kind.empty() && parse_edit_info("mon;x;r1;").revision.empty());
  CHECK_STR(parse_edit_info("mon;3;;conflict").error, "conflict");   // the error code is still readable
  CHECK(!parse_edit_info("mon;3").valid);
  CHECK(!parse_edit_info("mon").valid);
  CHECK(!parse_edit_info("mon;3;r1;;").valid);        // too many ";"
  CHECK(!parse_edit_info("mon;3;r1;a;b").valid);
  CHECK(parse_edit_info("mon;3;r1;a;b").kind.empty());   // and nothing is trusted
  CHECK(!parse_edit_info("funday;3;r1;").valid);      // unknown kind
  CHECK(!parse_edit_info("MON;3;r1;").valid);
  CHECK(!parse_edit_info("mon;-1;r1;").valid);
  CHECK(!parse_edit_info("mon;x;r1;").valid);
  CHECK(!parse_edit_info("mon;;r1;").valid);
  CHECK(!parse_edit_info("mon;99999;r1;").valid);     // too many digits
  CHECK(!parse_edit_info("mon;3.5;r1;").valid);
  CHECK(!parse_edit_info("mon;3;;").valid);           // no revision
  CHECK(!parse_edit_info("mon;3;a b;").valid);        // space in the revision
  CHECK(!parse_edit_info("mon;3;" + std::string(65, 'a') + ";").valid);
  CHECK(parse_edit_info("mon;3;" + std::string(64, 'a') + ";").valid);
  CHECK(!parse_edit_info("unknown").valid);
  CHECK(!parse_edit_info("unavailable").valid);
  CHECK(!parse_edit_info(std::string(300, 'x')).valid);
  CHECK(!parse_edit_info(std::string("mon;3;r1;") + std::string(300, 'x')).valid);
  // An error that is not a code reads "unknown" (still an error), a long one too.
  CHECK_STR(parse_edit_info("mon;3;r1;bad code!").error, "unknown");
  CHECK_STR(parse_edit_info("mon;3;r1;" + std::string(41, 'a')).error, "unknown");
  CHECK_STR(parse_edit_info("mon;3;r1;" + std::string(40, 'a')).error, std::string(40, 'a'));
  CHECK(parse_edit_info("mon;3;r1;bad code!").valid);
  const char embedded[] = "mon;3;r1;a\0b";
  CHECK_STR(parse_edit_info(std::string(embedded, sizeof embedded - 1)).error, "unknown");
  CHECK(parse_edit_info("mon;3;r\xc3\xa9;").revision.empty());   // non-ASCII
}

// ------------------------------------------------------------------------------------------------ loading

TEST(load_skips_automatic_bands_and_keeps_parallel_data) {
  EditModel m = model_of({A(0, 360), U(360, 480, 1, 21.5), A(480, 1020), U(1020, 1350, 2, 24, "eco"), A(1350, 1440)});
  CHECK(!m.damaged());
  CHECK(m.size() == 2);
  CHECK_STR(spec(m), "360-480 h2150 | 1020-1350 c2400p=eco");
  CHECK(!m.dirty());
  CHECK(m.selected() == -1);
}

TEST(load_preset_only_and_off) {
  EditModel m = model_of({U(60, 120, 1, -100, "eco"), U(120, 180, 0, -100), U(180, 240, 1, 19, "away")});
  CHECK(!m.damaged());
  CHECK_STR(spec(m), "60-120 hp=eco | 120-180 o | 180-240 h1900p=away");
}

TEST(load_empty_day) {
  EditModel m = model_of({A(0, 1440)});
  CHECK(m.size() == 0 && !m.damaged() && !m.dirty());
  EditModel e = model_of({});
  CHECK(e.size() == 0 && !e.damaged());
  CHECK(e.page_count() == 1 && e.page_rows() == 0);
  CHECK(e.save_block() == SaveBlock::NOT_DIRTY);
}

TEST(load_locked_flags_and_inexpressible_values) {
  EditModel m = model_of({U(0, 60, 1, 20, "", 1), U(60, 120, 1, -100, "Ecò"), U(120, 180, 1, 20.123), U(180, 240, 1, -5.0),
                          U(240, 300, 1, 1000.0), U(300, 360, 1, -100, "has space"), U(360, 420, 1, -100, std::string(41, 'a')),
                          U(420, 480, 1, 21.5)});
  CHECK(m.size() == 8);
  for (size_t i = 0; i < 7; ++i) CHECK(m.band(i)->locked);
  CHECK(!m.band(7)->locked);
  CHECK(m.has_locked());
  CHECK(m.save_block() == SaveBlock::LOCKED);
  CHECK_STR(save_block_code(m.save_block()), "locked");
}

TEST(load_damaged_inputs) {
  {  // lists of different length
    EditInput in = input({U(0, 60, 1, 20), U(60, 120, 1, 21)});
    in.lists.temp.pop_back();
    EditModel m;
    m.load(in);
    CHECK(m.damaged());
    CHECK(m.save_block() == SaveBlock::DAMAGED);
  }
  {  // preset list missing: saving would drop the presets
    EditInput in = input({U(0, 60, 1, -100, "eco")});
    in.preset.clear();
    EditModel m;
    m.load(in);
    CHECK(m.damaged());
    CHECK(m.size() == 1 && m.band(0)->preset.empty());
  }
  {  // locked list missing
    EditInput in = input({U(0, 60, 1, 20)});
    in.flags.clear();
    EditModel m;
    m.load(in);
    CHECK(m.damaged());
  }
  {  // NaN, inf, start >= end
    EditModel m = model_of({U(NaN, 60, 1, 20), U(0, INF, 1, 20), U(100, 100, 1, 20), U(200, 100, 1, 20), U(300, 360, 1, 20)});
    CHECK(m.damaged());
    CHECK(m.size() == 1);   // 0..inf clamps to 0-1440; the rest is dropped or has no room left
  }
  {  // overlap is repaired and flagged
    EditModel m = model_of({U(0, 600, 1, 20), U(300, 900, 1, 21)});
    CHECK(m.damaged());
    CHECK_STR(spec(m), "0-600 h2000 | 600-900 h2100");
    CHECK(m.save_block() == SaveBlock::DAMAGED);
  }
  {  // unsorted input is sorted
    EditModel m = model_of({U(600, 700, 1, 21), U(0, 60, 1, 20)});
    CHECK(!m.damaged());
    CHECK_STR(spec(m), "0-60 h2000 | 600-700 h2100");
  }
  {  // fractional and out-of-day minutes, unknown mode, temp on an off band
    CHECK(model_of({U(0.4, 60, 1, 20)}).damaged());
    CHECK(model_of({U(-5, 60, 1, 20)}).damaged());
    CHECK(model_of({U(0, 2000, 1, 20)}).damaged());
    CHECK(model_of({U(0, 60, 7, 20)}).damaged());
    CHECK(model_of({U(0, 60, NaN, 20)}).damaged());
    CHECK(model_of({U(0, 60, 0, 20)}).damaged());
    CHECK(model_of({U(0, 60, 0, -100, "eco")}).damaged());
    CHECK(model_of({U(0, 60, 1, -100)}).damaged());   // heat with nothing
  }
  {  // 17 user bands: more than the tile edits
    std::vector<S> v;
    for (int i = 0; i < 17; ++i) v.push_back(U(i * 60, i * 60 + 30, 1, 20));
    EditModel m = model_of(v);
    CHECK(m.size() == 16);
    CHECK(m.damaged());
  }
  {  // 16 are fine
    std::vector<S> v;
    for (int i = 0; i < 16; ++i) v.push_back(U(i * 60, i * 60 + 30, 1, 20));
    EditModel m = model_of(v);
    CHECK(m.size() == 16 && !m.damaged());
  }
  {  // huge lists
    DayLists l;
    EditInput in;
    for (int i = 0; i < 300; ++i) {
      in.lists.start.push_back(0);
      in.lists.end.push_back(0);
      in.lists.mode.push_back(0);
      in.lists.temp.push_back(0);
      in.flags.push_back(1);
      in.preset.push_back("");
    }
    in.info.n = 300;
    EditModel m;
    m.load(in);
    CHECK(m.damaged());
    CHECK(m.size() == 0);
  }
}

TEST(load_automatic_flag_hostile) {
  EditInput in = input({U(0, 60, 1, 20), U(60, 120, 1, 21)});
  in.flags[1] = NaN;   // not a number: nothing is known of the band (it stays a row) and the day is damaged
  EditModel m;
  m.load(in);
  CHECK(m.size() == 2 && m.damaged());
  in.flags[1] = 1;     // bit 1: automatic
  m.load(in);
  CHECK(m.size() == 1 && !m.damaged());
  in.flags[1] = 3;     // automatic and locked: still a gap
  m.load(in);
  CHECK(m.size() == 1 && !m.has_locked());
  in.flags[1] = 2;     // bit 2: locked
  m.load(in);
  CHECK(m.size() == 2 && m.band(1)->locked && !m.damaged());
  for (double bad : {-1.0, 256.0, 0.5, 1e30, INF}) {
    in.flags[1] = bad;
    m.load(in);
    CHECK(m.damaged());
  }
}

// ------------------------------------------------------------------------------------------------ pages and selection

TEST(pages_and_rows) {
  std::vector<S> v;
  for (int i = 0; i < 10; ++i) v.push_back(U(i * 100, i * 100 + 50, 1, 20));
  EditModel m = model_of(v);
  CHECK(m.rows_per_page() == 3);
  m.set_rows_per_page(1);
  CHECK(m.rows_per_page() == 3);   // 3..14
  m.set_rows_per_page(99);
  CHECK(m.rows_per_page() == 14);
  m.set_rows_per_page(-4);
  CHECK(m.rows_per_page() == 3);
  m.set_rows_per_page(4);
  CHECK(m.page_count() == 3);
  CHECK(m.page() == 0 && m.page_first() == 0 && m.page_rows() == 4);
  CHECK(!m.prev_page());
  CHECK(m.next_page() && m.next_page());
  CHECK(!m.next_page());
  CHECK(m.page() == 2 && m.page_first() == 8 && m.page_rows() == 2);
  CHECK(m.select(5));
  CHECK(m.page() == 1);   // the page of the row
  CHECK(!m.select(10));
  CHECK(!m.select(-1));
  CHECK(m.selected() == 5);
  m.set_rows_per_page(14);
  CHECK(m.page() == 0 && m.page_count() == 1 && m.page_rows() == 10);
  m.set_rows_per_page(3);
  CHECK(m.page() == 1);   // 5 / 3: still shows the selected row
  m.deselect();
  CHECK(m.selected() == -1);
}

TEST(rows_exactly_full_page) {
  std::vector<S> v;
  for (int i = 0; i < 6; ++i) v.push_back(U(i * 100, i * 100 + 50, 1, 20));
  EditModel m = model_of(v);
  m.set_rows_per_page(3);
  CHECK(m.page_count() == 2);
  v.push_back(U(700, 750, 1, 20));
  m.load(input(v));
  CHECK(m.page_count() == 3);
}

TEST(title) {
  CHECK_STR(edit_title("Casa", 3), "Casa - 3 rooms");
  CHECK_STR(edit_title("Casa", 1), "Casa - 1 room");
  CHECK_STR(edit_title("Casa", 3, "stanza", "stanze"), "Casa - 3 stanze");
  CHECK_STR(edit_title("Casa", -1), "Casa");
  CHECK_STR(edit_title("", 2), "2 rooms");
  CHECK_STR(edit_title("unknown", 2), "2 rooms");
  CHECK_STR(edit_title("unavailable", 2), "2 rooms");
  CHECK_STR(edit_title("", -1), "");
  CHECK(edit_title(std::string(200, 'x'), 2).size() <= MAX_NAME_BYTES + 10);
  // an UTF-8 name is not cut in the middle of a character
  std::string long_name;
  for (int i = 0; i < 30; ++i) long_name += "\xc3\xa8";
  const std::string t = edit_title(long_name, 2);
  CHECK(t.find(" - 2 rooms") != std::string::npos);
  EditSession s;
  s.open(input({U(0, 60, 1, 20)}, "mon;3;r1;", "Casa"), make_range(5, 35, 0.5), 4);
  CHECK_STR(s.title(), "Casa - 3 rooms");
  CHECK_STR(s.title("stanza", "stanze"), "Casa - 3 stanze");
}

// ------------------------------------------------------------------------------------------------ time moves

TEST(move_start_rules) {
  EditModel m = model_of({U(60, 120, 1, 20), U(120, 300, 1, 21), U(600, 720, 1, 22)});
  m.select(1);
  EditResult r = m.move_start(-15);   // adjacent to the band before: blocked, the neighbour is not dragged
  CHECK(!r.changed && r.msg == Msg::EDIT_BLOCKED);
  CHECK_STR(msg_code(r.msg), "edit_blocked");
  CHECK(m.band(0)->end == 120 && m.band(1)->start == 120);
  CHECK(!m.dirty());
  r = m.move_start(60);
  CHECK(r.changed && r.msg == Msg::NONE);
  CHECK(m.band(1)->start == 180);
  CHECK(m.dirty());
  r = m.move_start(-60);
  CHECK(r.changed);
  CHECK(!m.dirty());   // back to the start: not a modification
  m.select(2);
  CHECK(m.move_start(-60).changed);
  CHECK(m.band(2)->start == 540);
  CHECK(m.move_start(60 * 100).msg == Msg::EDIT_BLOCKED);
  CHECK(m.move_start(60 * 2).changed);   // 660..720
  CHECK(m.move_start(15).changed);       // 675 : 45 min left
  CHECK(m.move_start(15).changed);       // 690 : 30
  CHECK(m.move_start(15).changed);       // 705 : 15 (the minimum)
  CHECK(m.band(2)->start == 705 && m.band(2)->end - m.band(2)->start == 15);
  CHECK(m.move_start(15).msg == Msg::EDIT_BLOCKED);   // 10 min would be left
  CHECK(m.move_start(0).msg == Msg::NONE && !m.move_start(0).changed);
  CHECK(m.band(2)->start == 705);
}

TEST(move_start_at_day_edge) {
  EditModel m = model_of({U(30, 120, 1, 20)});
  m.select(0);
  CHECK(m.move_start(-60).msg == Msg::EDIT_BLOCKED);   // below 00:00
  CHECK(m.move_start(-15).changed);
  CHECK(m.band(0)->start == 15);
  CHECK(m.move_start(-15).changed);
  CHECK(m.band(0)->start == 0);
  CHECK(m.move_start(-15).msg == Msg::EDIT_BLOCKED);
  EditModel z = model_of({U(0, 60, 1, 20)});
  z.select(0);
  CHECK(z.move_start(-1).msg == Msg::EDIT_BLOCKED);
}

TEST(move_end_rules) {
  EditModel m = model_of({U(60, 120, 1, 20), U(120, 300, 1, 21), U(1380, 1440, 1, 22)});
  m.select(0);
  CHECK(m.move_end(15).msg == Msg::EDIT_BLOCKED);   // touches the next band
  CHECK(m.move_end(-15).changed);
  CHECK(m.move_end(15).changed);                    // back
  CHECK(!m.dirty());
  CHECK(m.move_end(-60).msg == Msg::EDIT_BLOCKED);  // would leave 0 min
  CHECK(m.move_end(-45).changed);                   // 15 min left
  CHECK(m.band(0)->end == 75);
  CHECK(m.move_end(-15).msg == Msg::EDIT_BLOCKED);
  m.select(2);
  CHECK(m.move_end(15).msg == Msg::EDIT_BLOCKED);   // after 24:00
  CHECK(m.move_end(-15).changed);
  CHECK(m.move_end(15).changed);
  CHECK(m.band(2)->end == 1440);
  CHECK(m.move_end(60).msg == Msg::EDIT_BLOCKED);
  CHECK(m.move_end(-60 * 1000).msg == Msg::EDIT_BLOCKED);
  CHECK(m.move_end(INT_MAX).msg == Msg::EDIT_BLOCKED);
  CHECK(m.move_end(INT_MIN).msg == Msg::EDIT_BLOCKED);
  CHECK(m.move_start(INT_MIN).msg == Msg::EDIT_BLOCKED);
  CHECK(m.move_start(INT_MAX).msg == Msg::EDIT_BLOCKED);
}

TEST(move_without_selection_or_locked) {
  EditModel m = model_of({U(60, 120, 1, 20, "", 1), U(300, 400, 1, 20)});
  CHECK(!m.move_start(15).changed && m.move_start(15).msg == Msg::NONE);   // nothing selected: nothing
  CHECK(!m.move_end(15).changed && !m.cycle_mode().changed && !m.step_temp(1).changed && !m.remove().changed);
  m.select(0);
  EditResult r = m.move_start(15);
  CHECK(!r.changed && r.msg == Msg::LOCKED);
  CHECK(m.move_end(15).msg == Msg::LOCKED);
  CHECK(m.cycle_mode().msg == Msg::LOCKED);
  CHECK(m.step_temp(1).msg == Msg::LOCKED);
  CHECK(m.remove().msg == Msg::LOCKED);
  CHECK_STR(msg_code(Msg::LOCKED), "locked");
  CHECK(m.size() == 2 && !m.dirty());
}

// ------------------------------------------------------------------------------------------------ mode and temperature

TEST(mode_cycle) {
  EditModel m = model_of({U(60, 120, 1, 20.5, "eco")});
  m.select(0);
  CHECK(m.cycle_mode().changed);
  CHECK_STR(spec(m), "60-120 c2050p=eco");   // cool keeps temperature and preset
  CHECK(m.cycle_mode().changed);
  CHECK_STR(spec(m), "60-120 o");             // off drops both
  CHECK(m.cycle_mode().changed);
  CHECK_STR(spec(m), "60-120 h2000");         // heat again: default temperature, no preset
  CHECK(m.cycle_mode().changed && m.cycle_mode().changed && m.cycle_mode().changed);   // full circle
  CHECK_STR(spec(m), "60-120 h2000");
}

TEST(mode_cycle_preset_only_and_neighbour_temp) {
  EditModel m = model_of({U(60, 120, 1, -100, "eco"), U(120, 180, 0, -100), U(180, 240, 2, 24)});
  m.select(0);
  CHECK(m.cycle_mode().changed);
  CHECK_STR(spec(m), "60-120 cp=eco | 120-180 o | 180-240 c2400");
  m.select(1);
  CHECK(m.cycle_mode().changed);   // off -> heat: the neighbour before has no temperature, so the nearest with one
  CHECK(m.band(1)->mode == Mode::HEAT && m.band(1)->has_temp && m.band(1)->centi == 2400);
}

TEST(mode_to_heat_snaps_to_range) {
  EditModel m = model_of({U(60, 120, 0, -100)});
  m.set_range(make_range(5, 18, 0.5));   // 20 is above the maximum
  m.select(0);
  CHECK(m.cycle_mode().changed);
  CHECK(m.band(0)->centi == 1800);
}

TEST(temperature_steps) {
  EditModel m = model_of({U(60, 120, 1, 20.0)});
  m.set_range(make_range(16, 22, 0.5));
  m.select(0);
  CHECK(m.step_temp(1).changed);
  CHECK(m.band(0)->centi == 2050);
  CHECK(m.step_temp(-3).changed);
  CHECK(m.band(0)->centi == 1900);
  CHECK(m.step_temp(100).changed);
  CHECK(m.band(0)->centi == 2200);
  EditResult r = m.step_temp(1);   // at the limit: nothing, not even a message
  CHECK(!r.changed && r.msg == Msg::NONE);
  CHECK(m.step_temp(-100).changed);
  CHECK(m.band(0)->centi == 1600);
  CHECK(!m.step_temp(-1).changed);
  CHECK(!m.step_temp(0).changed);
  CHECK(m.step_temp(INT_MAX).changed);
  CHECK(m.band(0)->centi == 2200);
  CHECK(m.step_temp(INT_MIN).changed);
  CHECK(m.band(0)->centi == 1600);
}

TEST(temperature_step_grid_and_off_band) {
  EditModel m = model_of({U(60, 120, 1, 20.3), U(120, 180, 0, -100)});
  m.set_range(make_range(5, 35, 0.5));
  m.select(0);
  CHECK(m.step_temp(1).changed);
  CHECK(m.band(0)->centi == 2100);   // 20.3 snaps to 20.5, then + 0.5
  m.select(1);
  CHECK(!m.step_temp(1).changed);    // off: no temperature
  CHECK(!m.band(1)->has_temp);
  EditModel f = model_of({U(60, 120, 1, 20.0)});
  f.set_range(make_range(5, 35, 1.0));
  f.select(0);
  f.step_temp(1);
  CHECK(f.band(0)->centi == 2100);
  f.step_temp(1);
  CHECK(f.band(0)->centi == 2200);
}

TEST(temperature_on_preset_only_band) {
  EditModel m = model_of({U(60, 120, 1, -100, "eco")});
  m.select(0);
  CHECK(m.step_temp(1).changed);
  CHECK(m.band(0)->has_temp && m.band(0)->preset == "eco");
  CHECK(m.band(0)->centi == 2050);   // default 20.0 plus one step
}

// ------------------------------------------------------------------------------------------------ add / remove

TEST(add_into_empty_day) {
  EditModel m = model_of({});
  EditResult r = m.add();
  CHECK(r.changed);
  CHECK_STR(spec(m), "0-60 h2000");
  CHECK(m.selected() == 0);
  CHECK(m.dirty());
}

TEST(add_first_gap_of_an_hour) {
  EditModel m = model_of({U(0, 30, 1, 21), U(60, 600, 2, 25), U(600, 1380, 1, 19)});   // gap 30..60 is too short
  CHECK(m.add().changed);
  CHECK_STR(spec(m), "0-30 h2100 | 60-600 c2500 | 600-1380 h1900 | 1380-1440 h1900");
}

TEST(add_takes_neighbour_before) {
  EditModel m = model_of({U(0, 60, 2, 25), U(180, 240, 1, 19)});   // gap 60..180
  CHECK(m.add().changed);
  CHECK_STR(spec(m), "0-60 c2500 | 60-120 c2500 | 180-240 h1900");
  CHECK(m.selected() == 1);
}

TEST(add_first_gap_before_first_band_uses_the_next) {
  EditModel m = model_of({U(120, 240, 2, 23)});
  CHECK(m.add().changed);
  CHECK_STR(spec(m), "0-60 c2300 | 120-240 c2300");
  CHECK(m.selected() == 0);
}

TEST(add_neighbour_off_or_preset_only_gives_heat) {
  EditModel off = model_of({U(0, 60, 0, -100), U(300, 360, 1, 18)});
  CHECK(off.add().changed);
  CHECK_STR(spec(off), "0-60 o | 60-120 h1800 | 300-360 h1800");   // heat; the temperature of the nearest band that has one
  EditModel pre = model_of({U(0, 60, 1, -100, "eco")});
  CHECK(pre.add().changed);
  CHECK_STR(pre.bands()[1].preset, "");   // the preset is not copied
  CHECK(pre.band(1)->mode == Mode::HEAT && pre.band(1)->has_temp && pre.band(1)->centi == 2000);
}

TEST(add_no_space_and_full) {
  EditModel m = model_of({U(0, 60, 1, 20), U(75, 1440, 1, 20)});   // the only gap is 15 min
  EditResult r = m.add();
  CHECK(!r.changed && r.msg == Msg::NO_SPACE);
  CHECK_STR(msg_code(r.msg), "no_space");
  CHECK(!m.dirty() && m.size() == 2);
  EditModel whole = model_of({U(0, 1440, 1, 20)});
  CHECK(whole.add().msg == Msg::NO_SPACE);
  std::vector<S> v;
  for (int i = 0; i < 16; ++i) v.push_back(U(i * 60, i * 60 + 30, 1, 20));
  EditModel full = model_of(v);
  r = full.add();
  CHECK(!r.changed && r.msg == Msg::FULL);
  CHECK_STR(msg_code(r.msg), "full");
  CHECK(full.size() == 16);
}

TEST(add_exactly_one_hour_gap_at_the_end_and_in_the_middle) {
  EditModel m = model_of({U(0, 1380, 1, 20)});
  CHECK(m.add().changed);
  CHECK_STR(spec(m), "0-1380 h2000 | 1380-1440 h2000");
  EditModel mid = model_of({U(0, 600, 1, 20), U(660, 1440, 2, 24)});
  CHECK(mid.add().changed);
  CHECK_STR(spec(mid), "0-600 h2000 | 600-660 h2000 | 660-1440 c2400");
}

TEST(add_sixteenth_band_and_then_full) {
  std::vector<S> v;
  for (int i = 0; i < 15; ++i) v.push_back(U(i * 60, i * 60 + 30, 1, 20));
  EditModel m = model_of(v);
  CHECK(m.add().changed);
  CHECK(m.size() == 16);
  CHECK(m.add().msg == Msg::FULL);
}

TEST(add_then_the_equal_neighbour_is_sent_merged) {
  // an added hour identical to its neighbour is sent merged with it
  EditModel m = model_of({U(0, 60, 1, 20), U(120, 180, 2, 25)});
  CHECK(m.add().changed);   // 60-120 heat 20, same as the band before
  CHECK(m.dirty());   // the first band is one hour longer once merged
  CHECK_STR(m.encode().text, "00:00-02:00 heat 20; 02:00-03:00 cool 25");
}

TEST(remove) {
  EditModel m = model_of({U(0, 60, 1, 20), U(60, 120, 1, 21), U(120, 180, 1, 22)});
  m.select(1);
  CHECK(m.remove().changed);
  CHECK_STR(spec(m), "0-60 h2000 | 120-180 h2200");
  CHECK(m.selected() == 1);
  CHECK(m.remove().changed);
  CHECK(m.selected() == 0);
  CHECK(m.remove().changed);
  CHECK(m.size() == 0 && m.selected() == -1);
  CHECK(!m.remove().changed);
  CHECK(m.dirty());
  CHECK(m.save_block() == SaveBlock::NONE);   // a day without user bands is a legitimate save
  CHECK(m.encode().ok && m.encode().text.empty());
}

TEST(remove_then_add_restores) {
  EditModel m = model_of({U(0, 60, 1, 20)});
  m.select(0);
  m.remove();
  CHECK(m.add().changed);
  CHECK_STR(spec(m), "0-60 h2000");
  CHECK(!m.dirty());   // same content as the start
}

TEST(remove_keeps_the_page_valid) {
  std::vector<S> v;
  for (int i = 0; i < 7; ++i) v.push_back(U(i * 100, i * 100 + 50, 1, 20));
  EditModel m = model_of(v);
  m.set_rows_per_page(3);
  m.select(6);
  CHECK(m.page() == 2);
  m.remove();
  CHECK(m.page() == 1 && m.selected() == 5);
  CHECK(m.page_count() == 2);
}

TEST(frozen_model_refuses_changes) {
  EditModel m = model_of({U(0, 60, 1, 20)});
  m.select(0);
  m.set_frozen(true);
  CHECK(m.move_end(15).msg == Msg::EDIT_BLOCKED);
  CHECK(m.add().msg == Msg::EDIT_BLOCKED);
  CHECK(m.remove().msg == Msg::EDIT_BLOCKED);
  CHECK(m.cycle_mode().msg == Msg::EDIT_BLOCKED);
  CHECK(m.step_temp(1).msg == Msg::EDIT_BLOCKED);
  CHECK(!m.dirty());
  m.set_frozen(false);
  CHECK(m.move_end(15).changed);
}

TEST(invariants_after_random_walk) {
  // A deterministic pseudo-random sequence of keys never breaks order, overlap, minimum length, day limits.
  EditModel m = model_of({U(60, 300, 1, 20), U(300, 600, 2, 22), U(900, 1000, 1, 19, "eco")});
  m.set_range(make_range(5, 35, 0.5));
  uint32_t x = 12345;
  for (int step = 0; step < 4000; ++step) {
    x = x * 1664525u + 1013904223u;
    const int op = (int) ((x >> 16) % 10);
    if (m.size() > 0 && (x >> 8) % 3 == 0) m.select((int) ((x >> 12) % m.size()));
    switch (op) {
      case 0: m.move_start(-60); break;
      case 1: m.move_start(60); break;
      case 2: m.move_start(-15); break;
      case 3: m.move_start(15); break;
      case 4: m.move_end(-60); break;
      case 5: m.move_end(60); break;
      case 6: m.move_end(15); break;
      case 7: m.cycle_mode(); break;
      case 8: (x & 1) ? m.add() : m.remove(); break;
      default: m.step_temp((x & 2) ? 1 : -1); break;
    }
    int covered = 0;
    CHECK(m.size() <= EDIT_MAX_BANDS);
    for (const EditBand &b : m.bands()) {
      CHECK(b.start >= covered);
      CHECK(b.end <= MINUTES_PER_DAY);
      CHECK(b.end - b.start >= EDIT_MIN_MINUTES);
      CHECK(b.mode == Mode::OFF ? (!b.has_temp && b.preset.empty()) : (b.has_temp || !b.preset.empty()));
      covered = b.end;
    }
    if (g_failed) return;
    CHECK(m.encode().ok);
  }
}

// ------------------------------------------------------------------------------------------------ encode / round trip

TEST(encode_text_form) {
  EditModel m = model_of({U(0, 360, 1, 17), U(360, 510, 1, 21.5), U(1020, 1440, 2, 24.25), U(1000, 1010, 0, -100)});
  const EncodeResult e = m.encode();
  CHECK(e.ok);
  CHECK_STR(e.text, "00:00-06:00 heat 17; 06:00-08:30 heat 21.5; 16:40-16:50 off; 17:00-24:00 cool 24.25");
}

TEST(encode_preset_and_empty) {
  EditModel m = model_of({U(60, 120, 1, -100, "eco"), U(120, 180, 1, 19.5, "Away_2-x")});
  CHECK_STR(m.encode().text, "01:00-02:00 heat preset=eco; 02:00-03:00 heat 19.5 preset=Away_2-x");
  EditModel e = model_of({A(0, 1440)});
  CHECK(e.encode().ok);
  CHECK_STR(e.encode().text, "");
}

TEST(encode_temperature_formatting) {
  CHECK_STR(temp_text(2000), "20");
  CHECK_STR(temp_text(2050), "20.5");
  CHECK_STR(temp_text(1725), "17.25");
  CHECK_STR(temp_text(0), "0");
  CHECK_STR(temp_text(5), "0.05");
  CHECK_STR(temp_text(100), "1");
  CHECK_STR(temp_text(99999), "999.99");
  CHECK_STR(temp_text(1000), "10");
}

TEST(encode_rejects_unwritable_bands) {
  auto one = [](int s, int e, Mode m, bool ht, int c, const char *p) {
    EditBand b;
    b.start = s;
    b.end = e;
    b.mode = m;
    b.has_temp = ht;
    b.centi = c;
    b.preset = p;
    return std::vector<EditBand>{b};
  };
  CHECK(encode_bands(one(0, 60, Mode::HEAT, true, 2000, "")).ok);
  CHECK(!encode_bands(one(60, 60, Mode::HEAT, true, 2000, "")).ok);
  CHECK(!encode_bands(one(60, 30, Mode::HEAT, true, 2000, "")).ok);
  CHECK(!encode_bands(one(-1, 30, Mode::HEAT, true, 2000, "")).ok);
  CHECK(!encode_bands(one(0, 1441, Mode::HEAT, true, 2000, "")).ok);
  CHECK(!encode_bands(one(0, 60, Mode::HEAT, true, -100, "")).ok);
  CHECK(!encode_bands(one(0, 60, Mode::HEAT, true, 100000, "")).ok);
  CHECK(!encode_bands(one(0, 60, Mode::HEAT, false, 0, "")).ok);          // heat with nothing
  CHECK(!encode_bands(one(0, 60, Mode::OFF, true, 2000, "")).ok);         // off with a temperature
  CHECK(!encode_bands(one(0, 60, Mode::OFF, false, 0, "eco")).ok);        // off with a preset
  CHECK(!encode_bands(one(0, 60, Mode::HEAT, false, 0, "a b")).ok);
  CHECK(!encode_bands(one(0, 60, Mode::HEAT, false, 0, "a;b")).ok);       // would split the band
  CHECK(!encode_bands(one(0, 60, Mode::HEAT, false, 0, "\xc3\xa8")).ok);
  CHECK(!encode_bands(one(0, 60, Mode::HEAT, false, 0, std::string(41, 'a').c_str())).ok);
  CHECK(encode_bands(one(0, 60, Mode::HEAT, false, 0, std::string(40, 'a').c_str())).ok);
  CHECK_STR(encode_bands(one(0, 60, Mode::HEAT, false, 0, "a b")).error, "invalid");
  std::vector<EditBand> two = one(0, 100, Mode::HEAT, true, 2000, "");
  two.push_back(one(50, 200, Mode::HEAT, true, 2000, "")[0]);
  CHECK(!encode_bands(two).ok);   // overlap
  std::swap(two[0], two[1]);
  CHECK(!encode_bands(two).ok);   // out of order
}

TEST(encode_too_long) {
  // 16 bands with the longest form: 'hh:mm-hh:mm heat 123.45 preset=<40 chars>'
  std::vector<EditBand> v;
  for (int i = 0; i < 16; ++i) {
    EditBand b;
    b.start = i * 60;
    b.end = i * 60 + 30;
    b.mode = Mode::HEAT;
    b.has_temp = true;
    b.centi = 12345;
    b.preset = std::string(40, 'p');
    v.push_back(b);
  }
  EncodeResult e = encode_bands(v);
  CHECK(!e.ok);
  CHECK_STR(e.error, "too_long");
  CHECK(e.text.empty());
  // ... 12 of them still fit
  v.resize(12);
  e = encode_bands(v);
  CHECK(e.ok);
  CHECK(e.text.size() <= EDIT_MAX_TEXT);
  // The model says so too
  std::vector<S> s;
  for (int i = 0; i < 16; ++i) s.push_back(U(i * 60, i * 60 + 30, 1, 123.45, std::string(40, 'p')));
  EditModel m = model_of(s);
  m.select(0);
  m.move_end(-15);
  CHECK(m.save_block() == SaveBlock::TOO_LONG);
  CHECK_STR(save_block_code(m.save_block()), "too_long");
}

TEST(sixteen_ordinary_bands_fit_with_room_to_spare) {
  std::vector<S> s;
  for (int i = 0; i < 16; ++i) s.push_back(U(i * 60, i * 60 + 30, 1, 21.5));
  EditModel m = model_of(s);
  m.select(0);
  m.move_end(-15);
  CHECK(m.encode().ok);
  CHECK(m.encode().text.size() < 400);
}

TEST(round_trip_real_days) {
  const std::vector<std::vector<S>> days = {
      {U(0, 360, 1, 17), U(360, 510, 1, 21.5), A(510, 1020), U(1020, 1350, 1, 21.5), U(1350, 1440, 0, -100)},
      {U(0, 1440, 2, 26)},
      {U(60, 120, 1, -100, "eco"), U(120, 180, 1, 19.5, "comfort_2"), U(180, 240, 0, -100)},
      {A(0, 1440)},
      {U(1, 2, 1, 5.05), U(1438, 1440, 1, 35)},
      {U(0, 15, 1, 0.5), U(15, 30, 1, 0.0), U(30, 45, 1, 999.99)},
  };
  for (const auto &d : days) {
    EditModel m = model_of(d);
    CHECK(!m.damaged());
    const EncodeResult e = encode_bands(normalized(m.bands()));
    CHECK(e.ok);
    std::vector<EditBand> back;
    CHECK(decode(e.text, back));
    EditModel again;
    EditInput in;
    for (const EditBand &b : back) {
      in.lists.start.push_back(b.start);
      in.lists.end.push_back(b.end);
      in.lists.mode.push_back((int) b.mode);
      in.lists.temp.push_back(b.has_temp ? b.centi / 100.0 : -100);
      in.flags.push_back(0);
      in.preset.push_back(b.preset);
    }
    in.info = parse_edit_info("mon;3;r1;");
    in.info.n = (int) back.size();
    again.load(in);
    CHECK(!again.damaged());
    CHECK(same_bands(again.bands(), m.bands()));
    CHECK_STR(spec(again), spec(m));
  }
}

TEST(round_trip_preset_exactly_as_read) {
  EditModel m = model_of({U(60, 120, 1, -100, "Eco-Mode_1")});
  CHECK_STR(m.encode().text, "01:00-02:00 heat preset=Eco-Mode_1");
}

TEST(normalized_merges_touching_equal_bands) {
  EditModel m = model_of({U(0, 60, 1, 20), U(60, 120, 1, 20), U(120, 180, 1, 21), U(200, 260, 1, 21)});
  const std::vector<EditBand> n = normalized(m.bands());
  CHECK(n.size() == 3);
  CHECK(n[0].start == 0 && n[0].end == 120);
  CHECK_STR(m.encode().text, "00:00-02:00 heat 20; 02:00-03:00 heat 21; 03:20-04:20 heat 21");
  // different preset or different mode do not merge
  EditModel p = model_of({U(0, 60, 1, 20, "a"), U(60, 120, 1, 20, "b"), U(120, 180, 2, 20)});
  CHECK(normalized(p.bands()).size() == 3);
  // same_bands is order-independent in input and ignores the 'locked' flag
  std::vector<EditBand> a = m.bands(), b = m.bands();
  std::swap(b[0], b[3]);
  CHECK(same_bands(a, b));
  b[0].centi += 5;
  CHECK(!same_bands(a, b));
}

// ------------------------------------------------------------------------------------------------ the session

static const SetpointRange RNG = make_range(5, 35, 0.5);

static EditSession open_session(const std::vector<S> &v, const char *info = "mon;3;r1;") {
  EditSession s;
  s.open(input(v, info), RNG, 4);
  return s;
}

TEST(save_once_and_saved) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  CHECK(s.model().select(0));
  CHECK(!s.can_save());   // not dirty
  CHECK(!s.press_save(100).send);
  CHECK(s.state() == SaveState::IDLE);
  s.model().step_temp(1);
  CHECK(s.can_save());
  SaveRequest r = s.press_save(1000);
  CHECK(r.send);
  CHECK_STR(r.bands, "01:00-02:00 heat 20.5");
  CHECK_STR(r.day_kind, "mon");
  CHECK_STR(r.base_revision, "r1");
  CHECK(s.state() == SaveState::SAVING);
  CHECK(!s.press_save(1100).send);   // the second tap is ignored
  CHECK(!s.press_save(1200).send);
  CHECK(s.state() == SaveState::SAVING);
  CHECK(!s.can_save());
  // an on_state without relevant change: still saving
  CHECK(s.on_state(input({U(60, 120, 1, 20)}, "mon;3;r1;"), 1500) == EditEvent::NONE);
  CHECK(s.state() == SaveState::SAVING);
  // the save arrives: new revision, the sent bands
  CHECK(s.on_state(input({U(60, 120, 1, 20.5)}, "mon;3;r2;"), 1800) == EditEvent::SAVED);
  CHECK(s.state() == SaveState::SAVED);
  CHECK_STR(s.seen_revision(), "r2");
  CHECK(!s.model().dirty());
  CHECK(!s.can_save());
  // acknowledged by the next tap
  CHECK(!s.press_save(2000).send);
  CHECK(s.state() == SaveState::IDLE);
}

TEST(saved_compares_content_not_text) {
  // we send two touching equal bands; HA brings them merged, with the temperature written the other way
  EditSession s = open_session({U(0, 60, 1, 20), U(120, 180, 2, 25)});
  s.model().select(0);
  s.model().move_end(0);
  CHECK(s.model().add().changed);                       // 60-120 heat 20 (merges with the first)
  s.model().step_temp(1);                                // 60-120 heat 20.5: really different
  SaveRequest r = s.press_save(0);
  CHECK(r.send);
  CHECK_STR(r.bands, "00:00-01:00 heat 20; 01:00-02:00 heat 20.5; 02:00-03:00 cool 25");
  // HA has the same bands in a different list order and with the temperature as 20.500000001 (float noise)
  EditInput in = input({U(120, 180, 2, 25), U(60, 120, 1, 20.5), U(0, 60, 1, 20)}, "mon;3;r2;");
  in.lists.temp[1] = 20.5;
  CHECK(s.on_state(in, 10) == EditEvent::SAVED);
  // a merged version of equal neighbours is also the same content
  EditSession t = open_session({U(0, 60, 1, 20), U(120, 180, 2, 25)});
  t.model().select(1);
  t.model().move_start(-60);
  t.model().cycle_mode();   // cool -> off
  t.model().cycle_mode();   // off -> heat 20
  t.model().cycle_mode();
  t.model().cycle_mode();
  t.model().cycle_mode();
  CHECK(t.press_save(0).send);
  CHECK(t.on_state(input({U(0, 60, 1, 20), U(60, 180, 1, 20)}, "mon;3;r2;"), 1) == EditEvent::SAVED);
}

TEST(saved_ignores_different_content) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  CHECK(s.press_save(0).send);
  // different content, same revision, no error: nothing to say yet... but the bands differ from the base: changed elsewhere
  EditEvent e = s.on_state(input({U(60, 120, 1, 22)}, "mon;3;r1;"), 5);
  CHECK(e == EditEvent::SAVE_FAILED);
  CHECK(s.state() == SaveState::IDLE);
  CHECK(s.notice() == Notice::CHANGED_ELSEWHERE);
  CHECK(s.model().band(0)->centi == 2200);   // the new bands are loaded
  CHECK(!s.model().dirty());
}

TEST(timeout_after_five_seconds) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  CHECK(s.press_save(1000).send);
  CHECK(!s.tick(1001));
  CHECK(!s.tick(5999));
  CHECK(s.state() == SaveState::SAVING);
  CHECK(s.tick(6000));
  CHECK(s.state() == SaveState::TIMEOUT);
  CHECK_STR(s.error(), "");   // plain "Not saved"
  CHECK(!s.tick(6001));       // only once
  CHECK(s.model().dirty());   // the copy is still there
  CHECK(!s.model().frozen());
  CHECK(!s.press_save(7000).send);   // this tap acknowledges ...
  CHECK(s.state() == SaveState::IDLE);
  CHECK(s.press_save(7001).send);    // ... and the next one tries again
}

TEST(timeout_across_the_millisecond_wrap) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  const uint32_t t0 = 0xFFFFFF00u;
  CHECK(s.press_save(t0).send);
  CHECK(!s.tick(t0 + 4999u));
  CHECK(s.tick(t0 + 5000u));
}

TEST(late_arrival_after_timeout_is_saved) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  s.press_save(0);
  s.tick(5000);
  CHECK(s.state() == SaveState::TIMEOUT);
  CHECK(s.on_state(input({U(60, 120, 1, 20.5)}, "mon;3;r2;"), 6000) == EditEvent::SAVED);
  CHECK(s.state() == SaveState::SAVED);
  // but when the person has edited again after the timeout, a late arrival is just a change from elsewhere
  EditSession t = open_session({U(60, 120, 1, 20)});
  t.model().select(0);
  t.model().step_temp(1);
  t.press_save(0);
  t.tick(5000);
  t.model().step_temp(2);
  CHECK(t.on_state(input({U(60, 120, 1, 20.5)}, "mon;3;r2;"), 6000) == EditEvent::CHANGED_ELSEWHERE);
}

TEST(error_from_last_error) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  CHECK(s.press_save(0).send);
  EditEvent e = s.on_state(input({U(60, 120, 1, 20)}, "mon;3;r1;invalid_bands"), 100);
  CHECK(e == EditEvent::SAVE_FAILED);
  CHECK(s.state() == SaveState::ERROR);
  CHECK_STR(s.error(), "invalid_bands");
  CHECK(s.model().dirty());   // the work is kept
  CHECK(!s.model().frozen());
  CHECK(!s.tick(100000));     // no timeout in ERROR
  CHECK(!s.press_save(200).send);   // acknowledges
  CHECK(s.state() == SaveState::IDLE);
  CHECK_STR(s.error(), "");
  CHECK(s.press_save(300).send);
  // each documented code is carried
  const char *codes[] = {"stale_day", "conflict", "temperature_out_of_range", "day_locked", "invalid_bands"};
  for (const char *c : codes) {
    EditSession q = open_session({U(60, 120, 1, 20)});
    q.model().select(0);
    q.model().step_temp(1);
    q.press_save(0);
    q.on_state(input({U(60, 120, 1, 20)}, (std::string("mon;3;r1;") + c).c_str()), 1);
    CHECK(q.state() == SaveState::ERROR);
    CHECK_STR(q.error(), c);
  }
}

TEST(old_last_error_is_not_this_save_error) {
  // the error of an earlier attempt is already there when we send: it is not the answer to this save
  EditSession s = open_session({U(60, 120, 1, 20)}, "mon;3;r1;invalid_bands");
  s.model().select(0);
  s.model().step_temp(1);
  CHECK(s.press_save(0).send);
  CHECK(s.on_state(input({U(60, 120, 1, 20)}, "mon;3;r1;invalid_bands"), 100) == EditEvent::NONE);
  CHECK(s.state() == SaveState::SAVING);
  CHECK(s.tick(5000));
  CHECK(s.state() == SaveState::TIMEOUT);
  // a different code is the answer
  EditSession t = open_session({U(60, 120, 1, 20)}, "mon;3;r1;invalid_bands");
  t.model().select(0);
  t.model().step_temp(1);
  t.press_save(0);
  CHECK(t.on_state(input({U(60, 120, 1, 20)}, "mon;3;r1;conflict"), 100) == EditEvent::SAVE_FAILED);
  CHECK_STR(t.error(), "conflict");
  // an error while idle is remembered as the base for the next save
  EditSession u = open_session({U(60, 120, 1, 20)});
  u.on_state(input({U(60, 120, 1, 20)}, "mon;3;r1;stale_day"), 1);
  CHECK(u.state() == SaveState::IDLE);
  u.model().select(0);
  u.model().step_temp(1);
  u.press_save(2);
  CHECK(u.on_state(input({U(60, 120, 1, 20)}, "mon;3;r1;stale_day"), 3) == EditEvent::NONE);
}

TEST(saved_beats_a_leftover_error) {
  EditSession s = open_session({U(60, 120, 1, 20)}, "mon;3;r1;invalid_bands");
  s.model().select(0);
  s.model().step_temp(1);
  s.press_save(0);
  CHECK(s.on_state(input({U(60, 120, 1, 20.5)}, "mon;3;r2;"), 10) == EditEvent::SAVED);
}

TEST(save_blocked_by_locked_band) {
  EditSession s = open_session({U(60, 120, 1, 20, "", 1), U(120, 180, 1, 21)});
  s.model().select(1);
  CHECK(s.model().step_temp(1).changed);
  CHECK(s.model().dirty());
  CHECK(!s.can_save());
  CHECK(s.save_block() == SaveBlock::LOCKED);
  CHECK(!s.press_save(0).send);
  CHECK(s.state() == SaveState::IDLE);
}

TEST(save_blocked_when_not_opened) {
  EditSession s;
  CHECK(!s.opened());
  CHECK(!s.press_save(0).send);
  CHECK(!s.tick(10000));
  CHECK(s.on_state(input({U(0, 60, 1, 20)}), 0) == EditEvent::NONE);
  CHECK(!s.can_save());
  s.acknowledge();
}

TEST(unreadable_info_cannot_save) {
  EditSession s;
  EditInput in = input({U(60, 120, 1, 20)}, "garbage");
  s.open(in, RNG, 4);
  s.model().select(0);
  s.model().step_temp(1);
  CHECK(s.model().dirty());
  CHECK(s.seen_kind().empty());
  CHECK(!s.can_save());
  CHECK(!s.press_save(0).send);
  CHECK(s.state() == SaveState::IDLE);
}

TEST(model_is_frozen_while_saving) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  s.press_save(0);
  CHECK(s.model().step_temp(1).msg == Msg::EDIT_BLOCKED);
  CHECK(s.model().step_temp(1).changed == false);
  s.on_state(input({U(60, 120, 1, 20.5)}, "mon;3;r2;"), 1);
  CHECK(!s.model().frozen());
  CHECK(s.model().step_temp(1).changed);
}

TEST(changed_elsewhere_when_dirty) {
  EditSession s = open_session({U(60, 120, 1, 20), U(300, 400, 1, 22)});
  s.model().select(1);
  s.model().step_temp(1);
  CHECK(s.model().dirty());
  EditEvent e = s.on_state(input({U(60, 120, 1, 20), U(300, 400, 1, 22), U(500, 600, 1, 18)}, "mon;3;r2;"), 10);
  CHECK(e == EditEvent::CHANGED_ELSEWHERE);
  CHECK(s.notice() == Notice::CHANGED_ELSEWHERE);
  CHECK(s.model().size() == 3);
  CHECK(!s.model().dirty());
  CHECK(s.model().selected() == -1);   // back to the list
  CHECK_STR(s.seen_revision(), "r2");
  s.acknowledge();
  CHECK(s.notice() == Notice::NONE);
}

TEST(revision_change_alone_with_dirty_copy) {
  // another day of the program was changed (same bands today): the revision moved; the copy has changes
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  CHECK(s.on_state(input({U(60, 120, 1, 20)}, "mon;3;r9;"), 1) == EditEvent::CHANGED_ELSEWHERE);
  CHECK(!s.model().dirty());
  CHECK_STR(s.seen_revision(), "r9");
}

TEST(silent_reload_when_not_dirty) {
  EditSession s = open_session({U(60, 120, 1, 20), U(300, 400, 1, 22)});
  s.model().select(1);
  EditEvent e = s.on_state(input({U(60, 120, 1, 21), U(300, 400, 1, 22)}, "mon;3;r2;"), 10);
  CHECK(e == EditEvent::RELOADED);
  CHECK(s.notice() == Notice::NONE);
  CHECK(s.model().band(0)->centi == 2100);
  CHECK(s.model().selected() == 1);   // stays on the row
  CHECK_STR(s.seen_revision(), "r2");
  // revision change with the same bands: also silent
  CHECK(s.on_state(input({U(60, 120, 1, 21), U(300, 400, 1, 22)}, "mon;3;r3;"), 20) == EditEvent::RELOADED);
  // the row is gone: selection clamps
  CHECK(s.on_state(input({U(60, 120, 1, 21)}, "mon;3;r4;"), 30) == EditEvent::RELOADED);
  CHECK(s.model().selected() == 0);
  // nothing changed: nothing
  CHECK(s.on_state(input({U(60, 120, 1, 21)}, "mon;3;r4;"), 40) == EditEvent::NONE);
  // all rows gone
  CHECK(s.on_state(input({}, "mon;3;r5;"), 50) == EditEvent::RELOADED);
  CHECK(s.model().selected() == -1);
}

TEST(same_content_new_revision_text_does_not_disturb) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  // attributes unrelated to the bands change often (slot, next...): nothing happens
  for (int i = 0; i < 5; ++i) CHECK(s.on_state(input({U(60, 120, 1, 20), A(0, 60)}, "mon;3;r1;"), i) == EditEvent::NONE);
}

TEST(day_changed) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);   // dirty
  EditEvent e = s.on_state(input({U(0, 600, 1, 18)}, "tue;3;r1;"), 10);
  CHECK(e == EditEvent::DAY_CHANGED);
  CHECK(s.notice() == Notice::DAY_CHANGED);
  CHECK_STR(s.seen_kind(), "tue");
  CHECK(s.model().size() == 1 && s.model().band(0)->centi == 1800);
  CHECK(!s.model().dirty());
  CHECK(s.model().selected() == -1);
  CHECK(s.state() == SaveState::IDLE);
  s.acknowledge();
  CHECK(s.notice() == Notice::NONE);
  // a holiday
  CHECK(s.on_state(input({U(0, 600, 1, 18)}, "holiday;3;r1;"), 20) == EditEvent::DAY_CHANGED);
}

TEST(day_changed_while_saving) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  s.press_save(0);
  CHECK(s.on_state(input({U(60, 120, 1, 20)}, "tue;3;r1;"), 1) == EditEvent::DAY_CHANGED);
  CHECK(s.state() == SaveState::IDLE);
  CHECK(!s.model().frozen());
  CHECK(!s.tick(100000));   // no timeout left over
}

TEST(invalid_info_does_not_look_like_a_new_day) {
  // an unreadable edit_info (the attribute is missing for a moment) is not a day change and not a revision change
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  CHECK(s.on_state(input({U(60, 120, 1, 20)}, ""), 1) == EditEvent::NONE);
  CHECK(s.model().dirty());
}

TEST(save_base_revision_is_the_one_seen_at_opening) {
  EditSession s = open_session({U(60, 120, 1, 20)}, "mon;2;rev-7;");
  s.model().select(0);
  s.model().step_temp(1);
  CHECK_STR(s.press_save(0).base_revision, "rev-7");
}

TEST(after_saved_a_new_save_uses_the_new_revision) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  s.press_save(0);
  s.on_state(input({U(60, 120, 1, 20.5)}, "mon;3;r2;"), 1);
  s.press_save(2);   // acknowledge
  s.model().step_temp(1);
  SaveRequest r = s.press_save(3);
  CHECK(r.send);
  CHECK_STR(r.base_revision, "r2");
  CHECK_STR(r.bands, "01:00-02:00 heat 21");
}

TEST(acknowledge_does_not_cancel_a_save) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  s.press_save(0);
  s.acknowledge();
  CHECK(s.state() == SaveState::SAVING);
}

TEST(close_resets) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  s.press_save(0);
  s.close();
  CHECK(!s.opened());
  CHECK(s.state() == SaveState::IDLE);
  CHECK(!s.model().frozen());
  CHECK(!s.tick(100000));
}

TEST(open_resets_everything) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  s.press_save(0);
  s.tick(5000);
  s.open(input({U(0, 60, 1, 18)}, "wed;2;x;"), RNG, 6);
  CHECK(s.state() == SaveState::IDLE && s.notice() == Notice::NONE);
  CHECK(s.model().size() == 1 && !s.model().dirty());
  CHECK(s.model().rows_per_page() == 6);
  CHECK_STR(s.seen_kind(), "wed");
  CHECK(s.rooms() == 2);
}

TEST(empty_day_save) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().remove();
  SaveRequest r = s.press_save(0);
  CHECK(r.send);
  CHECK_STR(r.bands, "");
  CHECK(s.on_state(input({A(0, 1440)}, "mon;3;r2;"), 1) == EditEvent::SAVED);
}

TEST(damaged_arrival_is_not_saved) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().remove();   // sent: an empty day
  s.press_save(0);
  // HA brings broken lists that happen to hold no user bands: not accepted as the answer
  EditInput in = input({});
  in.info = parse_edit_info("mon;3;r2;");
  in.info.n = 0;
  in.lists.start.push_back(0);   // different lengths
  CHECK(s.on_state(in, 1) != EditEvent::SAVED);
}


TEST(minimum_duration_is_fifteen_not_one) {
  EditModel m = model_of({U(100, 125, 1, 20)});   // 25 minutes
  m.select(0);
  CHECK(m.move_start(15).msg == Msg::EDIT_BLOCKED);   // would leave 10
  CHECK(m.move_end(-15).msg == Msg::EDIT_BLOCKED);
  CHECK(m.move_start(10).changed);                   // leaves exactly 15
  CHECK(m.band(0)->start == 110);
  EditModel e = model_of({U(100, 125, 1, 20)});
  e.select(0);
  CHECK(e.move_end(-10).changed);
  CHECK(e.band(0)->end == 115);
}

TEST(mode_off_to_heat_prefers_the_band_before_at_equal_distance) {
  EditModel m = model_of({U(0, 60, 1, 18), U(60, 120, 0, -100), U(120, 180, 1, 25)});
  m.select(1);
  CHECK(m.cycle_mode().changed);
  CHECK(m.band(1)->centi == 1800);
}

TEST(text_length_boundary_960) {
  std::vector<EditBand> v;
  int total = 0;
  for (int i = 0; i < 16; ++i) {
    EditBand b;
    b.start = i * 60;
    b.end = i * 60 + 30;
    b.mode = Mode::HEAT;
    b.preset = std::string(34, 'x');
    v.push_back(b);
  }
  // each band is 24 + L characters, "; " between them. L=34 everywhere: 16*58+30 = 958.
  for (const EditBand &b : v) total += 24 + (int) b.preset.size();
  total += 2 * 15;
  CHECK(total == 958);
  CHECK(encode_bands(v).ok);
  CHECK(encode_bands(v).text.size() == 958);
  v[0].preset += "y";   // 959
  v[1].preset += "y";   // 960
  EncodeResult e = encode_bands(v);
  CHECK(e.ok && e.text.size() == 960);
  v[2].preset += "y";   // 961
  e = encode_bands(v);
  CHECK(!e.ok);
  CHECK_STR(e.error, "too_long");
}

TEST(press_save_after_result_never_sends_in_the_same_tap) {
  for (int how = 0; how < 3; ++how) {
    EditSession s = open_session({U(60, 120, 1, 20)});
    s.model().select(0);
    s.model().step_temp(1);
    s.press_save(0);
    if (how == 0) s.on_state(input({U(60, 120, 1, 20.5)}, "mon;3;r2;"), 1);          // SAVED
    if (how == 1) s.on_state(input({U(60, 120, 1, 20)}, "mon;3;r1;conflict"), 1);   // ERROR
    if (how == 2) s.tick(5000);                                                       // TIMEOUT
    s.model().step_temp(1);   // there is something to save in each case
    CHECK(!s.press_save(6000).send);
    CHECK(s.state() == SaveState::IDLE);
  }
}


// ---------------------------------------------------------------- review of d2c98ef

static EditInput with_seq(EditInput in, int64_t seq) {
  in.info.seq = seq;
  return in;
}

TEST(repeated_refusals_with_the_same_code_are_errors_each_time) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  CHECK(s.press_save(0).send);
  CHECK(s.on_state(with_seq(input({U(60, 120, 1, 20)}, "mon;3;r1;invalid_bands"), 1), 10) == EditEvent::SAVE_FAILED);
  CHECK(s.state() == SaveState::ERROR);
  s.acknowledge();
  CHECK(s.press_save(100).send);   // the same copy again
  // the same code, but the counter rose: a new refusal
  CHECK(s.on_state(with_seq(input({U(60, 120, 1, 20)}, "mon;3;r1;invalid_bands"), 2), 110) == EditEvent::SAVE_FAILED);
  CHECK(s.state() == SaveState::ERROR);
  CHECK_STR(s.error(), "invalid_bands");
  s.acknowledge();
  CHECK(s.press_save(200).send);
  // the same code and the same counter: it is the old refusal
  CHECK(s.on_state(with_seq(input({U(60, 120, 1, 20)}, "mon;3;r1;invalid_bands"), 2), 210) == EditEvent::NONE);
  CHECK(s.state() == SaveState::SAVING);
}

TEST(residual_error_of_an_earlier_session_is_not_a_refusal) {
  EditSession s;
  s.open(with_seq(input({U(60, 120, 1, 20)}, "mon;3;r1;conflict"), 7), RNG, 4);
  s.model().select(0);
  s.model().step_temp(1);
  CHECK(s.press_save(0).send);
  CHECK(s.on_state(with_seq(input({U(60, 120, 1, 20)}, "mon;3;r1;conflict"), 7), 10) == EditEvent::NONE);
  CHECK(s.state() == SaveState::SAVING);
  CHECK(s.tick(5000));
  CHECK(s.state() == SaveState::TIMEOUT);
  // a counter that went DOWN (restart of Home Assistant) with the same code is also a new fact
  EditSession u;
  u.open(with_seq(input({U(60, 120, 1, 20)}, "mon;3;r1;conflict"), 7), RNG, 4);
  u.model().select(0);
  u.model().step_temp(1);
  u.press_save(0);
  CHECK(u.on_state(with_seq(input({U(60, 120, 1, 20)}, "mon;3;r1;conflict"), 1), 10) == EditEvent::SAVE_FAILED);
}

TEST(unreadable_info_changes_nothing) {
  // idle, dirty
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  for (const char *bad : {"", "junk", "mon;3;r1", "funday;3;r1;", "mon;3;r1;;"}) {
    EditInput in = input({U(0, 600, 1, 18)}, bad);   // other bands too: still nothing happens
    CHECK(s.on_state(in, 1) == EditEvent::NONE);
    CHECK(s.model().dirty() && s.model().size() == 1 && s.model().selected() == 0);
    CHECK(s.notice() == Notice::NONE && s.state() == SaveState::IDLE);
  }
  // unavailable: empty lists, no info
  EditInput gone;
  CHECK(s.on_state(gone, 2) == EditEvent::NONE);
  CHECK(s.model().dirty() && s.model().size() == 1);
  // during SAVING: neither failure nor change elsewhere; the timeout still ends it
  CHECK(s.press_save(10).send);
  CHECK(s.on_state(gone, 11) == EditEvent::NONE);
  CHECK(s.on_state(input({U(0, 600, 1, 18)}, "x"), 12) == EditEvent::NONE);
  CHECK(s.state() == SaveState::SAVING && s.model().frozen());
  CHECK(s.model().band(0)->centi == 2050);
  CHECK(s.tick(5010));
  CHECK(s.state() == SaveState::TIMEOUT);
  // and the arrival of what was sent is still recognised afterwards
  CHECK(s.on_state(input({U(60, 120, 1, 20.5)}, "mon;3;r2;"), 6000) == EditEvent::SAVED);
}

TEST(cut_lists_are_damaged) {
  // the core cut every list to 3 entries but edit_n says 5
  EditInput in = input({U(0, 60, 1, 20), U(60, 120, 1, 21), U(120, 180, 1, 22)});
  in.info.n = 5;
  EditModel m;
  m.load(in);
  CHECK(m.damaged());
  CHECK(m.save_block() == SaveBlock::DAMAGED);
  in.info.n = 3;
  m.load(in);
  CHECK(!m.damaged());
  in.info.n = 2;   // more entries than announced
  m.load(in);
  CHECK(m.damaged());
  in.info.n = -1;  // attribute absent, info valid
  m.load(in);
  CHECK(m.damaged());
  in.info.n = 0;   // 0 is a real count, not "absent"
  m.load(in);
  CHECK(m.damaged());
  EditInput empty = input({});
  EditModel e;
  e.load(empty);
  CHECK(!e.damaged());   // 0 entries, edit_n 0
  empty.info.n = -1;
  e.load(empty);
  CHECK(e.damaged());
  empty.info = parse_edit_info("nonsense");   // info not valid: its absence is the problem, not the count
  empty.info.n = -1;
  e.load(empty);
  CHECK(!e.damaged());
  // a cut day cannot be saved
  EditSession s;
  EditInput cut = input({U(0, 60, 1, 20)});
  cut.info.n = 4;
  s.open(cut, RNG, 4);
  s.model().select(0);
  s.model().step_temp(1);
  CHECK(!s.can_save());
  CHECK(!s.press_save(0).send);
}

TEST(max_text_for_counts_every_field) {
  // climate_entity_id + id, day_kind + kind, bands, base_revision + revision
  CHECK(max_text_for("climate.soggiorno", "mon", "r1") == 1024 - (17 + 17 + 8 + 3 + 5 + 13 + 2));
  CHECK(max_text_for("climate.soggiorno", "holiday", "r1") == 1024 - (17 + 17 + 8 + 7 + 5 + 13 + 2));
  CHECK(max_text_for("c", "mon", "") == 1024 - (17 + 1 + 8 + 3 + 5));   // no revision: no such field
  CHECK(max_text_for("", "mon", "r1") == 1024 - (17 + 255 + 8 + 3 + 5 + 13 + 2));   // unknown id: the longest
  CHECK(max_text_for(std::string(2000, 'x'), "mon", "r1") == 0);
  CHECK(max_text_for("climate.a", "mon", std::string(64, 'r')) == 1024 - (17 + 9 + 8 + 3 + 5 + 13 + 64));
  // against the real rule of the action: the sum of key + value of every field must be <= 1024
  const std::string id = "climate.camera_da_letto", kind = "tue", rev = "abcdef-12";
  const size_t cap = max_text_for(id, kind, rev);
  const size_t total = std::string("climate_entity_id").size() + id.size() + std::string("day_kind").size() + kind.size() +
                       std::string("bands").size() + cap + std::string("base_revision").size() + rev.size();
  CHECK(total == 1024);
}

TEST(encode_ceiling_is_a_parameter) {
  std::vector<EditBand> v;
  for (int i = 0; i < 3; ++i) {
    EditBand b;
    b.start = i * 60;
    b.end = i * 60 + 30;
    b.mode = Mode::HEAT;
    b.has_temp = true;
    b.centi = 2000;
    v.push_back(b);
  }
  const std::string text = encode_bands(v).text;   // "00:00-00:30 heat 20; ..." 
  CHECK(text.size() == 3 * 19 + 2 * 2);
  CHECK(encode_bands(v, text.size()).ok);
  EncodeResult e = encode_bands(v, text.size() - 1);
  CHECK(!e.ok);
  CHECK_STR(e.error, "too_long");
  CHECK(!encode_bands(v, 0).ok);
  CHECK(encode_bands(std::vector<EditBand>(), 0).ok);   // the empty text always fits
}

TEST(session_ceiling_follows_the_room_and_the_revision) {
  // a day that fits with a short revision but not with a 64-character one, for a long entity id
  std::vector<S> v;
  for (int i = 0; i < 16; ++i) v.push_back(U(i * 60, i * 60 + 30, 1, 20.5, std::string(30, 'p')));
  const std::string id = "climate." + std::string(100, 'x');
  const std::string rev = std::string(64, 'r');
  EditSession s;
  s.open(input(v, ("mon;3;" + rev + ";").c_str()), RNG, 4, id);
  const size_t cap = max_text_for(id, "mon", rev);
  CHECK(s.model().max_text() == cap);
  s.model().select(0);
  s.model().move_end(-15);
  const size_t len = s.model().encode().text.size() + (s.model().encode().ok ? 0 : 0);
  (void) len;
  EditSession t;
  t.open(input(v, "mon;3;r;"), RNG, 4, "climate.a");
  t.model().select(0);
  t.model().move_end(-15);
  CHECK(t.model().max_text() > cap);
  // exactly at the ceiling passes, one more does not
  std::vector<EditBand> bands = t.model().bands();
  const size_t n = encode_bands(bands, 100000).text.size();
  t.model().set_max_text(n);
  CHECK(t.model().encode().ok);
  t.model().set_max_text(n - 1);
  CHECK(!t.model().encode().ok);
  CHECK(t.model().save_block() == SaveBlock::TOO_LONG);
  // after a change of revision the ceiling is recomputed
  EditSession u;
  u.open(input({U(60, 120, 1, 20)}, "mon;3;r;"), RNG, 4, "climate.a");
  const size_t before = u.model().max_text();
  u.on_state(input({U(60, 120, 1, 21)}, ("mon;3;" + std::string(40, 'r') + ";").c_str()), 1);
  CHECK(u.model().max_text() == before - 39);
  // unknown climate id: the conservative ceiling
  EditSession w = open_session({U(60, 120, 1, 20)});
  CHECK(w.model().max_text() == max_text_for("", "mon", "r1"));
  CHECK(w.model().max_text() < EDIT_MAX_TEXT);
}

TEST(send_failed_ends_in_not_sent) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  CHECK(!s.send_failed());   // nothing under way
  CHECK(s.press_save(0).send);
  CHECK(s.send_failed());
  CHECK(s.state() == SaveState::ERROR);
  CHECK_STR(s.error(), "not_sent");
  CHECK(!s.model().frozen());
  CHECK(s.model().dirty());
  CHECK(!s.send_failed());   // once
  CHECK(!s.tick(100000));    // no timeout afterwards
  CHECK(!s.press_save(5).send);   // acknowledged by this tap
  CHECK(s.state() == SaveState::IDLE);
  CHECK(s.press_save(6).send);    // and it can be tried again
  EditSession c;
  CHECK(!c.send_failed());
}

TEST(page_survives_a_reload_without_selection) {
  std::vector<S> v;
  for (int i = 0; i < 8; ++i) v.push_back(U(i * 100, i * 100 + 50, 1, 20));
  EditSession s;
  s.open(input(v), RNG, 3);
  CHECK(s.model().next_page());
  CHECK(s.model().page() == 1);
  v[0].temp = 21;
  CHECK(s.on_state(input(v, "mon;3;r2;"), 1) == EditEvent::RELOADED);
  CHECK(s.model().page() == 1);
  // the page is gone: clamped
  std::vector<S> few(v.begin(), v.begin() + 2);
  CHECK(s.on_state(input(few, "mon;3;r3;"), 2) == EditEvent::RELOADED);
  CHECK(s.model().page() == 0);
  // a new session starts on the first page
  CHECK(s.model().size() == 2);
  s.open(input(v), RNG, 3);
  s.model().next_page();
  CHECK(s.model().page() == 1);
  s.open(input(v), RNG, 3);
  CHECK(s.model().page() == 0);
  // a new day: first page
  s.model().next_page();
  CHECK(s.on_state(input(v, "tue;3;r3;"), 3) == EditEvent::DAY_CHANGED);
  CHECK(s.model().page() == 0);
}

TEST(select_last_row_of_a_page) {
  std::vector<S> v;
  for (int i = 0; i < 7; ++i) v.push_back(U(i * 100, i * 100 + 50, 1, 20));
  EditModel m = model_of(v);
  m.set_rows_per_page(3);
  CHECK(m.select(2));
  CHECK(m.page() == 0);   // the last row of page 0 stays on page 0
  CHECK(m.select(3));
  CHECK(m.page() == 1);   // the first of page 1
  CHECK(m.select(5));
  CHECK(m.page() == 1);
  CHECK(m.select(6));
  CHECK(m.page() == 2);
  CHECK(m.page_first() == 6 && m.page_rows() == 1);
}

TEST(tick_across_the_wrap_does_not_expire_early) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  CHECK(s.press_save(0xFFFFFF00u).send);
  CHECK(!s.tick(0xFFFFFFF0u));   // 240 ms later
  CHECK(!s.tick(0x00000010u));   // after the wrap: 272 ms
  CHECK(s.state() == SaveState::SAVING);
  CHECK(!s.tick(0xFFFFFF00u + 4999u));
  CHECK(s.tick(0xFFFFFF00u + 5000u));
}

TEST(open_after_error_clears_it) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  s.press_save(0);
  s.on_state(input({U(60, 120, 1, 20)}, "mon;3;r1;conflict"), 1);
  CHECK(s.state() == SaveState::ERROR && s.error() == "conflict");
  s.open(input({U(60, 120, 1, 20)}), RNG, 4);
  CHECK(s.state() == SaveState::IDLE);
  CHECK_STR(s.error(), "");
  CHECK(!s.model().dirty());
}

TEST(step_temp_off_the_grid) {
  EditModel m = model_of({U(60, 120, 1, 20.3), U(120, 180, 1, 20.3), U(180, 240, 1, 20.3)});
  m.set_range(make_range(5, 35, 0.5));
  m.select(0);
  CHECK(m.step_temp(-1).changed);
  CHECK(m.band(0)->centi == 2000);   // 20.3 -> 20.5 -> 20.0
  m.select(1);
  CHECK(m.step_temp(1).changed);
  CHECK(m.band(1)->centi == 2100);
  m.select(2);
  CHECK(m.step_temp(-100).changed);
  CHECK(m.band(2)->centi == 500);
}

TEST(sent_is_normalized_and_the_merged_arrival_is_saved) {
  // two adjacent equal bands are sent merged; HA has them merged: SAVED. Unmerged: SAVED too.
  for (int merged = 0; merged < 2; ++merged) {
    EditSession s = open_session({U(0, 60, 1, 20), U(180, 240, 2, 25)});
    s.model().select(0);
    CHECK(s.model().add().changed);   // 60-120 heat 20: touches the first
    CHECK(s.model().size() == 3);
    SaveRequest r = s.press_save(0);
    CHECK(r.send);
    CHECK_STR(r.bands, "00:00-02:00 heat 20; 03:00-04:00 cool 25");
    EditInput in = merged ? input({U(0, 120, 1, 20), U(180, 240, 2, 25)}, "mon;3;r2;")
                          : input({U(0, 60, 1, 20), U(60, 120, 1, 20), U(180, 240, 2, 25)}, "mon;3;r2;");
    CHECK(s.on_state(in, 1) == EditEvent::SAVED);
    CHECK(!s.model().dirty());
  }
}

TEST(discard_unsaved_after_a_failed_delete) {
  const EditInput ha = input({U(60, 120, 1, 20), U(180, 240, 1, 21)});
  for (int how = 0; how < 3; ++how) {   // 0 refused by HA, 1 not sent, 2 timeout
    EditSession s = open_session({U(60, 120, 1, 20), U(180, 240, 1, 21)});
    s.model().select(0);
    CHECK(s.model().remove().changed);
    s.model().deselect();
    CHECK(s.press_save(0).send);
    CHECK(!s.discard_unsaved(ha));   // under way: nothing is dropped
    if (how == 0) s.on_state(input({U(60, 120, 1, 20), U(180, 240, 1, 21)}, "mon;3;r1;day_locked"), 1);
    if (how == 1) CHECK(s.send_failed());
    if (how == 2) CHECK(s.tick(5000));
    CHECK(s.model().size() == 1);
    CHECK(s.discard_unsaved(ha));
    CHECK(s.model().size() == 2);
    CHECK(!s.model().dirty());
    CHECK(!s.discard_unsaved(ha));
    CHECK(s.state() != SaveState::IDLE);
  }
}

TEST(reload_unfreezes_the_model) {
  EditSession s = open_session({U(60, 120, 1, 20)});
  s.model().select(0);
  s.model().step_temp(1);
  s.press_save(0);
  CHECK(s.model().frozen());
  // somebody else saved while we were saving
  CHECK(s.on_state(input({U(60, 120, 1, 23)}, "mon;3;r2;"), 1) == EditEvent::SAVE_FAILED);
  CHECK(!s.model().frozen());
  s.model().select(0);
  CHECK(s.model().step_temp(1).changed);
  // a day change while saving
  EditSession d = open_session({U(60, 120, 1, 20)});
  d.model().select(0);
  d.model().step_temp(1);
  d.press_save(0);
  d.on_state(input({U(60, 120, 1, 20)}, "tue;3;r1;"), 1);
  CHECK(!d.model().frozen());
}

TEST(edit_title_with_zero_rooms) {
  CHECK_STR(edit_title("Casa", 0), "Casa - 0 rooms");
  CHECK_STR(edit_title("", 0), "0 rooms");
  CHECK_STR(edit_title("Casa", 0, "stanza", "stanze"), "Casa - 0 stanze");
}

// ------------------------------------------------------------------------------------------------ panel_allowed

TEST(panel_allowed_rules) {
  TileInput ti;
  ti.climate_state = "heat";
  ti.has_room = true;
  ti.room_state = "Casa";
  ti.status = "ok";
  const EditInfo ok = parse_edit_info("mon;3;r1;");
  CHECK(panel_allowed(true, tile_view(ti), ok));
  CHECK(!panel_allowed(false, tile_view(ti), ok));                    // the option is off
  CHECK(!panel_allowed(true, tile_view(ti), parse_edit_info("")));    // no readable edit_info
  CHECK(!panel_allowed(true, tile_view(ti), parse_edit_info("mon;3;r1;;")));
  TileInput u = ti;
  u.climate_state = "unavailable";
  CHECK(!panel_allowed(true, tile_view(u), ok));
  u = ti;
  u.room_state = "unavailable";
  CHECK(!panel_allowed(true, tile_view(u), ok));
  u = ti;
  u.has_room = false;
  CHECK(!panel_allowed(true, tile_view(u), ok));
  u = ti;
  u.status = "paused";
  CHECK(!panel_allowed(true, tile_view(u), ok));
  u = ti;
  u.status = "unassigned";
  CHECK(!panel_allowed(true, tile_view(u), ok));
  u = ti;
  u.room_state = "unknown";
  CHECK(!panel_allowed(true, tile_view(u), ok));
  u = ti;
  u.climate_state = "off";   // the thermostat is off: the bands can still be edited
  CHECK(panel_allowed(true, tile_view(u), ok));
  u = ti;
  u.manual = true;
  CHECK(panel_allowed(true, tile_view(u), ok));
  CHECK(panel_allowed(true, TileView(), ok) == false);
}

int main() {
  (void) R;
  for (auto &t : tests()) {
    g_test = t.name;
    ++g_tests;
    t.fn();
  }
  std::printf("%d tests, %d checks, %d failed\n", g_tests, g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
