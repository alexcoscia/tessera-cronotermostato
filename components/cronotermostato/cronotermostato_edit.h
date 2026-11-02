#pragma once
// Pure logic of editing the bands of today from the display (plan 4, step C): no ESPHome, no LVGL, plain C++17, header
// only, so it builds and runs on a computer (tests/test_edit.cpp, tests/run.sh). The tile (cronotermostato.cpp, step D)
// turns it into rows, keys and the `cronotermostato.set_day` action.
//
// What it does:
//   - EditModel   the working copy of the USER bands of today (the automatic "off" bands are gaps, never rows), the
//                 selected row, the page, and the operations of the keys (start/end +-1h +-15min, mode, temperature,
//                 add, delete) with their local checks;
//   - encode_bands  the text of the `bands` field of `set_day` (grammar of engine/bands_text.py of the integration);
//   - parse_edit_info  the `edit_info` attribute: `<today_kind>;<program_rooms>;<program_revision>;<last_error>`;
//   - EditSession the model plus the Save state machine (one send, wait for Home Assistant, error, timeout) and the
//                 detection of a change made elsewhere (program revision) or of a new day;
//   - panel_allowed  whether the edit panel may be offered at all.
// All times are milliseconds given by the caller (a wrapping uint32_t counter is fine).
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "cronotermostato_logic.h"

