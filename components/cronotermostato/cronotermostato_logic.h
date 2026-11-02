#pragma once
// Pure logic of the cronotermostato tile: no ESPHome, no LVGL, plain C++17, so it builds and runs on a computer
// (tests/test_logic.cpp, tests/run.sh). The tile (cronotermostato.cpp) turns this logic into widgets, words and actions.
#include <algorithm>
#include <climits>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace esphome::cronotermostato {

// ---------------------------------------------------------------------------------------------------------------
// Modes and tones

// The `*_mode` codes of the room sensor: 0 off, 1 heat, 2 cool. Anything else (unknown, fractional, NaN) is OFF.
enum class Mode { OFF = 0, HEAT = 1, COOL = 2 };

inline Mode mode_from_code(double code) {
  if (code == 1.0) return Mode::HEAT;
  if (code == 2.0) return Mode::COOL;
  return Mode::OFF;
}

// Logical tone of a band. The tile maps it to Tessera: AMBER = Tone::BUSY, BLUE = Tone::ACCENT, GREY = Tone::NORMAL
// with theme::SUBTLE (docs/SPIKE.md, (b)).
enum class BarTone { AMBER, BLUE, GREY };

inline BarTone tone_of(Mode mode) {
  switch (mode) {
    case Mode::HEAT: return BarTone::AMBER;
    case Mode::COOL: return BarTone::BLUE;
    default: return BarTone::GREY;
  }
}

// ---------------------------------------------------------------------------------------------------------------
// The bands of today and the bar

constexpr int MINUTES_PER_DAY = 1440;
constexpr size_t MAX_BANDS = 16;         // bands drawn on the bar / listed in the card
constexpr size_t MAX_LIST_ITEMS = 128;        // entries of the lists that are ever looked at
constexpr double NO_TEMP = -100.0;       // `today_temp` value <= this means "no temperature"

// The parallel lists of the room sensor (`today_start`, `today_end` in wall-clock minutes 0-1440, `today_mode`,
// `today_temp`, `today_auto`), as numbers.
struct DayLists {
  std::vector<double> start, end, mode, temp, autom;
};

// One cleaned band: minutes inside 0-1440, start < end, bands sorted and not overlapping.
struct Band {
  int start = 0, end = 0;
  Mode mode = Mode::OFF;
  bool has_temp = false;
  double temp = 0;
  bool autom = false;   // automatic "off" band filling a gap of the program
};

namespace detail {
inline bool clean_minutes(double v, int &out) {
  if (!std::isfinite(v)) return false;
  out = (int) std::lround(std::min(std::max(v, -1.0), (double) MINUTES_PER_DAY + 1.0));
  out = std::min(std::max(out, 0), MINUTES_PER_DAY);
  return true;
}
}  // namespace detail

// The bands of a day ready to draw. `truncated_before` / `truncated_after`: the day has more bands than MAX_BANDS and the
// ones before / after the window were left out, so the bar has an unexplained empty part on that side (the tile may mark it).
struct Day {
  std::vector<Band> bands;
  bool truncated_before = false, truncated_after = false;
};

