// Cronotermostato: the room tile (the target, the temperature now, today's bands on a bar) and the card with the list of
// today's bands. This is the drawing only (plan 3, step 4): the keys + / - and Auto / Manual act in step 5.
// The maths (bands to pixels, texts, which pill) is in cronotermostato_logic.h, tested on a computer; here it becomes
// widgets. What was tried before is in docs/SPIKE.md.
#include "cronotermostato.h"
#include "cronotermostato_logic.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace esphome::cronotermostato {

static const char *const TAG = "cronotermostato";
using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

constexpr int MAX_SEGMENTS = (int) MAX_BANDS;   // the most bands the bar draws and the card lists
// The signs the fonts may lack, each with what stands in for it.
constexpr const char *MINUS = "\xE2\x88\x92";    // U+2212, the true minus
constexpr const char *EN_DASH = "\xE2\x80\x93";  // U+2013

// ---------------------------------------------------------------------------------------------------------------
// Reading the data (the tile and the card get the same message)

static double number(JsonVariantConst v, double fallback) {
  return v.is<double>() || v.is<float>() || v.is<int>() || v.is<long>() ? v.as<double>() : fallback;
}

// The lists of today's bands, as numbers; a value that is not a number becomes NaN (the logic drops that band).
// Only as many entries as the logic ever looks at, and the vectors live for the length of the call.
static DayLists read_lists(JsonObjectConst r) {
  DayLists l;
  JsonArrayConst s = r["today_start"], e = r["today_end"], m = r["today_mode"], t = r["today_temp"], a = r["today_auto"];
  const size_t n = std::min({s.size(), e.size(), m.size(), t.size(), MAX_LIST_ITEMS});
  l.start.reserve(n);
  l.end.reserve(n);
  l.mode.reserve(n);
  l.temp.reserve(n);
  l.autom.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    l.start.push_back(number(s[i], NAN));
    l.end.push_back(number(e[i], NAN));
    l.mode.push_back(number(m[i], 0));
    l.temp.push_back(number(t[i], NO_TEMP));
    l.autom.push_back(i < a.size() ? number(a[i], 0) : 0);
  }
  return l;
}

// What tile_view reads. `wait` (no data) gives an empty climate state, which the logic reads as "not available".
static TileInput read_input(JsonObjectConst data) {
  TileInput in;
  if (data["wait"].is<const char *>()) return in;
  in.climate_state = data["state"] | "";
  JsonObjectConst room = data["entities"]["room"];
  in.has_room = !room.isNull();
  in.room_state = room["state"] | "";
  JsonObjectConst r = room["attributes"];
  in.status = r["status"] | "";
  in.manual = r["manual"] | false;
  // Never cast a raw number: not finite or out of range would be undefined behaviour.
  in.slot_end = (int64_t) clamp_to(number(r["slot_end_time"], 0), 0, MAX_EPOCH, 0);
  in.override_until = (int64_t) clamp_to(number(r["override_until_time"], 0), 0, MAX_EPOCH, 0);
  in.next_mode = (int) clamp_to(number(r["next_mode"], -1), -1, 99, -1);
  in.next_temp = number(r["next_temp"], NAN);
  in.next_start = (int64_t) clamp_to(number(r["next_start_time"], 0), 0, MAX_EPOCH, 0);
  return in;
}

static char decimal_of(const tessera::Plugin *plugin) {
  const char *separator = plugin->text("decimal");
  return *separator ? *separator : '.';
}

static Tone tone_for(Mode mode) {
  switch (tone_of(mode)) {
    case BarTone::AMBER: return Tone::BUSY;
    case BarTone::BLUE: return Tone::ACCENT;
    default: return Tone::NORMAL;
  }
}

// A block in the colour of a mode: heat is Home Assistant's amber, cool the look's blue, off the grey role SUBTLE.
static void paint_mode(lv_obj_t *block, Mode mode) { ui::set_fill(block, tone_for(mode), theme::SUBTLE); }

static const char *mode_word(const tessera::Plugin *plugin, Mode mode, bool shortly) {
  switch (mode) {
    case Mode::HEAT: return plugin->text(shortly ? "heat_s" : "heat");
    case Mode::COOL: return plugin->text(shortly ? "cool_s" : "cool");
    default: return plugin->text("off");
  }
}