namespace esphome::cronotermostato {

// ---------------------------------------------------------------------------------------------------------------
// Constants

constexpr size_t EDIT_MAX_BANDS = MAX_BANDS;     // user bands in the working copy (16)
constexpr int EDIT_MIN_MINUTES = 15;             // shortest band
constexpr int EDIT_ADD_MINUTES = 60;             // length of a new band, and the smallest gap that takes one
constexpr int EDIT_BIG_STEP = 60;                // the keys: +-1 h ...
constexpr int EDIT_SMALL_STEP = 15;              // ... and +-15 min
constexpr size_t EDIT_ACTION_BYTES = 1024;       // plugin_action.h: key + value of every field, all together
constexpr size_t EDIT_MAX_TEXT = 960;            // fallback ceiling of `bands` when nothing better is known
constexpr size_t EDIT_MAX_PRESET = 40;           // as the integration
constexpr int EDIT_MIN_ROWS = 3, EDIT_MAX_ROWS = 14;   // rows per page, by the size of the screen
constexpr uint32_t SAVE_TIMEOUT_MS = 5000;
constexpr double EDIT_DEFAULT_TEMP = 20.0;       // for a band that has no neighbour with a temperature
constexpr size_t EDIT_INFO_MAX = 256;            // longer `edit_info` is hostile

// ---------------------------------------------------------------------------------------------------------------
// edit_info

// `<today_kind>;<program_rooms>;<program_revision>;<last_error>`. `valid` is true only for exactly four fields with a
// known kind, a number of rooms and a revision. Whatever fails is left empty / -1, never partly trusted.
struct EditInfo {
  bool valid = false;
  std::string kind;        // mon tue wed thu fri sat sun holiday
  int rooms = -1;          // rooms that use the program (the edit is shared by all of them); -1 unknown
  std::string revision;    // opaque token of the program revision: [A-Za-z0-9._-]{1,64}
  std::string error;       // code of the last refused set_day, "" none; text that is not a code reads "unknown"
  // Not in the text: the tile fills them from the attributes `edit_seq` and `edit_n` of the room sensor.
  int64_t seq = 0;         // counter of the refused set_day of the room (rises on every refusal, even with the same code)
  int n = -1;              // number of entries of the today_* lists (`edit_n`); -1: the attribute is absent
};

namespace detail {
inline bool is_code_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}
inline bool known_kind(const std::string &s) {
  static const char *const kinds[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun", "holiday"};
  for (const char *k : kinds)
    if (s == k) return true;
  return false;
}
inline bool is_revision(const std::string &s) {
  if (s.empty() || s.size() > 64) return false;
  for (char c : s)
    if (!(is_code_char(c) || c == '.')) return false;
  return true;
}
}  // namespace detail

inline EditInfo parse_edit_info(const std::string &text) {
  EditInfo info;
  if (text.size() > EDIT_INFO_MAX) return info;
  std::vector<std::string> f(1);
  for (char c : text) {
    if (c == ';') f.emplace_back();
    else f.back().push_back(c);
  }
  if (f.size() > 4) return info;          // too many ";": nothing is trusted
  f.resize(4);                            // fewer: the missing ones are empty (and the result is not valid)
  const bool complete = text.empty() ? false : std::count(text.begin(), text.end(), ';') == 3;
  bool ok = complete;
  if (detail::known_kind(f[0])) info.kind = f[0];
  else ok = false;
  if (!f[1].empty() && f[1].size() <= 4) {
    int n = 0;
    bool digits = true;
    for (char c : f[1]) {
      if (c < '0' || c > '9') digits = false;
      else n = n * 10 + (c - '0');
    }
    if (digits) info.rooms = n;
  }
  if (info.rooms < 0) ok = false;
  if (detail::is_revision(f[2])) info.revision = f[2];
  else ok = false;
  if (!f[3].empty()) {
    bool code = f[3].size() <= 40;
    for (char c : f[3])
      if (!detail::is_code_char(c)) code = false;
    info.error = code ? f[3] : "unknown";
  }
  info.valid = ok;
  if (!ok) {   // nothing of a text that is not whole is trusted (the error code is still readable)
    info.kind.clear();
    info.rooms = -1;
    info.revision.clear();
  }
  return info;
}

// The longest `bands` that fits the action: 1024 bytes minus key + value of the other fields (climate_entity_id, day_kind,
// base_revision when there is one) and the key of `bands` itself. An unknown climate id counts as the longest entity id (255).
inline size_t max_text_for(const std::string &climate_id, const std::string &day_kind, const std::string &revision) {
  const size_t id = climate_id.empty() ? 255 : climate_id.size();
  size_t used = 17 + id + 8 + day_kind.size() + 5;   // "climate_entity_id", "day_kind", "bands"
  if (!revision.empty()) used += 13 + revision.size();   // "base_revision"
  return used >= EDIT_ACTION_BYTES ? 0 : EDIT_ACTION_BYTES - used;
}

// ---------------------------------------------------------------------------------------------------------------
// The working copy

struct EditBand {
  int start = 0, end = 0;     // minutes, 0 <= start < end <= 1440
  Mode mode = Mode::OFF;
  bool has_temp = false;      // off: never; heat/cool: a temperature, a preset or both
  int centi = 0;              // temperature in hundredths of a degree (valid when has_temp)
  std::string preset;         // "" none; heat/cool only
  bool locked = false;        // shown, never changed (the integration refuses a day that has one)
};

// What a key press did: `changed` (the copy is different now) and `msg` (what to tell the person, if anything). A step
// that produces nothing (a temperature at its limit, no row selected) is { false, NONE }.
enum class Msg { NONE, EDIT_BLOCKED, LOCKED, FULL, NO_SPACE };
struct EditResult {
  bool changed = false;
  Msg msg = Msg::NONE;
};
// Stable codes for the translations: "", "edit_blocked", "locked", "full", "no_space".
inline const char *msg_code(Msg m) {
  switch (m) {
    case Msg::EDIT_BLOCKED: return "edit_blocked";
    case Msg::LOCKED: return "locked";
    case Msg::FULL: return "full";
    case Msg::NO_SPACE: return "no_space";
    default: return "";
  }
}

// Why Save is disabled ("locked" is the one the display explains).
enum class SaveBlock { NONE, LOCKED, DAMAGED, NOT_DIRTY, TOO_LONG, INVALID };
inline const char *save_block_code(SaveBlock b) {
  switch (b) {
    case SaveBlock::LOCKED: return "locked";
    case SaveBlock::DAMAGED: return "damaged";
    case SaveBlock::NOT_DIRTY: return "not_dirty";
    case SaveBlock::TOO_LONG: return "too_long";
    case SaveBlock::INVALID: return "invalid";
    default: return "";
  }
}

// The lists of the room sensor, as the tile reads them. `preset` and `flags` are parallel to `lists` (one entry per band,
// automatic ones included). `info.n` must be the length of the lists: the core cuts all lists to the same shorter length
// when they are too big, and a cut day must never be saved.
struct EditInput {
  DayLists lists;   // start, end, mode, temp (`autom` is not used: the flags below say it)
  std::vector<std::string> preset;
  std::vector<double> flags;   // `today_flags`: bit 1 automatic band, bit 2 locked band
  EditInfo info;
  std::string program_name;
};

namespace detail {
inline bool preset_ok(const std::string &p) {
  if (p.empty() || p.size() > EDIT_MAX_PRESET) return false;
  for (char c : p)
    if (!is_code_char(c)) return false;
  return true;
}
inline bool same_content(const EditBand &a, const EditBand &b) {
  return a.start == b.start && a.end == b.end && a.mode == b.mode && a.has_temp == b.has_temp &&
         (!a.has_temp || a.centi == b.centi) && a.preset == b.preset;
}
}  // namespace detail

// Content as the integration keeps it: sorted, and neighbours that touch and say the same become one band. Used to
// compare what was sent with what came back, and to decide `dirty`; never as the text of a band.
inline std::vector<EditBand> normalized(std::vector<EditBand> v) {
  std::stable_sort(v.begin(), v.end(), [](const EditBand &a, const EditBand &b) { return a.start < b.start; });
  std::vector<EditBand> out;
  for (EditBand &b : v) {
    if (!out.empty() && out.back().end == b.start && out.back().mode == b.mode && out.back().has_temp == b.has_temp &&
        (!b.has_temp || out.back().centi == b.centi) && out.back().preset == b.preset) {
      out.back().end = b.end;
      out.back().locked = out.back().locked || b.locked;
      continue;
    }
    out.push_back(std::move(b));
  }
  return out;
}

inline bool same_bands(const std::vector<EditBand> &a, const std::vector<EditBand> &b) {
  const std::vector<EditBand> x = normalized(a), y = normalized(b);
  if (x.size() != y.size()) return false;
  for (size_t i = 0; i < x.size(); ++i)
    if (!detail::same_content(x[i], y[i])) return false;
  return true;
}

// Text of one temperature: 2000 -> "20", 2050 -> "20.5", 1725 -> "17.25". Only for 0 <= centi < 100000.
inline std::string temp_text(int centi) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%d.%02d", centi / 100, centi % 100);
  std::string s = buf;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

struct EncodeResult {
  bool ok = false;
  std::string text;          // the `bands` field; "" is a valid text (a day without user bands)
  const char *error = "";    // "too_long" | "invalid"
};

// `HH:MM-HH:MM mode [temp] [preset=NAME]` joined by "; ". 24:00 as an end, the dot in the temperature, the preset as read.
// `max_text`: the longest `bands` that the action takes (max_text_for). "invalid": a band that cannot be written safely (preset outside [A-Za-z0-9_-]{1,40}, temperature that is negative, 1000
// or over, heat/cool with neither temperature nor preset, off with one, start >= end, overlap, out of the day).
inline EncodeResult encode_bands(const std::vector<EditBand> &bands, size_t max_text = EDIT_MAX_TEXT) {
  EncodeResult r;
  std::string text;
  int covered = 0;
  for (const EditBand &b : bands) {
    const bool shape = b.start >= covered && b.start >= 0 && b.end > b.start && b.end <= MINUTES_PER_DAY;
    const bool temp_ok = !b.has_temp || (b.centi >= 0 && b.centi < 100000);
    const bool preset_good = b.preset.empty() || detail::preset_ok(b.preset);
    const bool mode_ok = b.mode == Mode::OFF ? (!b.has_temp && b.preset.empty()) : (b.has_temp || !b.preset.empty());
    if (!shape || !temp_ok || !preset_good || !mode_ok) {
      r.error = "invalid";
      return r;
    }
    covered = b.end;
    if (!text.empty()) text += "; ";
    text += format_clock(b.start) + "-" + format_clock(b.end) + " " + mode_name(b.mode);
    if (b.has_temp) text += " " + temp_text(b.centi);
    if (!b.preset.empty()) text += " preset=" + b.preset;
    if (text.size() > max_text) {
      r.error = "too_long";
      return r;
    }
  }
  r.ok = true;
  r.text = std::move(text);
  return r;
}

class EditModel {
 public:
  // --- loading -------------------------------------------------------------------------------------------------
  // Builds the copy from the lists: the automatic bands are skipped. Anything that had to be repaired (lists of different
  // lengths, a value that is not a number, overlap, more than 16 bands, a band that could not be written back) sets
  // `damaged()`: the bands are shown as far as possible but Save stays off.
  void load(const EditInput &in) {
    std::vector<EditBand> out;
    damaged_ = false;
    const DayLists &l = in.lists;
    const size_t n0 = std::min({l.start.size(), l.end.size(), l.mode.size(), l.temp.size()});
    const size_t big = std::max({l.start.size(), l.end.size(), l.mode.size(), l.temp.size()});
    const size_t n = std::min(n0, MAX_LIST_ITEMS);
    if (big != n0 || n0 > MAX_LIST_ITEMS || in.preset.size() != n0 || in.flags.size() != n0) damaged_ = true;
    // The core cuts every list to the same shorter length when they are too big: `edit_n` tells the real count.
    if (in.info.n >= 0 ? (size_t) in.info.n != n0 : in.info.valid) damaged_ = true;
    int covered = 0;
    std::vector<std::pair<EditBand, size_t>> raw;
    for (size_t i = 0; i < n; ++i) {
      int flags = 0;
      if (i < in.flags.size() && std::isfinite(in.flags[i]) && in.flags[i] >= 0 && in.flags[i] <= 255 && in.flags[i] == std::floor(in.flags[i]))
        flags = (int) in.flags[i];
      else
        damaged_ = true;   // missing or not a small whole number: nothing is known about the band
      if (flags & 1) continue;   // automatic: a gap, not a row
      EditBand b;
      if (!detail::clean_minutes(l.start[i], b.start) || !detail::clean_minutes(l.end[i], b.end) || b.start >= MINUTES_PER_DAY ||
          b.end <= b.start) {
        damaged_ = true;
        continue;
      }
      if (l.start[i] != b.start || l.end[i] != b.end) damaged_ = true;   // clamped or fractional minutes
      if (!std::isfinite(l.mode[i]) || (l.mode[i] != 0 && l.mode[i] != 1 && l.mode[i] != 2)) damaged_ = true;
      b.mode = mode_from_code(l.mode[i]);
      if (std::isfinite(l.temp[i]) && l.temp[i] > NO_TEMP) {
        const double t = l.temp[i];
        const double c = std::round(t * 100.0);
        if (t >= 0 && t < 1000 && std::fabs(t * 100.0 - c) < 1e-6) {
          b.centi = (int) c;
          b.has_temp = true;
        } else {
          b.locked = true;   // cannot be written back as it is
        }
      }
      if (i < in.preset.size()) {
        b.preset = in.preset[i];
        if (!b.preset.empty() && !detail::preset_ok(b.preset)) b.locked = true;
      }
      if (flags & 2) b.locked = true;
      if (b.mode == Mode::OFF && (b.has_temp || !b.preset.empty())) {   // off takes neither
        b.has_temp = false;
        b.preset.clear();
        damaged_ = true;
      }
      if (b.mode != Mode::OFF && !b.has_temp && b.preset.empty() && !b.locked) damaged_ = true;
      raw.emplace_back(std::move(b), i);
    }
    std::stable_sort(raw.begin(), raw.end(),
                     [](const std::pair<EditBand, size_t> &a, const std::pair<EditBand, size_t> &b) { return a.first.start < b.first.start; });
    for (auto &p : raw) {
      EditBand &b = p.first;
      if (b.start < covered) {   // overlap: repaired the way normalize_day does, and flagged
        b.start = covered;
        damaged_ = true;
      }
      if (b.start >= b.end) {
        damaged_ = true;
        continue;
      }
      covered = b.end;
      if (out.size() >= EDIT_MAX_BANDS) {
        damaged_ = true;
        break;
      }
      out.push_back(std::move(b));
    }
    work_ = out;
    original_ = std::move(out);
    selected_ = -1;   // back to the list; the page stays when it still exists
    frozen_ = false;
    clamp_page();
  }
  void first_page() { page_ = 0; }