// Lists -> clean bands. Lists of different lengths: the shortest wins. A band with a value that is not a finite number
// for its start or end is dropped; minutes outside 0-1440 are clamped; a band with start >= end (after clamping) is
// dropped; bands are sorted by start (stable) and a band that overlaps an earlier one starts where that one ends;
// adjacent bands with the same mode, temperature and auto flag become one. At most MAX_BANDS are returned: when there are more, the window that holds the band of `focus_minute` (or the next
// one, or the last) is kept; without a focus (< 0) the first ones.
inline Day normalize_day(const DayLists &l, int focus_minute = -1) {
  size_t n = std::min({l.start.size(), l.end.size(), l.mode.size(), l.temp.size(), l.autom.size(), MAX_LIST_ITEMS});
  std::vector<Band> all;
  all.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    Band b;
    if (!detail::clean_minutes(l.start[i], b.start) || !detail::clean_minutes(l.end[i], b.end)) continue;
    if (b.start >= MINUTES_PER_DAY || b.end <= b.start) continue;
    b.mode = mode_from_code(l.mode[i]);
    b.has_temp = std::isfinite(l.temp[i]) && l.temp[i] > NO_TEMP;
    b.temp = b.has_temp ? l.temp[i] : 0;
    b.autom = std::isfinite(l.autom[i]) && l.autom[i] != 0;
    all.push_back(b);
  }
  std::stable_sort(all.begin(), all.end(), [](const Band &a, const Band &b) { return a.start < b.start; });
  std::vector<Band> out;
  out.reserve(all.size());
  int covered = 0;
  for (Band b : all) {
    b.start = std::max(b.start, covered);
    if (b.start >= b.end) continue;
    covered = b.end;
    if (!out.empty() && out.back().end == b.start && out.back().mode == b.mode && out.back().has_temp == b.has_temp &&
        out.back().temp == b.temp && out.back().autom == b.autom) {
      out.back().end = b.end;
      continue;
    }
    out.push_back(b);
  }
  Day day;
  if (out.size() > MAX_BANDS) {
    size_t cur = 0;
    if (focus_minute >= 0) {
      const int f = std::min(focus_minute, MINUTES_PER_DAY - 1);
      cur = out.size() - 1;
      for (size_t i = 0; i < out.size(); ++i)
        if (out[i].end > f) {
          cur = i;
          break;
        }
    }
    const size_t first = cur > 7 ? std::min(cur - 7, out.size() - MAX_BANDS) : 0;
    day.truncated_before = first > 0;
    day.truncated_after = first + MAX_BANDS < out.size();
    out = std::vector<Band>(out.begin() + first, out.begin() + first + MAX_BANDS);
  }
  day.bands = std::move(out);
  return day;
}

inline std::vector<Band> normalize_bands(const DayLists &l, int focus_minute = -1) { return normalize_day(l, focus_minute).bands; }

// A band on the bar, in pixels: [x0, x1).
struct Segment {
  int x0 = 0, x1 = 0;
  int start = 0, end = 0;   // minutes
  Mode mode = Mode::OFF;
  bool has_temp = false;
  double temp = 0;
  bool autom = false;
};

// Minute -> pixel boundary of a bar `width` pixels wide, rounded to the nearest pixel (the editor's `left` percent
// times the width, i.e. the same maths as stripSegments).
inline int boundary_x(int minute, int width) {
  return (int) (((int64_t) minute * width * 2 + MINUTES_PER_DAY) / (2 * (int64_t) MINUTES_PER_DAY));
}

// Bands -> pixels. Neighbours that touch share their boundary pixel (no hole, no overlap); a gap between bands stays a
// gap; every band gets at least one pixel (a band may push the next one to the right, never over it);
// the last band of a day that runs to 24:00 ends at `width`. If there are fewer pixels than bands, the first `width` bands are drawn.
inline std::vector<Segment> bar_segments(const std::vector<Band> &bands, int width) {
  std::vector<Segment> out;
  if (width <= 0) return out;
  // k bands are drawn: all of them, up to MAX_BANDS and one pixel each. Band i may not reach beyond the pixels the
  // k-1-i bands after it still need.
  const int k = (int) std::min<size_t>(std::min(bands.size(), MAX_BANDS), (size_t) width);
  int prev = 0;
  for (int i = 0; i < k; ++i) {
    const Band &b = bands[i];
    const int cap = width - (k - 1 - i);
    Segment s;
    s.x0 = std::min(std::max(boundary_x(b.start, width), prev), cap - 1);
    s.x1 = std::min(std::max(boundary_x(b.end, width), s.x0 + 1), cap);
    s.start = b.start;
    s.end = b.end;
    s.mode = b.mode;
    s.has_temp = b.has_temp;
    s.temp = b.temp;
    s.autom = b.autom;
    prev = s.x1;
    out.push_back(s);
  }
  return out;
}

