#pragma once
// Geometry and texts of the edit panel of the card (plan 4, step D): pure C++17, no ESPHome, no LVGL, so it builds and runs
// on a computer (tests/test_pad.cpp, tests/run.sh). The card (cronotermostato.cpp) turns it into blocks and labels.
//
// What it does:
//   - make_pad()     where every key of the three views of the card stands: READ (the bands, with the key "Edit"),
//                    LIST (one row per user band, a header, a footer with < > +) and DETAIL (start, end, mode and
//                    temperature, Delete / Save / Back). Rows per page follow the real height.
//   - pad_zone_at()  which key a tap at (x, y) hits. The zones of one view never overlap and are the key's rectangle grown
//                    by half the gap, so a finger between two keys still hits one of them, never both.
//   - the confirmation of Delete (view CONFIRM): the band is shown with two big keys, Cancel at the left and Confirm at the
//                    right, a wide gap apart. Confirm never stands where Delete stood and ignores the first 600 ms (a double
//                    tap on Delete cannot confirm); confirm_tap(), delete_gate(), confirm_survives() and the texts are here so
//                    they are tested on a computer.
//   - the texts      which translation key says a message, a notice, a refusal or the state of Save, in a long form and a
//                    short one (the card picks the one that fits the room it has).
// A tap on a screen cannot be told apart from where the finger was: the core gives one on_tap per card and
// tessera::tap_point(), so the card tests the point against these zones.
#include <algorithm>
#include <cstdint>
#include <string>

#include "cronotermostato_edit.h"

namespace esphome::cronotermostato {

// ---------------------------------------------------------------------------------------------------------------
// Rectangles and zones

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
  int right() const { return x + w; }
  int bottom() const { return y + h; }
  // The point is inside the rectangle grown by `grow` pixels on every side (the edges on the left/top count, right/bottom not).
  bool has(int px, int py, int grow = 0) const {
    return !empty() && px >= x - grow && px < x + w + grow && py >= y - grow && py < y + h + grow;
  }
};

enum class EView { READ, LIST, DETAIL, CONFIRM };
enum class EZ {
  NONE,
  OPEN,                                  // READ: the key "Edit"
  ROW,                                   // LIST: a row (index = row on the page)
  PREV, NEXT, ADD,                       // LIST: footer
  S_M60, S_M15, S_P15, S_P60,            // DETAIL: start -1h -15 +15 +1h
  E_M60, E_M15, E_P15, E_P60,            // DETAIL: end
  MODE, TEMP_M, TEMP_P,                  // DETAIL: mode (cycles), temperature - and +
  DELETE, SAVE, BACK,                    // DETAIL: footer
  CONFIRM_NO, CONFIRM_YES,               // CONFIRM: Cancel (left) and Confirm (right)
};
struct EZone {
  EZ kind = EZ::NONE;
  int index = -1;   // the row of a ROW
  bool operator==(const EZone &o) const { return kind == o.kind && index == o.index; }
};

// What the card measured. Heights and widths in pixels of the card's room.
struct PadInput {
  int w = 0, h = 0;
  int key_h = 0;        // the wanted height of a key: 7 mm, or the line of text and some air if that is more
  int key_min_w = 0;    // the narrowest key: 7 mm
  int gap = 2;          // between keys
  int line_h = 0;       // a line of BODY text
  int foot_w = 0;       // the wanted width of < > + (the footer of the list)
  int cap_w = 0;        // width of the widest "Start 88:88" (caption and time); 0: no caption fits anyway
  int time_w = 0;       // width of "88:88"
  int mode_w = 0;       // the mode key: the widest mode word and its margin
  int open_w = 0;       // the key "Edit": its word and its margin
  int key_max_w = 0;    // the widest key of the detail (0: no limit)
  int key_max_h = 0;    // the tallest key of the detail (0: no limit)
  int wide_gap = 0;     // between Cancel and Confirm (0: four times `gap`); never more than a third of the width
};

constexpr int PAD_MAX_ROWS = (int) EDIT_MAX_ROWS;