  // --- the copy ------------------------------------------------------------------------------------------------
  size_t size() const { return work_.size(); }
  const std::vector<EditBand> &bands() const { return work_; }
  const std::vector<EditBand> &original() const { return original_; }
  const EditBand *band(size_t i) const { return i < work_.size() ? &work_[i] : nullptr; }
  bool damaged() const { return damaged_; }
  bool has_locked() const {
    for (const EditBand &b : work_)
      if (b.locked) return true;
    return false;
  }
  bool dirty() const { return !same_bands(work_, original_); }

  // After a load Home Assistant's version is the base again.
  void set_range(const SetpointRange &r) { range_ = r; }
  const SetpointRange &range() const { return range_; }
  // While a save is under way the copy does not change.
  void set_frozen(bool f) { frozen_ = f; }
  bool frozen() const { return frozen_; }

  // --- selection and pages -------------------------------------------------------------------------------------
  int selected() const { return selected_; }
  bool select(int i) {
    if (i < 0 || (size_t) i >= work_.size()) return false;
    selected_ = i;
    page_ = i / rows_;
    return true;
  }
  void deselect() { selected_ = -1; }
  int rows_per_page() const { return rows_; }
  void set_rows_per_page(int rows) {
    rows_ = std::min(std::max(rows, EDIT_MIN_ROWS), EDIT_MAX_ROWS);
    if (selected_ >= 0) page_ = selected_ / rows_;
    clamp_page();
  }
  int page() const { return page_; }
  int page_count() const { return work_.empty() ? 1 : (int) ((work_.size() + rows_ - 1) / rows_); }
  bool next_page() {
    if (page_ + 1 >= page_count()) return false;
    ++page_;
    return true;
  }
  bool prev_page() {
    if (page_ <= 0) return false;
    --page_;
    return true;
  }
  int page_first() const { return page_ * rows_; }                                         // index of the first row shown
  int page_rows() const { return std::min(rows_, std::max(0, (int) work_.size() - page_first())); }   // rows shown