// The "now" marker: minutes from local midnight (clamped to 0-1439) -> pixel inside [0, width-1].
inline int now_x(int minute, int width) {
  if (width <= 0) return 0;
  const int m = std::min(std::max(minute, 0), MINUTES_PER_DAY - 1);
  return std::min((int) ((int64_t) m * width / MINUTES_PER_DAY), width - 1);
}

// The pixel for the "now" marker when the bar is drawn: like now_x, but moved inside the drawn band of that minute
// (the floor of the minute can lie one pixel left of the rounded start of its band).
inline int now_marker(const std::vector<Segment> &segments, int minute, int width) {
  int x = now_x(minute, width);
  const int m = std::min(std::max(minute, 0), MINUTES_PER_DAY - 1);
  for (const Segment &s : segments)
    if (s.start <= m && m < s.end) return std::min(std::max(x, s.x0), s.x1 - 1);
  return x;
}

// ---------------------------------------------------------------------------------------------------------------
// Text

// Minutes from midnight -> "HH:MM", 24 hours, with the leading zero. 1440 gives "24:00" (the end of a band that runs
// to midnight, as in the editor); below 0 gives "00:00", above 1440 "24:00", not a number "--:--".
inline std::string format_clock(double minutes) {
  if (!std::isfinite(minutes)) return "--:--";
  const int m = (int) std::lround(std::min(std::max(minutes, 0.0), (double) MINUTES_PER_DAY));
  char buf[32];
  std::snprintf(buf, sizeof buf, "%02d:%02d", m / 60, m % 60);
  return buf;
}

// Hours and minutes of a wall clock (0-23 and 0-59; 24:00 too) -> "HH:MM"; anything else "--:--".
inline std::string format_hm(int hour, int minute) {
  if (hour < 0 || minute < 0 || minute > 59 || hour > 24 || (hour == 24 && minute != 0)) return "--:--";
  char buf[32];
  std::snprintf(buf, sizeof buf, "%02d:%02d", hour, minute);
  return buf;
}

// Decimals to show for a thermostat step: 1 -> 0, 0.5 and 0.1 -> 1, 0.25 -> 2. An unknown step (missing, zero,
// negative, not a clean decimal, finer than 0.01) -> 1.
inline int decimals_of_step(double step) {
  if (!std::isfinite(step) || step <= 0) return 1;
  double scale = 1;
  for (int d = 0; d <= 2; ++d, scale *= 10) {
    const double x = step * scale;
    if (x >= 1 && x < 1e6 && std::fabs(x - std::round(x)) < 1e-6) return d;
  }
  return 1;
}

// A temperature as text with `decimals_of_step(step)` decimals, using only the characters of Font::DIGITS (0-9 - . , °).
// `separator` is '.' or ','; `degree` appends "°" (UTF-8). Not a number or beyond +-999.9 -> "-".
inline std::string format_temp(double value, double step, char separator = '.', bool degree = false) {
  if (!std::isfinite(value) || std::fabs(value) >= 1000.0) return "-";
  const int d = decimals_of_step(step);
  int64_t scale = 1;
  for (int i = 0; i < d; ++i) scale *= 10;
  const int64_t n = std::llround(value * (double) scale + (value < 0 ? -1e-9 : 1e-9));   // half away from zero
  const int64_t a = n < 0 ? -n : n;
  if (a >= 1000 * scale) return "-";   // rounding can reach 1000
  char buf[32];
  if (d == 0) {
    std::snprintf(buf, sizeof buf, "%s%lld", n < 0 ? "-" : "", (long long) a);
  } else {
    std::snprintf(buf, sizeof buf, "%s%lld%c%0*lld", n < 0 ? "-" : "", (long long) (a / scale), separator, d, (long long) (a % scale));
  }
  std::string s = buf;
  if (degree) s += "\xC2\xB0";
  return s;
}