// Show or hide a part, touching LVGL only when the state really changes.
static void show(lv_obj_t *obj, bool on) {
  if (!obj) return;
  const bool hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
  if (hidden != on) return;   // already as asked
  if (on) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static int minute_of(uint32_t epoch) {
  if (!epoch) return -1;
  const tessera::LocalTime lt = tessera::local_time(epoch);
  return lt.hour * 60 + lt.minute;
}

// ---------------------------------------------------------------------------------------------------------------
// The tile

class RoomTile : public tessera::Tile {
 public:
  explicit RoomTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    std::snprintf(entity_, sizeof entity_, "%s", c.entity);
    tile_ = c.tile;
    w_ = c.width;
    h_ = c.height;
    decimal_ = decimal_of(plugin_);
    const int gap = ui::px(4);
    const int body_h = ui::line_height(Font::BODY);
    bar_h_ = std::max({ui::px(8), ui::mm(2.5f), h_ / 40});   // thicker on a large tile
    tick_h_ = bar_h_ + 2 * ui::px(2);
    tick_w_ = std::max(2, ui::px(2));
    tick_edge_ = std::max(1, ui::px(1));   // the tick's rim in the tile's ground, so it shows on the dark grey too
    const bool has_name = c.name && *c.name;

    // What fits, richest first (choose_layout in the logic, tested): the large digits, the room's name, the "then" line.
    LayoutInput li;
    li.height = h_;
    li.digits_h = ui::line_height(Font::DIGITS);
    li.value_h = ui::line_height(Font::VALUE);
    li.body_h = body_h;
    li.tick_h = tick_h_;
    li.gap = gap;
    li.has_name = c.name && *c.name;
    const LayoutPlan plan = choose_layout(li);
    value_font_ = plan.digits ? Font::DIGITS : Font::VALUE;
    show_name_ = plan.name;
    show_then_ = plan.then;
    const int value_h = plan.value_h;
    bar_y_ = plan.bar_y + (tick_h_ - bar_h_) / 2;
    pill_y_ = plan.pill_y;
    then_y_ = plan.then_y;
    const int top = plan.region_top;
    const int region_h = plan.region_h;

    // Plus at the LEFT, minus at the RIGHT (Alex's choice); the target and the temperature now stand between them.
    key_h_ = std::min(std::min(std::max(ui::mm(12), region_h / 3), ui::mm(18)), region_h);   // larger on a large tile
    key_w_ = std::min(std::min(std::max(ui::mm(11), w_ / 8), ui::mm(18)), w_ / 4);
    key_y_ = top + (region_h - key_h_) / 2;
    keys_bottom_ = key_y_ + key_h_;
    minus_ = ui::covers(MINUS, Font::BODY_LARGE) ? MINUS : "-";
    for (int i = 0; i < 2; ++i) {
      key_[i] = ui::block(c.parent, theme::TRACK);
      key_label_[i] = ui::label(key_[i], Font::BODY_LARGE, theme::INK);
      lv_obj_set_size(key_[i], key_w_, key_h_);
      lv_obj_set_pos(key_[i], i == 0 ? 0 : w_ - key_w_, key_y_);
      ui::set_text(key_label_[i], i == 0 ? "+" : minus_);
      lv_obj_center(key_label_[i]);
    }
    mid_x_ = key_w_ + gap;
    mid_w_ = std::max(1, w_ - 2 * mid_x_);
    set_y_ = plan.mid_y;
    now_y_ = set_y_ + value_h;

    name_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(name_, w_);
    lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(name_, 0, 0);
    ui::set_text(name_, c.name ? c.name : "");
    show(name_, show_name_);

    set_ = ui::label(c.parent, value_font_, theme::INK);
    lv_obj_set_width(set_, mid_w_);
    lv_obj_set_style_text_align(set_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(set_, mid_x_, set_y_);
    now_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_pos(now_, mid_x_, now_y_);
    dot_ = ui::block(c.parent, theme::TRACK);
    dot_d_ = std::max(ui::px(8), body_h / 2);
    lv_obj_set_size(dot_, dot_d_, dot_d_);
    lv_obj_set_style_radius(dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(dot_, LV_OBJ_FLAG_HIDDEN);

    then_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(then_, w_);
    lv_obj_set_style_text_align(then_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(then_, 0, then_y_);
    show(then_, show_then_);
    pill_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(pill_, w_);
    lv_obj_set_style_text_align(pill_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(pill_, 0, pill_y_);

    // The bar: a track with the bands inside it (the track clips them to its rounded ends), and the "now" tick over it.
    track_ = ui::block(c.parent, theme::TRACK);
    lv_obj_set_size(track_, w_, bar_h_);
    lv_obj_set_pos(track_, 0, bar_y_);
    lv_obj_set_style_pad_all(track_, 0, 0);
    lv_obj_set_style_radius(track_, ui::px(3), 0);
    lv_obj_set_style_clip_corner(track_, true, 0);
    for (int i = 0; i < MAX_SEGMENTS; ++i) {
      seg_[i] = ui::block(track_, theme::SUBTLE);
      lv_obj_set_style_radius(seg_[i], 0, 0);   // square joints: neighbours touch without a notch
      lv_obj_add_flag(seg_[i], LV_OBJ_FLAG_HIDDEN);
    }
    tick_ = ui::block(c.parent, theme::INK);
    lv_obj_set_size(tick_, tick_w_ + 2 * tick_edge_, tick_h_);
    lv_obj_set_pos(tick_, 0, bar_y_ - (tick_h_ - bar_h_) / 2);
    lv_obj_set_style_border_width(tick_, tick_edge_, 0);
    lv_obj_set_style_border_opa(tick_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tick_, ui::color(theme::CARD), 0);
    lv_obj_add_flag(tick_, LV_OBJ_FLAG_HIDDEN);

    // The tap zone: the bar and the strip over it, up to 7 mm high but never over the keys, so it can be less (about 6 mm
    // on the CYD). A screen that cannot tell where the finger was (tap_point() gives -1,-1) never opens the card.
    zone_y_ = std::max(h_ - std::max(tick_h_, ui::mm(7)), keys_bottom_);
  }

  void on_state(JsonObjectConst data) override {
    const TileInput in = read_input(data);
    const TileView view = tile_view(in);
    JsonObjectConst a = data["attributes"];
    climate_down_ = detail::is_down(in.climate_state);
    kind_ = view.pill;
    controls_ = view.controls;
    bar_visible_ = view.bar_visible;
    program_ = roomy::text(view.program);
    show_until_ = view.show_until && view.until > 0;
    until_ = show_until_ ? (uint32_t) view.until : 0;
    has_next_ = view.has_next;
    next_mode_ = view.next_mode;
    next_has_temp_ = view.next_has_temp;
    next_temp_ = view.next_temp;
    next_start_ = view.has_next ? (uint32_t) view.next_start : 0;

    // The setpoint: the logic keeps the value shown (the keys of step 5 move it); here it follows the climate.
    step_ = number(a["target_temp_step"], 0.5);
    setpoint_.set_range(make_range(number(a["min_temp"], 7), number(a["max_temp"], 35), step_));
    if (climate_down_) setpoint_.reset();
    else setpoint_.on_state(number(a["temperature"], NAN), 0);
    current_ = climate_down_ ? NAN : number(a["current_temperature"], NAN);
    hvac_ = hvac_dot(a["hvac_action"] | "");   // no hvac_action, no dot

    // The bar: the day's bands to pixels, worked out here and not every second. A day with more than 16 bands keeps
    // the window around the band of now (chosen when the data comes, and it comes at every change of band).
    nseg_ = 0;
    const uint32_t now = tessera::epoch();
    if (bar_visible_ && !climate_down_) {
      JsonObjectConst r = data["entities"]["room"]["attributes"];
      const Day day = normalize_day(read_lists(r), now ? minute_of(now) : -1);
      for (const Segment &s : bar_segments(day.bands, w_)) {
        if (nseg_ >= MAX_SEGMENTS) break;
        seg_data_[nseg_++] = s;
      }
      // day.truncated_before / truncated_after are not drawn: a day with more than 16 bands is far from real, the part
      // left out stays the empty track (nothing hides it), and the window always holds the band of now.
    }
    paint();
    minute_ = -2;   // the tick and the pill are set again on the next on_tick
  }

  void on_tick(uint32_t epoch) override {
    const int minute = minute_of(epoch);
    if (minute == minute_) return;
    minute_ = minute;
    paint_clock(epoch, minute);
  }

  void on_theme() override {
    paint();
    paint_clock(tessera::epoch(), minute_of(tessera::epoch()));
  }

  // Only a tap on the bar (and the strip over it) opens the list of today's bands. A tap anywhere else does nothing
  // here; the keys act in step 5.
  void on_tap() override {
    if (!bar_visible_) return;
    const tessera::Point at = tessera::tap_point();
    if (at.x < 0 || at.y < zone_y_ || at.x >= w_) return;
    tessera::open_card(plugin_->plugin_id(), "day", entity_, tile_);
  }

 private:
  bool fits(const std::string &text) const { return ui::text_width(text, Font::BODY) <= w_; }

  // The pill: the program ("Auto" and its name), a manual override and when it ends, or why there is none.
  std::string pill_text() const {
    switch (kind_) {
      case Pill::UNAVAILABLE: return plugin_->text("unavailable");
      case Pill::CHOOSE_ROOM: return plugin_->text("no_room");
      case Pill::PAUSED: return plugin_->text("paused");
      case Pill::NO_PROGRAM: return plugin_->text("no_program");
      case Pill::PROBLEM: return plugin_->text("problem");
      case Pill::MANUAL: {
        if (!show_until_) return plugin_->text("manual");
        const std::string time = tessera::clock_text(until_);
        const std::string full = tessera::fill(plugin_->text("manual_until"), "time", time);
        return fits(full) ? full : tessera::fill(plugin_->text("until"), "time", time);
      }
      default: {
        if (program_.empty()) return plugin_->text("auto");
        const std::string named = tessera::fill(plugin_->text("auto_named"), "name", roomy::plain(program_));
        return fits(named) ? named : std::string(plugin_->text("auto"));
      }
    }
  }

  // "Then Heating 21.5° at 17:00": the long word for the mode, else the short one, else nothing.
  std::string then_text() const {
    if (!has_next_ || next_start_ == 0 || kind_ == Pill::NO_PROGRAM || kind_ == Pill::PAUSED || climate_down_) return "";
    const std::string time = tessera::clock_text(next_start_);
    const std::string temp = next_has_temp_ ? format_temp(next_temp_, step_, decimal_, true) : "";
    for (bool shortly : {false, true}) {
      std::string value = mode_word(plugin_, next_mode_, shortly);
      if (!temp.empty() && next_mode_ != Mode::OFF) value += " " + temp;
      const std::string text = tessera::fill(tessera::fill(plugin_->text("then"), "value", value).c_str(), "time", time);
      if (fits(text)) return text;
    }
    return "";
  }

  // What does not depend on the clock: the numbers, the words and the bands. Set only where it differs.
  void paint() {
    const bool alive = !climate_down_;
    // The target. Large digits when the font has every character of it, else the next face down.
    std::string target = "-";
    if (alive && setpoint_.shown() != NO_VALUE) target = format_temp(to_degrees(setpoint_.shown()), step_, decimal_, true);
    const Font face = value_font_ == Font::DIGITS && !ui::covers(target, Font::DIGITS) ? Font::VALUE : value_font_;
    ui::set_font(set_, face);
    ui::set_text(set_, target);
    ui::set_color(set_, alive ? theme::INK : theme::MUTED);
    // The temperature now, with the dot of what the thermostat does (amber heating, blue cooling) before it.
    std::string now;
    if (alive && std::isfinite(current_)) {
      const std::string t = format_temp(current_, step_, decimal_, true);
      if (t != "-") now = tessera::fill(plugin_->text("now"), "n", t);
    }
    ui::set_text(now_, now);
    ui::set_color(now_, theme::MUTED);
    const int nw = now.empty() ? 0 : ui::text_width(now, Font::BODY);
    const bool dot = alive && hvac_ != 0;
    const int dot_gap = dot ? ui::px(6) : 0;
    const int total = nw + (dot ? dot_d_ + dot_gap : 0);
    const int x0 = mid_x_ + std::max(0, (mid_w_ - total) / 2);
    const int body_h = ui::line_height(Font::BODY);
    lv_obj_set_width(now_, std::max(1, nw + ui::px(2)));
    lv_obj_set_pos(now_, x0 + (dot ? dot_d_ + dot_gap : 0), now_y_);
    show(dot_, dot);
    if (dot) {
      lv_obj_set_pos(dot_, x0, now_y_ + (body_h - dot_d_) / 2);
      ui::set_fill(dot_, hvac_ == 1 ? Tone::BUSY : Tone::ACCENT, theme::TRACK);
    }
    ui::set_color(name_, theme::MUTED);
    for (int i = 0; i < 2; ++i) {
      ui::set_fill(key_[i], Tone::NORMAL, theme::TRACK);
      ui::set_color(key_label_[i], controls_ ? theme::INK : theme::MUTED);
    }

    // The bar: a band is a block inside the track; with no bar to show the track stays empty (switched off).
    const bool bar = bar_visible_ && alive;
    for (int i = 0; i < MAX_SEGMENTS; ++i) {
      if (!bar || i >= nseg_) {
        show(seg_[i], false);
        continue;
      }
      const Segment &s = seg_data_[i];
      show(seg_[i], true);
      lv_obj_set_pos(seg_[i], s.x0, 0);
      lv_obj_set_size(seg_[i], std::max(1, s.x1 - s.x0), bar_h_);
      paint_mode(seg_[i], s.mode);
    }
    ui::set_fill(track_, Tone::NORMAL, theme::TRACK);
    ui::set_fill(tick_, Tone::NORMAL, theme::INK);
    lv_obj_set_style_border_color(tick_, ui::color(theme::CARD), 0);
  }

  // What follows the clock: the tick on the bar, the pill and the "then" line.
  void paint_clock(uint32_t epoch, int minute) {
    // No tick without a band to stand on (empty lists), nor when the bar is off.
    const bool bar = bar_visible_ && !climate_down_ && nseg_ > 0;
    show(tick_, bar && epoch && minute >= 0);
    if (bar && epoch && minute >= 0) {
      const int x = now_marker(seg_data_, (size_t) nseg_, minute, w_);
      const int full = tick_w_ + 2 * tick_edge_;
      lv_obj_set_x(tick_, std::min(std::max(0, x - full / 2), w_ - full));
    }
    ui::set_text(pill_, pill_text());
    // INK for a manual override or a problem (it asks to be seen), MUTED for the rest; never amber or blue as text.
    ui::set_color(pill_, kind_ == Pill::MANUAL || kind_ == Pill::PROBLEM ? theme::INK : theme::MUTED);
    if (show_then_) {
      ui::set_text(then_, then_text());
      ui::set_color(then_, theme::MUTED);
    }
  }

  const tessera::Plugin *plugin_;
  char entity_[96]{};
  int w_ = 0, h_ = 0, tile_ = -1;
  int bar_y_ = 0, bar_h_ = 0, tick_h_ = 0, tick_w_ = 0, tick_edge_ = 1, pill_y_ = 0, then_y_ = 0, set_y_ = 0, now_y_ = 0, zone_y_ = 0;
  int key_y_ = 0, key_h_ = 0, key_w_ = 0, keys_bottom_ = 0, mid_x_ = 0, mid_w_ = 0, dot_d_ = 0;
  Font value_font_ = Font::VALUE;
  bool show_name_ = false, show_then_ = false;
  char decimal_ = '.';
  const char *minus_ = "-";
  // What the last on_state said (texts as tessera::Text, in PSRAM).
  Pill kind_ = Pill::UNAVAILABLE;
  bool climate_down_ = true, controls_ = false, bar_visible_ = false, show_until_ = false, has_next_ = false,
       next_has_temp_ = false;
  Mode next_mode_ = Mode::OFF;
  double next_temp_ = 0, step_ = 0.5, current_ = NAN;
  uint32_t until_ = 0, next_start_ = 0;
  int hvac_ = 0;   // 0 nothing, 1 heating, 2 cooling
  tessera::Text program_;
  Setpoint setpoint_;
  Segment seg_data_[MAX_SEGMENTS];
  int nseg_ = 0;
  int minute_ = -2;
  lv_obj_t *name_{}, *pill_{}, *set_{}, *now_{}, *dot_{}, *then_{}, *track_{}, *tick_{};
  lv_obj_t *key_[2]{}, *key_label_[2]{}, *seg_[MAX_SEGMENTS]{};
};

// ---------------------------------------------------------------------------------------------------------------
// The card: one row per band of today, "08:00-12:00  Heating  21.5°", the band of now marked.

class DayCard : public tessera::Card {
 public:
  explicit DayCard(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    w_ = c.width;
    decimal_ = decimal_of(plugin_);
    const int pad = ui::px(6), gap = ui::px(8);
    const int body_h = ui::line_height(Font::BODY);
    line_ = body_h + ui::px(8);
    chip_w_ = ui::px(5);
    dash_ = ui::covers(EN_DASH, Font::BODY) ? EN_DASH : "-";
    time_w_ = ui::text_width(std::string("88:88") + dash_ + "88:88", Font::BODY);
    temp_w_ = ui::text_width("88.8\xC2\xB0", Font::BODY);
    now_w_ = ui::text_width(plugin_->text("right_now"), Font::BODY);
    // The mode column holds the widest of the three words; a narrow card takes the short words and drops "Now".
    int mode_long = 0, mode_short = 0;
    for (Mode m : {Mode::HEAT, Mode::COOL, Mode::OFF}) {
      mode_long = std::max(mode_long, ui::text_width(mode_word(plugin_, m, false), Font::BODY));
      mode_short = std::max(mode_short, ui::text_width(mode_word(plugin_, m, true), Font::BODY));
    }
    const int fixed = 2 * pad + chip_w_ + gap + time_w_ + gap + temp_w_ + gap;
    show_now_ = fixed + mode_long + gap + now_w_ <= w_;
    short_ = !show_now_ && fixed + mode_long > w_;
    mode_w_ = short_ ? mode_short : mode_long;
    // A line over the rows says the program (and a holiday); the rows follow, as many as the height holds.
    header_ = c.height >= 5 * line_;
    top_ = header_ ? line_ : 0;
    rows_ = std::max(1, std::min(MAX_SEGMENTS, (c.height - top_) / line_));
    hdr_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(hdr_, w_ - 2 * pad);
    lv_obj_set_pos(hdr_, pad, (line_ - body_h) / 2);
    show(hdr_, header_);
    more_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(more_, w_ - 2 * pad);
    lv_obj_set_style_text_align(more_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(more_, pad, 0);
    show(more_, false);
    note_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(note_, w_ - 2 * pad);
    lv_obj_set_pos(note_, pad, top_ + (line_ - body_h) / 2);
    for (int i = 0; i < rows_; ++i) {
      const int y = top_ + i * line_;
      const int ty = y + (line_ - body_h) / 2;
      Line &r = row_[i];
      // The row of now: a dark stripe at the left edge, its text in full ink (the other rows in a softer one).
      r.hl = ui::block(c.parent, theme::INK);
      lv_obj_set_size(r.hl, std::max(2, ui::px(3)), line_ - ui::px(4));
      lv_obj_set_pos(r.hl, ui::px(1), y + ui::px(2));
      r.chip = ui::block(c.parent, theme::SUBTLE);
      lv_obj_set_size(r.chip, chip_w_, line_ - ui::px(8));
      lv_obj_set_pos(r.chip, pad, y + ui::px(4));
      r.time = ui::label(c.parent, Font::BODY, theme::INK);
      r.mode = ui::label(c.parent, Font::BODY, theme::INK);
      r.temp = ui::label(c.parent, Font::BODY, theme::INK);
      const int x_time = pad + chip_w_ + gap;
      lv_obj_set_width(r.time, time_w_ + ui::px(2));
      lv_obj_set_pos(r.time, x_time, ty);
      lv_obj_set_width(r.mode, mode_w_ + ui::px(2));
      lv_obj_set_pos(r.mode, x_time + time_w_ + gap, ty);
      lv_obj_set_width(r.temp, temp_w_ + ui::px(2));
      lv_obj_set_style_text_align(r.temp, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_pos(r.temp, w_ - pad - temp_w_ - ui::px(2), ty);
      if (show_now_) {
        r.now = ui::label(c.parent, Font::BODY, theme::INK);
        lv_obj_set_width(r.now, now_w_ + ui::px(2));
        lv_obj_set_style_text_align(r.now, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(r.now, w_ - pad - temp_w_ - gap - now_w_ - ui::px(2), ty);
      }
    }
  }

  void on_state(JsonObjectConst data) override {
    const TileInput in = read_input(data);
    const TileView view = tile_view(in);
    JsonObjectConst r = data["entities"]["room"]["attributes"];
    step_ = number(data["attributes"]["target_temp_step"], 0.5);
    program_ = roomy::text(view.program);
    holiday_ = r["holiday"] | false;
    note_key_ = nullptr;
    n_ = 0;
    switch (view.pill) {
      case Pill::UNAVAILABLE: note_key_ = "unavailable"; break;
      case Pill::CHOOSE_ROOM: note_key_ = "no_room"; break;
      case Pill::NO_PROGRAM: note_key_ = "no_program"; break;
      case Pill::PAUSED: note_key_ = "paused"; break;
      default: break;
    }
    const uint32_t now = tessera::epoch();
    if (!note_key_) {
      for (const Row &row : card_rows(read_lists(r), now ? std::max(0, minute_of(now)) : 0)) {
        if (n_ >= MAX_SEGMENTS) break;
        rows_data_[n_] = row;
        rows_data_[n_].current = now && row.current;
        ++n_;
      }
      if (n_ == 0) note_key_ = "no_bands";
    }
    minute_ = minute_of(now);
    paint();
  }

  // The band of now moves with the clock; the rows are the same.
  void on_tick(uint32_t epoch) override {
    const int minute = minute_of(epoch);
    if (minute == minute_) return;
    minute_ = minute;
    for (int i = 0; i < n_; ++i)
      rows_data_[i].current = minute >= 0 && rows_data_[i].start <= minute && minute < rows_data_[i].end;
    paint();
  }

  void on_theme() override { paint(); }

 private:
  struct Line {
    lv_obj_t *hl{}, *chip{}, *time{}, *mode{}, *temp{}, *now{};
  };

  void paint() {
    // The header: the program's name and "Holiday" when today is one.
    std::string head = roomy::plain(program_);
    if (holiday_) head += (head.empty() ? "" : " \xC2\xB7 ") + std::string(plugin_->text("holiday"));
    ui::set_text(hdr_, head);
    ui::set_color(hdr_, theme::MUTED);
    ui::set_text(note_, note_key_ ? plugin_->text(note_key_) : "");
    ui::set_color(note_, theme::MUTED);
    // The window of rows: all of them, or the ones around the band of now.
    int cur = 0;
    for (int i = 0; i < n_; ++i)
      if (rows_data_[i].current) cur = i;
    // More bands than rows: the last row's place says how many are left out ("+3 more").
    const int vis = n_ > rows_ && rows_ > 1 ? rows_ - 1 : rows_;
    const int first = n_ > vis ? std::min(std::max(cur - vis / 2, 0), n_ - vis) : 0;
    const bool more = n_ > vis;
    show(more_, more);
    if (more) {
      lv_obj_set_y(more_, top_ + vis * line_ + (line_ - ui::line_height(Font::BODY)) / 2);
      ui::set_text(more_, tessera::fill(plugin_->text("more"), "n", std::to_string(n_ - vis)));
      ui::set_color(more_, theme::MUTED);
    }
    for (int i = 0; i < rows_; ++i) {
      Line &r = row_[i];
      const int idx = first + i;
      const bool shown = i < vis && idx < n_;
      for (lv_obj_t *part : {r.chip, r.time, r.mode, r.temp}) show(part, shown);
      if (!shown) {
        show(r.hl, false);
        show(r.now, false);
        continue;
      }
      const Row &d = rows_data_[idx];
      show(r.hl, d.current);
      show(r.now, d.current);
      paint_mode(r.chip, d.mode);
      ui::set_text(r.time, format_clock(d.start) + dash_ + format_clock(d.end));
      ui::set_text(r.mode, mode_word(plugin_, d.mode, short_));
      ui::set_text(r.temp, d.has_temp ? format_temp(d.temp, step_, decimal_, true) : "");
      if (r.now) ui::set_text(r.now, plugin_->text("right_now"));
      const theme::Role ink = d.current ? theme::INK : theme::INK_SOFT;
      ui::set_fill(r.hl, Tone::NORMAL, theme::INK);
      ui::set_color(r.time, ink);
      ui::set_color(r.mode, ink);
      ui::set_color(r.temp, ink);
      if (r.now) ui::set_color(r.now, theme::INK);
    }
  }

  const tessera::Plugin *plugin_;
  int w_ = 0, line_ = 0, top_ = 0, rows_ = 0, chip_w_ = 0, time_w_ = 0, temp_w_ = 0, now_w_ = 0, mode_w_ = 0;
  bool header_ = false, show_now_ = false, short_ = false, holiday_ = false;
  char decimal_ = '.';
  const char *dash_ = "-";
  const char *note_key_ = nullptr;
  double step_ = 0.5;
  int n_ = 0, minute_ = -2;
  tessera::Text program_;
  Row rows_data_[MAX_SEGMENTS];
  Line row_[MAX_SEGMENTS];
  lv_obj_t *hdr_{}, *note_{}, *more_{};
};

void Cronotermostato::setup() {
  add_tile("room", [this]() { return new RoomTile(this); });
  add_card("day", [this]() { return new DayCard(this); }, false);
}

}  // namespace esphome::cronotermostato