  // --- operations ----------------------------------------------------------------------------------------------
  // Start of the selected band by `delta` minutes (the keys use +-60 and +-15). Blocked ("edit_blocked") below 00:00, into
  // the band before, or within 15 minutes of its end: moving an edge never drags the neighbour.
  EditResult move_start(int delta) {
    EditBand *b = target();
    if (!b) return refusal_;
    if (delta == 0) return {};
    const int idx = selected_;
    const int64_t v = (int64_t) b->start + delta;
    const int lo = idx > 0 ? work_[idx - 1].end : 0;
    if (v < lo || v > (int64_t) b->end - EDIT_MIN_MINUTES) return {false, Msg::EDIT_BLOCKED};
    b->start = (int) v;
    return {true, Msg::NONE};
  }
  // End of the selected band: blocked after 24:00, into the next band, or within 15 minutes of its start.
  EditResult move_end(int delta) {
    EditBand *b = target();
    if (!b) return refusal_;
    if (delta == 0) return {};
    const int idx = selected_;
    const int64_t v = (int64_t) b->end + delta;
    const int hi = (size_t) idx + 1 < work_.size() ? work_[idx + 1].start : MINUTES_PER_DAY;
    if (v > hi || v < (int64_t) b->start + EDIT_MIN_MINUTES) return {false, Msg::EDIT_BLOCKED};
    b->end = (int) v;
    return {true, Msg::NONE};
  }
  // Heat -> cool -> off -> heat. Heat and cool keep temperature and preset; off drops both; off -> heat takes the
  // temperature of the neighbour (or 20 degrees) and no preset.
  EditResult cycle_mode() {
    EditBand *b = target();
    if (!b) return refusal_;
    switch (b->mode) {
      case Mode::HEAT: b->mode = Mode::COOL; break;
      case Mode::COOL:
        b->mode = Mode::OFF;
        b->has_temp = false;
        b->centi = 0;
        b->preset.clear();
        break;
      default:
        b->mode = Mode::HEAT;
        b->has_temp = true;
        b->centi = default_temp(selected_);
        break;
    }
    return {true, Msg::NONE};
  }
  // `steps` times the step of the thermostat inside its limits, like the + and - of the tile. A preset-only band gets a
  // temperature (the preset stays). Off bands and steps that stop at a limit change nothing.
  EditResult step_temp(int steps) {
    EditBand *b = target();
    if (!b) return refusal_;
    if (b->mode == Mode::OFF || steps == 0) return {};
    const int base = b->has_temp ? b->centi : default_temp(selected_);
    const int64_t moved = (int64_t) snap(base, range_) + (int64_t) steps * range_.step;
    const int t = snap((int) std::min<int64_t>(std::max<int64_t>(moved, INT_MIN + 1), INT_MAX), range_);
    if (b->has_temp && t == b->centi) return {};
    b->has_temp = true;
    b->centi = t;
    return {true, Msg::NONE};
  }
  // A band of one hour in the first gap of one hour or more, with the mode and temperature of the band before it (or after
  // it when it is the first); a neighbour that is off, or has no temperature, gives heat at the default temperature.
  // Selects the new band. "full" at 16 bands, "no_space" without a gap.
  EditResult add() {
    if (frozen_) return {false, Msg::EDIT_BLOCKED};
    if (work_.size() >= EDIT_MAX_BANDS) return {false, Msg::FULL};
    int cursor = 0;
    int where = -1, at = 0;
    for (size_t i = 0; i <= work_.size() && where < 0; ++i) {
      const int next = i < work_.size() ? work_[i].start : MINUTES_PER_DAY;
      if (next - cursor >= EDIT_ADD_MINUTES) {
        where = (int) i;
        at = cursor;
      } else if (i < work_.size()) {
        cursor = work_[i].end;
      }
    }
    if (where < 0) return {false, Msg::NO_SPACE};
    EditBand nb;
    nb.start = at;
    nb.end = at + EDIT_ADD_MINUTES;
    const EditBand *nei = work_.empty() ? nullptr : &work_[where > 0 ? where - 1 : 0];
    if (nei && nei->mode != Mode::OFF) {
      nb.mode = nei->mode;
      nb.has_temp = true;
      nb.centi = nei->has_temp ? nei->centi : default_temp(where > 0 ? where - 1 : 0);
    } else {
      nb.mode = Mode::HEAT;
      nb.has_temp = true;
      nb.centi = default_temp(where > 0 ? where - 1 : (work_.empty() ? -1 : 0));
    }
    work_.insert(work_.begin() + where, std::move(nb));
    select(where);
    return {true, Msg::NONE};
  }
  // Deletes the selected band; the next one (or the last) is selected, or none.
  EditResult remove() {
    EditBand *b = target();
    if (!b) return refusal_;
    work_.erase(work_.begin() + selected_);
    if (work_.empty()) selected_ = -1;
    else select(std::min<int>(selected_, (int) work_.size() - 1));
    clamp_page();
    return {true, Msg::NONE};
  }