// ---------------------------------------------------------------------------------------------------------------
// Setpoint + / -
//
// Temperatures are kept as integers in hundredths of a degree, so 0.1 + 0.2 is 30 and steps never drift.

constexpr int NO_VALUE = INT_MIN;

inline int to_centi(double degrees) {
  if (!std::isfinite(degrees) || std::fabs(degrees) >= 1e6) return NO_VALUE;
  return (int) std::lround(degrees * 100.0 + (degrees < 0 ? -1e-6 : 1e-6));
}

inline double to_degrees(int centi) { return centi / 100.0; }

struct SetpointRange {
  int lo = 500, hi = 3500, step = 50;   // hundredths of a degree
};

// From the climate's min_temp, max_temp and target_temp_step. A missing/invalid step is 0.5; a missing or inverted range
// is 5-35 (what the integration uses); a range narrower than one step is widened to one step.
inline SetpointRange make_range(double lo, double hi, double step) {
  SetpointRange r;
  const int l = to_centi(lo), h = to_centi(hi);
  if (l != NO_VALUE && h != NO_VALUE && h > l) {
    r.lo = l;
    r.hi = h;
  }
  const int s = to_centi(step);
  r.step = s >= 1 ? s : 50;
  if ((int64_t) r.hi - r.lo < r.step) r.hi = (int) std::min<int64_t>((int64_t) r.lo + r.step, INT_MAX);
  return r;
}

// To the nearest step (halfway rounds up) counted from the minimum, inside the range.
inline int snap(int centi, const SetpointRange &r) {
  const int64_t c = std::min<int64_t>(std::max<int64_t>(centi, r.lo), r.hi);
  int64_t v = r.lo + ((c - r.lo + r.step / 2) / r.step) * r.step;
  if (v > r.hi) v -= r.step;
  return (int) v;
}

constexpr uint32_t DEBOUNCE_MS = 600;   // after the last tap on + or -
constexpr uint32_t CONFIRM_MS = 5000;   // how long Home Assistant has to report the value we sent

// The time passed from `since` to `now`, also across the wrap of the millisecond counter.
inline bool expired(uint32_t now, uint32_t since, uint32_t limit) { return (uint32_t) (now - since) >= limit; }

// What the user sees and when to send: taps move a local value; 600 ms after the last tap the value is due to be sent;
// if Home Assistant does not report it within 5 s of the send, the shown value goes back to the real one. All times
// are milliseconds given by the caller.
class Setpoint {
 public:
  void set_range(const SetpointRange &r) {
    range_ = r;
    if (shown_ != NO_VALUE) shown_ = std::min(std::max(shown_, r.lo), r.hi);
    if (pending_ && pending_value_ != NO_VALUE) pending_value_ = std::min(std::max(pending_value_, r.lo), r.hi);
  }

  // The target Home Assistant reports (NaN: none).
  void on_state(double reported_degrees, uint32_t /*now*/) {
    reported_ = to_centi(reported_degrees);
    if (in_flight_ && reported_ == sent_value_) {   // HA brought what we sent
      in_flight_ = false;
      confirm_on_ = false;
    }
    // HA brought the value we chose. While a different value is on its way, a report equal to the pending one is stale.
    if (pending_ && reported_ == pending_value_ && (!in_flight_ || sent_value_ == pending_value_)) settle();
    if (!pending_) shown_ = reported_;
  }

  // + or - by `delta` steps. Returns true when the shown value changed.
  bool on_tap(int delta, uint32_t now) {
    const int base = shown_ != NO_VALUE ? shown_ : reported_;
    if (base == NO_VALUE || delta == 0) return false;
    const int64_t moved = (int64_t) snap(base, range_) + (int64_t) delta * range_.step;
    const int target = snap((int) std::min<int64_t>(std::max<int64_t>(moved, INT_MIN + 1), INT_MAX), range_);
    if (target == shown_) return false;
    shown_ = target;
    pending_ = true;
    pending_value_ = target;
    debounce_on_ = true;
    debounce_since_ = now;
    confirm_on_ = false;   // the wait restarts with the next send; in_flight_ and sent_value_ stay
    return true;
  }