struct PadLayout {
  PadInput in;
  int gap = 2;
  // READ: the header carries the key "Edit" at its right; the bands start under `read_top`.
  Rect read_head;      // the header line (the program's name) to the left of the key
  Rect open;           // the key
  int read_top = 0;
  // LIST
  bool list_header = false;
  Rect list_head;      // title / status line (empty without a header)
  int rows = 0;        // rows per page (EDIT_MIN_ROWS..EDIT_MAX_ROWS)
  int row_h = 0;
  Rect row[PAD_MAX_ROWS];
  Rect prev, next, add, info;   // the footer: `info` is the page and, without a header, the title or the message
  bool list_small = false;      // keys under 7 mm: the screen is too short for more
  // DETAIL
  bool detail_header = false;
  Rect detail_head;
  Rect s_cap, e_cap;                // the captions "Start 06:00" / "End 07:00"
  Rect s_key[4], e_key[4];          // -1h -15 +15 +1h
  Rect mode_key, temp_val, temp_m, temp_p;
  Rect del, save, back;
  bool captions = false;            // the caption has room for the word and the time (else the time alone)
  int detail_key_h = 0;
  bool detail_small = false;        // keys under 7 mm
  int detail_bottom = 0;
  bool detail_fits = false;
  // CONFIRM: up to three lines of text (the question, the band, the warning of a shared program), the two keys under them
  int conf_gap = 0;                 // between Cancel and Confirm
  int conf_key_h = 0;
  bool conf_small = false;          // keys under 7 mm
  int conf_lines = 0;               // lines of text that fit (0..3)
  Rect conf_line[3];                // the lines, centred in the room over the keys
  Rect conf_no, conf_yes;           // Cancel at the left, Confirm at the right
};

// Everything is worked out from the input alone.
inline PadLayout make_pad(const PadInput &in_) {
  PadLayout L;
  L.in = in_;
  const PadInput &in = L.in;
  const int gap = std::max(1, in.gap);
  L.gap = gap;
  const int w = std::max(1, in.w), h = std::max(1, in.h);
  int key_h = std::max(1, in.key_h);

  // --- READ -----------------------------------------------------------------------------------------------------
  {
    const int ow = std::min(std::max(in.open_w, in.key_min_w), w);
    const int kh = std::min(key_h, h);
    L.open = {w - ow, 0, ow, kh};
    L.read_head = {0, 0, std::max(0, w - ow - gap), kh};
    L.read_top = kh + gap;
  }

  // --- LIST -----------------------------------------------------------------------------------------------------
  {
    int kh = key_h;
    const int hdr_h = in.line_h + 2 * gap;
    const int pitch0 = kh + gap;
    // With a header line if three rows still fit under it; else without (the footer then carries the words).
    int n_with = (h - (hdr_h + gap) - kh) / pitch0;
    int n_without = (h - kh) / pitch0;
    int top = 0;
    int n;
    if (n_with >= (int) EDIT_MIN_ROWS) {
      L.list_header = true;
      top = hdr_h + gap;
      n = n_with;
    } else {
      n = n_without;
      if (n < (int) EDIT_MIN_ROWS) {   // a screen too short: keys under 7 mm, three rows and the footer
        kh = std::max(1, (h - 3 * gap) / 4);
        n = (int) EDIT_MIN_ROWS;
        L.list_small = true;
      }
    }
    n = std::min(n, (int) EDIT_MAX_ROWS);
    L.rows = n;
    L.row_h = kh;
    if (L.list_header) L.list_head = {0, 0, w, hdr_h};
    const int pitch = kh + gap;
    for (int i = 0; i < n; ++i) L.row[i] = {0, top + i * pitch, w, kh};
    const int fy = top + n * pitch;
    const int fw = std::min(std::max(in.foot_w, in.key_min_w), std::max(1, (w - 2 * gap) / 4));
    L.prev = {0, fy, fw, kh};
    L.next = {fw + gap, fy, fw, kh};
    L.add = {w - fw, fy, fw, kh};
    L.info = {2 * (fw + gap), fy, std::max(0, w - fw - gap - 2 * (fw + gap)), kh};
  }

  // --- DETAIL ---------------------------------------------------------------------------------------------------
  {
    int top = 0;
    // A status line above the four rows when the keys at their wanted height leave room for it.
    if (h - (4 * key_h + 3 * gap) >= in.line_h + 2 * gap) {
      L.detail_header = true;
      top = in.line_h + 2 * gap;
      L.detail_head = {0, 0, w, in.line_h + 2 * gap};
    }
    int dk = (h - top - 3 * gap) / 4;
    if (in.key_max_h > 0) dk = std::min(dk, in.key_max_h);
    dk = std::max(dk, 1);
    L.detail_key_h = dk;
    L.detail_small = dk < key_h;
    // Horizontal: [caption (label)] [-1h] [-15] [+15] [+1h]
    const int kmin = std::max(1, in.key_min_w);
    const int lw_max = std::max(in.time_w, w - (4 * kmin + 4 * gap));
    L.captions = in.cap_w > 0 && in.cap_w + 2 * gap <= lw_max;
    int lw = L.captions ? in.cap_w + 2 * gap : in.time_w + 2 * gap;
    int kw = (w - lw - 4 * gap) / 4;
    if (in.key_max_w > 0 && kw > in.key_max_w) kw = in.key_max_w;
    kw = std::max(kw, 1);
    lw = std::max(1, w - 4 * kw - 4 * gap);   // the caption takes what the keys leave
    const int ys[4] = {top, top + dk + gap, top + 2 * (dk + gap), top + 3 * (dk + gap)};
    L.s_cap = {0, ys[0], lw, dk};
    L.e_cap = {0, ys[1], lw, dk};
    for (int i = 0; i < 4; ++i) {
      L.s_key[i] = {lw + gap + i * (kw + gap), ys[0], kw, dk};
      L.e_key[i] = {lw + gap + i * (kw + gap), ys[1], kw, dk};
    }
    L.temp_p = {w - kw, ys[2], kw, dk};
    L.temp_m = {w - 2 * kw - gap, ys[2], kw, dk};
    const int room = L.temp_m.x - gap;   // what the mode key and the value share
    int mw = std::max(in.mode_w, kw);
    mw = std::min(mw, std::max(1, room - gap - in.time_w));
    L.mode_key = {0, ys[2], mw, dk};
    L.temp_val = {mw + gap, ys[2], std::max(0, room - (mw + gap)), dk};
    const int fw = (w - 2 * gap) / 3;
    L.del = {0, ys[3], fw, dk};
    L.save = {fw + gap, ys[3], fw, dk};
    L.back = {2 * (fw + gap), ys[3], w - 2 * (fw + gap), dk};
    L.detail_bottom = ys[3] + dk;
    L.detail_fits = L.detail_bottom <= h;
  }

  // --- CONFIRM --------------------------------------------------------------------------------------------------
  {
    // Two text lines always have room (the band and the question or the warning); the keys give way before the text does.
    const int need_text = 2 * in.line_h + 2 * gap;
    int ck = key_h;
    if (h - ck - gap < need_text) ck = std::max(1, h - gap - need_text);
    L.conf_key_h = ck;
    L.conf_small = ck < key_h;
    int cg = in.wide_gap > 0 ? in.wide_gap : 4 * gap;
    cg = std::max(std::min(cg, w / 3), 2 * gap);
    L.conf_gap = cg;
    const int kw = std::max(1, (w - cg) / 2);
    L.conf_gap = std::max(0, w - 2 * kw);   // what the keys really leave (an odd width gives the gap one more pixel)
    const int ky = std::max(0, h - ck);
    L.conf_no = {0, ky, kw, std::min(ck, h)};
    L.conf_yes = {w - kw, ky, kw, std::min(ck, h)};
    const int pitch = in.line_h + 2 * gap;
    const int room = ky - gap;   // the height over the keys
    const int n = std::max(0, std::min(3, (room + 2 * gap) / std::max(1, pitch)));
    L.conf_lines = n;
    const int used = n > 0 ? n * pitch - 2 * gap : 0;
    const int top = std::max(0, (room - used) / 2);
    for (int i = 0; i < n; ++i) L.conf_line[i] = {0, top + i * pitch, w, in.line_h};
  }
  return L;
}