  // --- saving --------------------------------------------------------------------------------------------------
  SaveBlock save_block() const {
    if (has_locked()) return SaveBlock::LOCKED;
    if (damaged_) return SaveBlock::DAMAGED;
    if (!dirty()) return SaveBlock::NOT_DIRTY;
    const EncodeResult e = encode();
    if (!e.ok) return std::string(e.error) == "too_long" ? SaveBlock::TOO_LONG : SaveBlock::INVALID;
    return SaveBlock::NONE;
  }
  // The text to send: the normalized content (touching equal neighbours become one band, as the integration keeps them).
  EncodeResult encode() const { return encode_bands(normalized(work_), max_text_); }
  // The ceiling of `bands` for this room, day and revision (max_text_for); the session keeps it up to date.
  void set_max_text(size_t n) { max_text_ = n; }
  size_t max_text() const { return max_text_; }

 private:
  // The selected band if it can be changed now; otherwise `refusal_` says why (nothing selected: nothing happens).
  EditBand *target() {
    refusal_ = {};
    if (selected_ < 0 || (size_t) selected_ >= work_.size()) return nullptr;
    if (frozen_) {
      refusal_ = {false, Msg::EDIT_BLOCKED};
      return nullptr;
    }
    if (work_[selected_].locked) {
      refusal_ = {false, Msg::LOCKED};
      return nullptr;
    }
    return &work_[selected_];
  }
  // Temperature of the nearest band that has one (the one before first), or 20 degrees; always on the step grid.
  int default_temp(int index) const {
    int best = NO_VALUE;
    if (index >= 0 && (size_t) index < work_.size()) {
      int bestd = INT_MAX;
      for (int j = 0; j < (int) work_.size(); ++j) {
        if (j == index || !work_[j].has_temp || work_[j].mode == Mode::OFF) continue;
        const int d = std::abs(j - index) * 2 + (j > index ? 1 : 0);   // before beats after at the same distance
        if (d < bestd) {
          bestd = d;
          best = work_[j].centi;
        }
      }
    }
    if (best == NO_VALUE) best = to_centi(EDIT_DEFAULT_TEMP);
    return snap(best, range_);
  }
  void clamp_page() {
    if (page_ >= page_count()) page_ = page_count() - 1;
    if (page_ < 0) page_ = 0;
  }