  bool due(uint32_t now) const { return debounce_on_ && expired(now, debounce_since_, DEBOUNCE_MS); }

  // When due: takes the value to send (hundredths) and starts the 5 s wait. False when nothing is due, or when the value
  // is the one Home Assistant already has.
  bool take(uint32_t now, int &value) {
    if (!due(now)) return false;
    debounce_on_ = false;
    if (in_flight_ && pending_value_ == sent_value_) {   // already on its way: only wait again
      confirm_on_ = true;
      confirm_since_ = now;
      return false;
    }
    if (!in_flight_ && pending_value_ == reported_) {
      settle();
      shown_ = reported_;
      return false;
    }
    value = pending_value_;
    sent_value_ = value;
    in_flight_ = true;
    confirm_on_ = true;
    confirm_since_ = now;
    return true;
  }

  // Call often (the tile's on_tick). True when the shown value went back to the real one.
  bool tick(uint32_t now) {
    if (confirm_on_ && expired(now, confirm_since_, CONFIRM_MS)) {
      settle();
      shown_ = reported_;
      return true;
    }
    return false;
  }

  // The entity is gone/unavailable: forget everything but the range.
  void reset() {
    settle();
    reported_ = shown_ = NO_VALUE;
  }

  int shown() const { return shown_; }
  int reported() const { return reported_; }
  bool pending() const { return pending_; }
  const SetpointRange &range() const { return range_; }

 private:
  void settle() {
    pending_ = false;
    debounce_on_ = false;
    confirm_on_ = false;
    in_flight_ = false;
    pending_value_ = sent_value_ = NO_VALUE;
  }

  SetpointRange range_;
  int reported_ = NO_VALUE, shown_ = NO_VALUE, pending_value_ = NO_VALUE, sent_value_ = NO_VALUE;
  bool pending_ = false, debounce_on_ = false, confirm_on_ = false, in_flight_ = false;   // in_flight_: sent, not confirmed
  uint32_t debounce_since_ = 0, confirm_since_ = 0;
};

// ---------------------------------------------------------------------------------------------------------------
// What the tile shows

constexpr size_t MAX_NAME_BYTES = 48;   // program name kept for the pill

enum class Pill {
  UNAVAILABLE,   // the climate (or the room sensor / the thermostat behind it) is not there: dash, inert keys
  CHOOSE_ROOM,   // no room sensor chosen in the tile's options
  PAUSED,        // the room is paused
  MANUAL,        // a manual override is active
  AUTO,          // following the program
  NO_PROGRAM,    // the room has no program assigned
  PROBLEM,       // status says a command cannot be applied (unsupported mode/preset, out of range, error, unknown)
};

// Everything the tile reads. Times are epoch seconds (0 or less: absent); the `*_time` attributes of the room sensor.
struct TileInput {
  std::string climate_state;   // state of the tile's climate entity; "" when there is no data
  bool has_room = false;       // the `room` option is filled in
  std::string room_state;      // state of the room sensor = name of the program ("unknown" when none)
  std::string status;          // `status` attribute: ok, paused, unavailable, unsupported_mode, ...
  bool manual = false;
  int64_t slot_end = 0;        // slot_end_time
  int64_t override_until = 0;  // override_until_time
  int next_mode = -1;          // next_mode code, -1 when absent
  double next_temp = NAN;      // next_temp, NaN when absent
  int64_t next_start = 0;      // next_start_time
};