// The key a tap hits. `shown_rows`: how many rows the page shows (the others are empty and take no tap). The zones are the
// keys grown by half the gap, so they never overlap (neighbours stand one gap apart) and a finger between two keys hits one.
inline EZone pad_zone_at(const PadLayout &L, EView view, int shown_rows, int x, int y) {
  if (x < 0 || y < 0 || x >= L.in.w || y >= L.in.h) return {};
  const int g = L.gap / 2;
  auto hit = [&](const Rect &r) { return r.has(x, y, g); };
  switch (view) {
    case EView::READ:
      if (hit(L.open)) return {EZ::OPEN, -1};
      return {};
    case EView::LIST: {
      const int n = std::min(std::max(shown_rows, 0), L.rows);
      for (int i = 0; i < n; ++i)
        if (hit(L.row[i])) return {EZ::ROW, i};
      if (hit(L.prev)) return {EZ::PREV, -1};
      if (hit(L.next)) return {EZ::NEXT, -1};
      if (hit(L.add)) return {EZ::ADD, -1};
      return {};
    }
    case EView::DETAIL: {
      static const EZ s[4] = {EZ::S_M60, EZ::S_M15, EZ::S_P15, EZ::S_P60};
      static const EZ e[4] = {EZ::E_M60, EZ::E_M15, EZ::E_P15, EZ::E_P60};
      for (int i = 0; i < 4; ++i) {
        if (hit(L.s_key[i])) return {s[i], -1};
        if (hit(L.e_key[i])) return {e[i], -1};
      }
      if (hit(L.mode_key)) return {EZ::MODE, -1};
      if (hit(L.temp_m)) return {EZ::TEMP_M, -1};
      if (hit(L.temp_p)) return {EZ::TEMP_P, -1};
      if (hit(L.del)) return {EZ::DELETE, -1};
      if (hit(L.save)) return {EZ::SAVE, -1};
      if (hit(L.back)) return {EZ::BACK, -1};
      return {};
    }
    case EView::CONFIRM:
      if (hit(L.conf_no)) return {EZ::CONFIRM_NO, -1};
      if (hit(L.conf_yes)) return {EZ::CONFIRM_YES, -1};
      return {};
  }
  return {};
}