  std::vector<EditBand> work_, original_;
  int selected_ = -1, page_ = 0, rows_ = EDIT_MIN_ROWS;
  bool damaged_ = false, frozen_ = false;
  size_t max_text_ = EDIT_MAX_TEXT;
  SetpointRange range_;
  EditResult refusal_;
};

// "<program> - N rooms" (the words come from the translations). The name is cut like the pill's; an unknown number of
// rooms gives the name alone; no name gives just the count.
inline std::string edit_title(const std::string &program, int rooms, const char *one = "room", const char *many = "rooms") {
  const std::string name = detail::cut_name(program);
  std::string count;
  if (rooms >= 0) count = std::to_string(rooms) + " " + (rooms == 1 ? one : many);
  if (name.empty() || name == "unknown" || name == "unavailable") return count;
  return count.empty() ? name : name + " - " + count;
}

// ---------------------------------------------------------------------------------------------------------------
// The session: the model and the Save state machine

enum class SaveState { IDLE, SAVING, SAVED, ERROR, TIMEOUT };
// What the display must say about a change it noticed, until the next tap.
enum class Notice { NONE, CHANGED_ELSEWHERE, DAY_CHANGED };
// What on_state() found.
enum class EditEvent {
  NONE,               // nothing for the panel
  RELOADED,           // the bands changed elsewhere and the copy was not touched: reloaded in silence
  CHANGED_ELSEWHERE,  // the copy had changes: they are gone, the list is shown again with the new bands
  DAY_CHANGED,        // it is another day (midnight): close the detail, reload
  SAVED,              // Home Assistant brought what was sent
  SAVE_FAILED,        // Home Assistant refused (error()) or something else changed during the save
};

// What to do when Save is pressed: send `set_day` with these fields, once.
struct SaveRequest {
  bool send = false;
  std::string day_kind;        // today_kind seen at the opening
  std::string bands;
  std::string base_revision;   // revision seen at the opening ("" unknown: leave the field out)
};

class EditSession {
 public:
  EditModel &model() { return model_; }
  const EditModel &model() const { return model_; }
  bool opened() const { return opened_; }