struct TileView {
  Pill pill = Pill::UNAVAILABLE;
  std::string program;         // name of the program (AUTO, MANUAL, PROBLEM; empty otherwise)
  bool show_until = false;     // "until HH:MM": `until` is valid
  int64_t until = 0;           // epoch s. Show it as "until" only when pill == MANUAL (the override's end); in AUTO and
                               // PROBLEM it is the end of the slot (the tile may ignore it)
  bool has_next = false;       // "then ...": next_* are valid
  Mode next_mode = Mode::OFF;
  bool next_has_temp = false;
  double next_temp = 0;
  int64_t next_start = 0;
  bool controls = false;       // + and - work
  bool can_resume = false;     // the "MANUAL x" / Auto key can resume the program
  bool bar_visible = false;    // the day bar is worth drawing
};

namespace detail {
inline std::string cut_name(const std::string &s) {
  if (s.size() <= MAX_NAME_BYTES) return s;
  size_t n = MAX_NAME_BYTES;
  while (n > 0 && ((unsigned char) s[n] & 0xC0) == 0x80) --n;   // do not cut a UTF-8 character in two
  return s.substr(0, n);
}
inline bool is_down(const std::string &s) { return s.empty() || s == "unavailable" || s == "unknown"; }
}  // namespace detail

// Order: climate down, no room chosen, room down (unavailable, missing), paused, no program (unassigned, not_running),
// problem (unsupported_*, out_of_range, error, unknown status), manual, no program (empty state), auto.
inline TileView tile_view(const TileInput &in) {
  TileView v;
  if (detail::is_down(in.climate_state)) return v;   // UNAVAILABLE
  if (!in.has_room) {
    v.pill = Pill::CHOOSE_ROOM;
    return v;
  }
  if (in.room_state == "unavailable" || in.status == "unavailable" || in.status == "missing") return v;
  if (in.status == "paused") {
    v.pill = Pill::PAUSED;
    return v;
  }
  const bool ok = in.status.empty() || in.status == "ok";
  const bool no_program = in.room_state.empty() || in.room_state == "unknown";
  if (in.status == "unassigned" || in.status == "not_running") {   // nothing to show, nothing to command
    v.pill = Pill::NO_PROGRAM;
    return v;
  }
  if (!ok) v.pill = Pill::PROBLEM;
  else if (in.manual) v.pill = Pill::MANUAL;
  else if (no_program) v.pill = Pill::NO_PROGRAM;
  else v.pill = Pill::AUTO;
  if (v.pill == Pill::NO_PROGRAM) return v;
  if (!no_program) v.program = detail::cut_name(in.room_state);
  v.controls = true;
  v.bar_visible = true;
  v.can_resume = in.manual;   // also in PROBLEM: an override can be dropped whatever the status
  v.until = v.pill == Pill::MANUAL ? in.override_until : in.slot_end;
  v.show_until = v.until > 0;
  if (!v.show_until) v.until = 0;
  if (in.next_start > 0 && in.next_mode >= 0 && in.next_mode <= 2) {
    v.has_next = true;
    v.next_mode = mode_from_code(in.next_mode);
    v.next_has_temp = std::isfinite(in.next_temp) && in.next_temp > NO_TEMP;
    v.next_temp = v.next_has_temp ? in.next_temp : 0;
    v.next_start = in.next_start;
  }
  return v;
}

// ---------------------------------------------------------------------------------------------------------------
// The card: one row per band

struct Row {
  int start = 0, end = 0;
  Mode mode = Mode::OFF;
  bool has_temp = false;
  double temp = 0;
  bool autom = false;
  bool current = false;   // the band of the local time: start <= now < end
};

// At most MAX_BANDS rows; when the day has more, the window that holds the current band.
inline std::vector<Row> card_rows(const DayLists &l, int now_minute) {
  const int now = std::min(std::max(now_minute, 0), MINUTES_PER_DAY - 1);
  std::vector<Row> rows;
  for (const Band &b : normalize_bands(l, now)) {
    Row r;
    r.start = b.start;
    r.end = b.end;
    r.mode = b.mode;
    r.has_temp = b.has_temp;
    r.temp = b.temp;
    r.autom = b.autom;
    r.current = b.start <= now && now < b.end;
    rows.push_back(r);
  }
  return rows;
}

}  // namespace esphome::cronotermostato