// ---------------------------------------------------------------------------------------------------------------
// What a zone does to the copy (the keys of the detail)

struct KeyStep {
  bool start = false;   // else the end
  int delta = 0;        // minutes
};
inline bool time_key(EZ z, KeyStep &out) {
  switch (z) {
    case EZ::S_M60: out = {true, -EDIT_BIG_STEP}; return true;
    case EZ::S_M15: out = {true, -EDIT_SMALL_STEP}; return true;
    case EZ::S_P15: out = {true, EDIT_SMALL_STEP}; return true;
    case EZ::S_P60: out = {true, EDIT_BIG_STEP}; return true;
    case EZ::E_M60: out = {false, -EDIT_BIG_STEP}; return true;
    case EZ::E_M15: out = {false, -EDIT_SMALL_STEP}; return true;
    case EZ::E_P15: out = {false, EDIT_SMALL_STEP}; return true;
    case EZ::E_P60: out = {false, EDIT_BIG_STEP}; return true;
    default: return false;
  }
}

// ---------------------------------------------------------------------------------------------------------------
// The confirmation of Delete

// Confirm ignores taps this long after the confirmation opened: a double tap on Delete (a finger that lands twice) cannot
// confirm. Cancel works at once.
constexpr uint32_t CONFIRM_GUARD_MS = 600;

enum class ConfirmTap { NONE, CANCEL, DELETE };
// What a tap on zone `z` does, `now` and `opened_at` in milliseconds (the clock may wrap).
inline ConfirmTap confirm_tap(EZ z, uint32_t now, uint32_t opened_at) {
  if (z == EZ::CONFIRM_NO) return ConfirmTap::CANCEL;
  if (z == EZ::CONFIRM_YES && expired(now, opened_at, CONFIRM_GUARD_MS)) return ConfirmTap::DELETE;
  return ConfirmTap::NONE;
}

// Which lines of the confirmation to draw when `lines` fit (0..3): the band always, then the warning of a shared program,
// then the question. Top to bottom they stand as question, band, warning.
struct ConfirmLines {
  bool question = false, band = false, warning = false;
};
inline ConfirmLines confirm_lines(int lines, bool shared) {
  ConfirmLines c;
  const int n = std::min(std::max(lines, 0), shared ? 3 : 2);
  if (n >= 1) c.band = true;
  if (shared) {
    if (n >= 2) c.warning = true;
    if (n >= 3) c.question = true;
  } else if (n >= 2) {
    c.question = true;
  }
  return c;
}

// The warning is for a program that more than one room uses. An unknown count (-1) or one room says nothing.
inline bool confirm_shared(int rooms) { return rooms > 1; }

// "08:30-10:30 Heat 21.5°": the time, the mode and, when there is one, the temperature.
inline std::string band_summary(const std::string &time, const std::string &mode, const std::string &temp) {
  std::string s = time;
  if (!mode.empty()) s += (s.empty() ? "" : " ") + mode;
  if (!temp.empty()) s += (s.empty() ? "" : " ") + temp;
  return s;
}

// ---------------------------------------------------------------------------------------------------------------
// Texts: translation keys (screen part of translations/*.json), a long form and a short one

struct TextKeys {
  const char *full = "";
  const char *shortly = "";
  bool any() const { return *full != 0; }
};