  // Enter the edit mode with what the sensor says now.
  void open(const EditInput &in, const SetpointRange &range, int rows, const std::string &climate_id = "") {
    opened_ = true;
    model_ = EditModel();   // nothing of an earlier session stays (page, freeze)
    climate_id_ = climate_id;
    model_.set_range(range);
    model_.set_rows_per_page(rows);
    load(in);
    state_ = SaveState::IDLE;
    notice_ = Notice::NONE;
    error_.clear();
  }
  void close() {
    opened_ = false;
    state_ = SaveState::IDLE;
    notice_ = Notice::NONE;
    model_.set_frozen(false);
  }

  SaveState state() const { return state_; }
  Notice notice() const { return notice_; }
  const std::string &error() const { return error_; }            // code shown with ERROR ("" for TIMEOUT: plain "Not saved")
  const std::string &seen_kind() const { return kind_; }
  const std::string &seen_revision() const { return revision_; }
  int rooms() const { return rooms_; }
  const std::string &program() const { return program_; }
  std::string title(const char *one = "room", const char *many = "rooms") const { return edit_title(program_, rooms_, one, many); }

  // Save is possible now: nothing under way, something to save, nothing locked.
  bool can_save() const {
    return opened_ && !kind_.empty() && state_ == SaveState::IDLE && model_.save_block() == SaveBlock::NONE;
  }
  SaveBlock save_block() const { return model_.save_block(); }

  // The Save key. In IDLE and possible: SAVING and the request (exactly once). SAVING: ignored. SAVED/ERROR/TIMEOUT: this
  // tap only goes back to IDLE. Call this OR acknowledge() for a tap, not both.
  SaveRequest press_save(uint32_t now) {
    SaveRequest r;
    if (!opened_) return r;
    if (state_ == SaveState::SAVING) return r;
    if (state_ != SaveState::IDLE) {
      acknowledge();
      return r;
    }
    notice_ = Notice::NONE;
    if (kind_.empty() || model_.save_block() != SaveBlock::NONE) return r;
    const EncodeResult e = model_.encode();
    if (!e.ok) return r;
    sent_ = normalized(model_.bands());
    sent_at_ = now;
    state_ = SaveState::SAVING;
    error_.clear();
    model_.set_frozen(true);
    r.send = true;
    r.day_kind = kind_;
    r.bands = e.text;
    r.base_revision = revision_;
    return r;
  }

  // Any other tap: the result of a save (SAVED, ERROR, TIMEOUT) and the notice are acknowledged. A save under way stays.
  void acknowledge() {
    notice_ = Notice::NONE;
    if (state_ == SaveState::SAVED || state_ == SaveState::ERROR || state_ == SaveState::TIMEOUT) {
      state_ = SaveState::IDLE;
      error_.clear();
    }
  }

  // `tessera::action` answered false (the fields did not fit, or the action was refused): nothing was sent. SAVING ends in
  // ERROR with the code "not_sent". True when the state changed.
  bool send_failed() {
    if (!opened_ || state_ != SaveState::SAVING) return false;
    state_ = SaveState::ERROR;
    error_ = "not_sent";
    model_.set_frozen(false);
    return true;
  }

  // From the tile's tick: 5 s after the send without an answer. True when the state changed (repaint).
  bool tick(uint32_t now) {
    if (!opened_ || state_ != SaveState::SAVING || !expired(now, sent_at_, SAVE_TIMEOUT_MS)) return false;
    state_ = SaveState::TIMEOUT;
    error_.clear();
    model_.set_frozen(false);
    return true;
  }

  // The sensor changed. Order: another day; the answer to a save; changes made elsewhere.
  EditEvent on_state(const EditInput &in, uint32_t /*now*/) {
    if (!opened_) return EditEvent::NONE;
    // The sensor is unavailable or its edit_info unreadable for a moment: nothing is known, nothing is touched (the
    // copy stays, a save under way ends by its timeout, panel_allowed hides the panel).
    if (!in.info.valid) return EditEvent::NONE;
    const std::string error_before = baseline_error_;
    const int64_t seq_before = baseline_seq_;
    if (in.info.kind != kind_) {
      reload(in);
      first_page_after_day();
      state_ = SaveState::IDLE;
      error_.clear();
      notice_ = Notice::DAY_CHANGED;
      return EditEvent::DAY_CHANGED;
    }
    EditModel fresh;
    fresh.load(in);
    const bool sent_state = state_ == SaveState::SAVING || state_ == SaveState::TIMEOUT;
    if (sent_state && !fresh.damaged() && same_bands(fresh.bands(), sent_) &&
        (state_ == SaveState::SAVING || same_bands(model_.bands(), sent_))) {   // what we sent has arrived (also late)
      const int keep = model_.selected();
      reload(in);
      if (keep >= 0 && model_.size() > 0) model_.select(std::min<int>(keep, (int) model_.size() - 1));
      state_ = SaveState::SAVED;
      error_.clear();
      return EditEvent::SAVED;
    }
    if (state_ == SaveState::SAVING) {
      if (!in.info.error.empty() && (in.info.error != error_before || in.info.seq != seq_before)) {
        baseline_error_ = in.info.error;
        baseline_seq_ = in.info.seq;
        state_ = SaveState::ERROR;
        error_ = in.info.error;
        model_.set_frozen(false);
        return EditEvent::SAVE_FAILED;
      }
      baseline_error_ = in.info.error;
      baseline_seq_ = in.info.seq;
      if (changed_elsewhere(in, fresh)) {   // somebody else saved in the meantime
        reload(in);
        state_ = SaveState::IDLE;
        error_.clear();
        notice_ = Notice::CHANGED_ELSEWHERE;
        return EditEvent::SAVE_FAILED;
      }
      return EditEvent::NONE;
    }
    baseline_error_ = in.info.error;
    baseline_seq_ = in.info.seq;
    if (!changed_elsewhere(in, fresh)) return EditEvent::NONE;
    if (model_.dirty()) {
      reload(in);
      state_ = SaveState::IDLE;
      error_.clear();
      notice_ = Notice::CHANGED_ELSEWHERE;
      return EditEvent::CHANGED_ELSEWHERE;
    }
    const int keep = model_.selected();
    reload(in);
    if (keep >= 0) model_.select(std::min<int>(keep, (int) model_.size() - 1));
    return EditEvent::RELOADED;
  }

 private:
  // The bands or the program revision are not the ones the copy started from.
  bool changed_elsewhere(const EditInput &in, const EditModel &fresh) const {
    if (!revision_.empty() && in.info.revision != revision_) return true;
    return !same_bands(fresh.bands(), model_.original()) || fresh.damaged() != model_damaged_;
  }
  void load(const EditInput &in) {
    model_.load(in);
    model_damaged_ = model_.damaged();
    kind_ = in.info.valid ? in.info.kind : "";
    revision_ = in.info.valid ? in.info.revision : "";
    rooms_ = in.info.valid ? in.info.rooms : -1;
    program_ = in.program_name;
    baseline_error_ = in.info.error;
    baseline_seq_ = in.info.seq;
    model_.set_max_text(max_text_for(climate_id_, kind_, revision_));
  }
  void first_page_after_day() { model_.first_page(); }
  // A new base: the copy, the selection (gone) and the revision are the new ones.
  void reload(const EditInput &in) {
    model_.set_frozen(false);
    load(in);
  }

  EditModel model_;
  bool opened_ = false, model_damaged_ = false;
  SaveState state_ = SaveState::IDLE;
  Notice notice_ = Notice::NONE;
  std::string kind_, revision_, program_, error_, baseline_error_, climate_id_;
  int64_t baseline_seq_ = 0;
  int rooms_ = -1;
  std::vector<EditBand> sent_;
  uint32_t sent_at_ = 0;
};

// ---------------------------------------------------------------------------------------------------------------
// Whether the panel is offered

// The tile's option `edit` is on, the room may be commanded (assigned, with a program, not paused, not unavailable: what
// tile_view calls `controls`) and the sensor gave a readable `edit_info`. Whether Save works is the session's business
// (a locked day shows the panel without Save).
inline bool panel_allowed(bool edit_option, const TileView &view, const EditInfo &info) {
  return edit_option && view.controls && view.pill != Pill::UNAVAILABLE && view.pill != Pill::NO_PROGRAM && info.valid;
}

}  // namespace esphome::cronotermostato