inline TextKeys msg_text(Msg m) {
  switch (m) {
    case Msg::EDIT_BLOCKED: return {"msg_edit_blocked", "msg_edit_blocked_s"};
    case Msg::LOCKED: return {"msg_locked", "msg_locked_s"};
    case Msg::FULL: return {"msg_full", "msg_full_s"};
    case Msg::NO_SPACE: return {"msg_no_space", "msg_no_space_s"};
    default: return {};
  }
}
inline TextKeys notice_text(Notice n) {
  switch (n) {
    case Notice::CHANGED_ELSEWHERE: return {"notice_elsewhere", "notice_elsewhere_s"};
    case Notice::DAY_CHANGED: return {"notice_day", "notice_day_s"};
    default: return {};
  }
}
// Why Save does nothing. NOT_DIRTY is not an error, only "nothing to save".
inline TextKeys block_text(SaveBlock b) {
  switch (b) {
    case SaveBlock::LOCKED: return {"block_locked", "block_locked_s"};
    case SaveBlock::DAMAGED: return {"block_damaged", "block_damaged_s"};
    case SaveBlock::NOT_DIRTY: return {"block_not_dirty", "block_not_dirty_s"};
    case SaveBlock::TOO_LONG: return {"block_too_long", "block_too_long_s"};
    case SaveBlock::INVALID: return {"block_invalid", "block_invalid_s"};
    default: return {};
  }
}
// The code of a refused save (edit_info's last field, or "not_sent" from the card). A code nobody knows reads "Not saved".
inline TextKeys error_text(const std::string &code) {
  struct E {
    const char *code, *full, *shortly;
  };
  static const E table[] = {
      {"stale_day", "err_stale_day", "err_stale_day_s"},
      {"conflict", "err_conflict", "err_conflict_s"},
      {"day_locked", "err_day_locked", "err_day_locked_s"},
      {"invalid_bands", "err_invalid_bands", "err_invalid_bands_s"},
      {"temp_range", "err_temp_range", "err_temp_range_s"},
      {"temperature_out_of_range", "err_temp_range", "err_temp_range_s"},
      {"invalid_program", "err_invalid_program", "err_invalid_program_s"},
      {"room_not_ready", "err_room_not_ready", "err_room_not_ready_s"},
      {"not_sent", "err_not_sent", "err_not_sent_s"},
  };
  for (const E &e : table)
    if (code == e.code) return {e.full, e.shortly};
  return {"err_unknown", "err_unknown_s"};
}

// The words of the confirmation (the warning has {n} for the number of rooms).
struct ConfirmWords {
  TextKeys question, shared;
  const char *yes, *no;
};
inline ConfirmWords confirm_words() {
  return {{"confirm_q", "confirm_q_s"}, {"confirm_shared", "confirm_shared_s"}, "confirm_yes", "confirm_no"};
}

// May Delete open the confirmation for the selected band? `why` says what stops it (the same words the keys use), empty when
// the reason is not worth a word (nothing selected). Nothing here changes the copy.
struct DeleteGate {
  bool ok = false;
  TextKeys why;
};
inline DeleteGate delete_gate(const EditModel &m) {
  DeleteGate g;
  const EditBand *b = m.selected() >= 0 ? m.band((size_t) m.selected()) : nullptr;
  if (!b) return g;
  if (m.damaged()) {
    g.why = block_text(SaveBlock::DAMAGED);
  } else if (m.frozen()) {
    g.why = msg_text(Msg::EDIT_BLOCKED);
  } else if (b->locked) {
    g.why = msg_text(Msg::LOCKED);
  } else if (m.has_locked()) {
    g.why = block_text(SaveBlock::LOCKED);   // another band of the day is locked: the day cannot be saved from here
  } else {
    g.ok = true;
  }
  return g;
}

// Whether the confirmation stays open after on_state() said `ev`. Any news (the bands reloaded, changed elsewhere, another
// day, the answer to a save) closes it without sending: what was shown may not be what is there now.
inline bool confirm_survives(EditEvent ev) { return ev == EditEvent::NONE; }

// The state of Save as the key says it, in one word.
inline const char *save_key(SaveState s) {
  switch (s) {
    case SaveState::SAVING: return "saving";
    case SaveState::SAVED: return "saved";
    case SaveState::ERROR: return "error";
    case SaveState::TIMEOUT: return "not_saved";
    default: return "save";
  }
}

// The sentence for the status line, by priority: a save under way or done, a refusal; then what a key said a moment ago
// (`transient`); then the notice of a change made elsewhere. Nothing: the line shows the title.
inline TextKeys status_of(SaveState state, Notice notice, const std::string &error, const TextKeys &transient) {
  switch (state) {
    case SaveState::SAVING: return {"msg_saving", "msg_saving"};
    case SaveState::SAVED: return {"msg_saved", "msg_saved"};
    case SaveState::ERROR: return error_text(error);
    case SaveState::TIMEOUT: return {"err_timeout", "err_timeout_s"};
    default: break;
  }
  if (transient.any()) return transient;
  return notice_text(notice);
}

// "1/3" for the page; the long form has the word ("Page 1/3") in the translation.
inline std::string page_numbers(int page, int count) { return std::to_string(page + 1) + "/" + std::to_string(count); }

}  // namespace esphome::cronotermostato
