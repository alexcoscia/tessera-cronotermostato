// Cronotermostato: the room tile (the target, the temperature now, today's bands on a bar) and the card with the list of
// today's bands. The keys + and - force the setpoint (cronotermostato.set_manual), the pill "Manual x" goes back to the
// program (cronotermostato.resume_auto), the bar opens the card (plan 3, steps 4 and 5).
// The maths (bands to pixels, texts, which pill) is in cronotermostato_logic.h, tested on a computer; here it becomes
// widgets. What was tried before is in docs/SPIKE.md.
#include "cronotermostato.h"
#include "cronotermostato_pad.h"
#include "esphome/core/hal.h"
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
constexpr const char *CROSS = "\xE2\x9C\x95";     // U+2715, the cross on the pill that goes back to automatic
constexpr const char *TIMES = "\xC3\x97";           // U+00D7, what stands in for it

// ---------------------------------------------------------------------------------------------------------------
// Reading the data (the tile and the card get the same message)

static double number(JsonVariantConst v, double fallback) {
  return v.is<double>() || v.is<float>() || v.is<int>() || v.is<long>() ? v.as<double>() : fallback;
}

// The lists of today's bands, as numbers; a value that is not a number becomes NaN (the logic drops that band).
// Only as many entries as the logic ever looks at, and the vectors live for the length of the call.
static DayLists read_lists(JsonObjectConst r) {
  DayLists l;
  JsonArrayConst s = r["today_start"], e = r["today_end"], m = r["today_mode"], t = r["today_temp"], a = r["today_flags"], pr = r["today_preset"];   // flags: bit 1 = automatic band
  const size_t n = std::min({s.size(), e.size(), m.size(), t.size(), MAX_LIST_ITEMS});
  l.start.reserve(n);
  l.end.reserve(n);
  l.mode.reserve(n);
  l.temp.reserve(n);
  l.autom.reserve(n);
  l.preset.reserve(std::min(pr.size(), n));
  for (size_t i = 0; i < n; ++i) {
    if (i < pr.size()) l.preset.emplace_back(pr[i] | "");
    l.start.push_back(number(s[i], NAN));
    l.end.push_back(number(e[i], NAN));
    l.mode.push_back(number(m[i], 0));
    l.temp.push_back(number(t[i], NO_TEMP));
    l.autom.push_back(i < a.size() ? (double) (((int) clamp_to(number(a[i], 0), 0, 255, 0)) & 1) : 0);
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

// The colour of a band: the colour of its mode (heat is Home Assistant's amber, cool the look's blue, off the grey role
// SUBTLE), with a share of the ink mixed in for the second, third and fourth value of the same mode in the day (a tint is a
// shade of the mode's colour in both looks). The text on it is neutral: see text_on().
static lv_color_t band_fill(Mode mode, int tint) {
  const lv_color_t base = ui::tone_color(tone_for(mode), theme::SUBTLE);
  const uint8_t share = tint_share(tint);
  return share ? lv_color_mix(ui::color(theme::INK), base, share) : base;
}
static uint32_t rgb_of(lv_color_t c) { return lv_color_to_u32(c) & ((1u << 24) - 1); }
// A block in the colour of a band; returns that colour (to choose the text on it).
static lv_color_t paint_band(lv_obj_t *block, Mode mode, int tint) {
  const lv_color_t c = band_fill(mode, tint);
  lv_obj_set_style_bg_color(block, c, 0);
  return c;
}
// Of the two neutral roles, ink and the card's ground, the one that reads better on this fill (the roles swap their light
// and dark values, so one of them is always dark and the other light).
static theme::Role text_on(lv_color_t fill) {
  return first_reads_better(rgb_of(fill), rgb_of(ui::color(theme::INK)), rgb_of(ui::color(theme::CARD))) ? theme::INK : theme::CARD;
}

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

// A cut that does not split a UTF-8 character and ends in dots, for a text the room cannot hold (a name a person chose).
static std::string fit_cut(std::string s, int max_w, Font font) {
  if (ui::text_width(s, font) <= max_w) return s;
  const char *dots = ui::covers("\xE2\x80\xA6", font) ? "\xE2\x80\xA6" : "..";
  while (!s.empty()) {
    size_t n = s.size() - 1;
    while (n > 0 && ((unsigned char) s[n] & 0xC0) == 0x80) --n;
    s.resize(n);
    if (ui::text_width(s + dots, font) <= max_w) return s + dots;
  }
  return "";
}

// How many characters (not bytes) a UTF-8 text has.
static int glyphs(const std::string &s) {
  int n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

// ---------------------------------------------------------------------------------------------------------------
// The tile

class RoomTile : public tessera::Tile {
 public:
  explicit RoomTile(Cronotermostato *plugin) : plugin_(plugin) {}
  ~RoomTile() override { plugin_->detach(this); }

  void create(const tessera::TileContext &c) override {
    // The climate of the tile: what the two actions name. Empty when the firmware does not give it, or when it does not fit
    // (a cut id would command another entity): the keys and the pill then stay inert.
    entity_[0] = 0;
    if (c.entity && std::strlen(c.entity) < sizeof entity_) std::snprintf(entity_, sizeof entity_, "%s", c.entity);
    tile_ = c.tile;
    edit_ = c.options["edit"] | true;   // the option: the card may offer to edit the bands (default on)
    w_ = c.width;
    h_ = c.height;
    decimal_ = decimal_of(plugin_);
    const int gap = ui::px(4);
    const int body_h = ui::line_height(Font::BODY);
    bar_h_ = std::max({ui::px(8), ui::mm(2.5f), h_ / 40});   // thicker on a large tile
    tick_h_ = bar_h_ + 2 * ui::px(2);
    tick_w_ = std::max(2, ui::px(2));
    tick_edge_ = std::max(1, ui::px(1));   // the tick's rim in the tile's ground, so it shows on the dark grey too
    dot_d_ = std::max(ui::px(8), body_h / 2);
    dot_gap_ = ui::px(6);
    // Plus at the LEFT, minus at the RIGHT (Alex's choice); the target and the temperature now stand between them.
    key_w_ = std::min(std::max(ui::mm(11), w_ / 8), ui::mm(18));   // a tile too narrow for keys this wide has none

    // What fits, richest first (choose_layout in the logic, tested): by the tile's pixels, not by its cells. The 2x2 look
    // (large digits, the room's name, the "then" line, the pill and the bar), or, when it does not fit, a row with the keys
    // (wide, low), a column with the bar (narrow, tall) or the target alone (narrow, low).
    const std::string sample = std::string("88") + decimal_ + "8\xC2\xB0";
    LayoutInput li;
    li.width = w_;
    li.height = h_;
    li.digits_h = ui::line_height(Font::DIGITS);
    li.value_h = ui::line_height(Font::VALUE);
    li.digits_w = ui::text_width(sample, Font::DIGITS);
    li.value_w = ui::text_width(sample, Font::VALUE);
    li.key_w = key_w_;
    li.dot_w = dot_d_ + dot_gap_;
    li.now_w = ui::text_width(tessera::fill(plugin_->text("now"), "n", sample.substr(0, 4) + "\xC2\xB0"), Font::BODY) + li.dot_w;
    li.touch_h = ui::mm(7);
    li.body_h = body_h;
    li.tick_h = tick_h_;
    li.gap = gap;
    li.has_name = c.name && *c.name;
    LayoutPlan plan = choose_layout(li);
    // Labels of the value on the bars (more than one cell, the full look only): the bar grows to the height of a line of
    // text when the tile then still has the same look, else it stays thin and shows colours and joints only.
    labels_ = false;
    if ((int) c.columns * (int) c.rows > 1 && plan.family == Family::FULL && plan.fits) {
      const int thick = body_h + 2 * ui::px(2);
      if (bar_h_ >= thick) {
        labels_ = true;
      } else {
        LayoutInput taller = li;
        taller.tick_h = thick + 2 * ui::px(2);
        const LayoutPlan other = choose_layout(taller);
        if (other.fits && other.family == plan.family && other.rank == plan.rank) {
          plan = other;
          bar_h_ = thick;
          tick_h_ = taller.tick_h;
          labels_ = true;
        }
      }
    }
    family_ = plan.family;
    has_keys_ = plan.keys;
    has_bar_ = plan.bar;
    has_pill_ = plan.pill;
    now_line_ = plan.now_line;
    value_font_ = plan.digits ? Font::DIGITS : Font::VALUE;
    show_name_ = plan.name;
    show_then_ = plan.then;
    const int value_h = plan.value_h;
    bar_y_ = plan.bar_y + (tick_h_ - bar_h_) / 2;
    pill_y_ = plan.pill_y;
    then_y_ = plan.then_y;
    const int top = plan.region_top;
    const int region_h = plan.region_h;

    key_h_ = std::min(std::min(std::max(ui::mm(12), region_h / 3), ui::mm(18)), region_h);   // larger on a large tile
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
      show(key_[i], has_keys_);
    }
    mid_x_ = has_keys_ ? key_w_ + gap : 0;
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
    lv_obj_set_size(dot_, dot_d_, dot_d_);
    lv_obj_set_style_radius(dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(dot_, LV_OBJ_FLAG_HIDDEN);

    then_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(then_, w_);
    lv_obj_set_style_text_align(then_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(then_, 0, then_y_);
    show(then_, show_then_);
    // The pill sits on a chip, as wide as its text. The chip is the colour of the keys while the pill is a key (manual), and
    // the colour of the tile's ground otherwise, so it is not seen.
    pill_bg_ = ui::block(c.parent, theme::CARD);
    lv_obj_set_style_radius(pill_bg_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(pill_bg_, 0, 0);
    pill_ = ui::label(pill_bg_, Font::BODY, theme::MUTED);
    lv_obj_set_style_text_align(pill_, LV_TEXT_ALIGN_CENTER, 0);
    show(pill_bg_, has_pill_);

    // The bar: a track with the bands inside it (the track clips them to its rounded ends), and the "now" tick over it.
    track_ = ui::block(c.parent, theme::TRACK);
    lv_obj_set_size(track_, w_, bar_h_);
    lv_obj_set_pos(track_, 0, bar_y_);
    lv_obj_set_style_pad_all(track_, 0, 0);
    lv_obj_set_style_radius(track_, ui::px(3), 0);
    lv_obj_set_style_clip_corner(track_, true, 0);
    show(track_, has_bar_);
    for (int i = 0; i < MAX_SEGMENTS; ++i) {
      seg_[i] = ui::block(track_, theme::SUBTLE);
      lv_obj_set_style_radius(seg_[i], 0, 0);   // square ends; the joint between two bands is the track showing through (separate)
      lv_obj_set_style_pad_all(seg_[i], 0, 0);
      lv_obj_add_flag(seg_[i], LV_OBJ_FLAG_HIDDEN);
      if (labels_) {
        seg_label_[i] = ui::label(seg_[i], Font::BODY, theme::INK);
        lv_obj_set_style_text_align(seg_label_[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_flag(seg_label_[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
    tick_ = ui::block(c.parent, theme::INK);
    lv_obj_set_size(tick_, tick_w_ + 2 * tick_edge_, tick_h_);
    lv_obj_set_pos(tick_, 0, bar_y_ - (tick_h_ - bar_h_) / 2);
    lv_obj_set_style_border_width(tick_, tick_edge_, 0);
    lv_obj_set_style_border_opa(tick_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tick_, ui::color(theme::CARD), 0);
    lv_obj_add_flag(tick_, LV_OBJ_FLAG_HIDDEN);

    // The tap zones (zone_at in the logic, tested): the keys' columns, the pill while it is a key, the bar and the strip
    // over it; the rest of the tile is nothing. A screen that cannot tell where the finger was (tap_point() gives -1,-1)
    // does nothing.
    zones_.w = w_;
    zones_.h = h_;
    zones_.key_w = key_w_;
    zones_.key_y = key_y_;
    zones_.key_h = key_h_;
    zones_.gap = gap;
    zones_.key_reach = ui::mm(7);
    now_bottom_ = now_y_ + body_h;
    body_h_ = body_h;
    place_zones();
    cross_ = ui::covers(CROSS, Font::BODY) ? CROSS : ui::covers(TIMES, Font::BODY) ? TIMES : "x";
    plugin_->attach(this);
  }

  void on_state(JsonObjectConst data) override {
    const TileInput in = read_input(data);
    const TileView view = tile_view(in);
    JsonObjectConst a = data["attributes"];
    climate_down_ = detail::is_down(in.climate_state);
    kind_ = view.pill;
    controls_ = view.controls;
    can_set_ = view.can_set && entity_[0] != 0;
    cmd_mode_ = view.command_mode;
    can_resume_ = pill_is_key(view, entity_[0] != 0);   // also while the pill says "Problem"
    resume_.on_state(in.manual);
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
    if (climate_down_) {
      setpoint_.reset();
    } else {
      if (!can_set_) setpoint_.cancel();   // the room can no longer be commanded: nothing waits, nothing goes out
      setpoint_.on_state(number(a["temperature"], NAN), 0);
    }
    current_ = climate_down_ ? NAN : number(a["current_temperature"], NAN);
    hvac_ = hvac_dot(a["hvac_action"] | "");   // no hvac_action, no dot

    // The bar: the day's bands to pixels, worked out here and not every second. A day with more than 16 bands keeps
    // the window around the band of now (chosen when the data comes, and it comes at every change of band).
    nseg_ = 0;
    const uint32_t now = tessera::epoch();
    if (bar_visible_ && has_bar_ && !climate_down_) {
      JsonObjectConst r = data["entities"]["room"]["attributes"];
      const Day day = normalize_day(read_lists(r), now ? minute_of(now) : -1);
      for (const Segment &s : bar_segments(day.bands, w_)) {
        if (nseg_ >= MAX_SEGMENTS) break;
        seg_data_[nseg_++] = s;
      }
      // day.truncated_before / truncated_after are not drawn: a day with more than 16 bands is far from real, the part
      // left out stays the empty track (nothing hides it), and the window always holds the band of now.
    }
    place_zones();
    paint();
    minute_ = -2;   // the tick and the pill are set again on the next on_tick
  }

  void on_tick(uint32_t epoch) override {
    tick(millis());   // the plugin's 250 ms interval does this too; this is the net for a tile it could not attach
    const int minute = minute_of(epoch);
    if (minute == minute_) return;
    minute_ = minute;
    paint_clock(epoch, minute);
  }

  void on_theme() override {
    paint();
    paint_clock(tessera::epoch(), minute_of(tessera::epoch()));
  }

  // What a tap does depends on where the finger was: + (left) and - (right) move the target, the pill goes back to the
  // program while it says "Manual x", the bar (and the strip over it) opens the list of today's bands, anything else
  // does nothing.
  void on_tap() override {
    const tessera::Point at = tessera::tap_point();
    switch (zone_at(zones_, at.x, at.y)) {
      case Zone::PLUS: return bump(+1);
      case Zone::MINUS: return bump(-1);
      case Zone::PILL: return resume();
      case Zone::BAR:
        plugin_->set_card_edit(edit_);   // the card is made inside open_card and reads this
        return (void) tessera::open_card(plugin_->plugin_id(), "day", entity_, tile_);
      default: return;
    }
  }

  // The waits of this tile, called every 250 ms by the plugin: the send 600 ms after the last tap, the return to the real
  // value 5 s after the send, the end of the wait for "back to automatic".
  void tick(uint32_t now) {
    int value = 0;
    if (setpoint_.take(now, value)) send(value);
    if (setpoint_.tick(now)) paint();
    if (resume_.tick(now)) paint_clock(tessera::epoch(), minute_of(tessera::epoch()));
  }

 private:
  bool fits(const std::string &text) const { return ui::text_width(text, Font::BODY) <= w_; }

  // The zones that depend on the state: whether the keys act, whether the pill is a key, where the bar zone starts.
  void place_zones() {
    if (family_ != Family::FULL) {
      compact_zones(zones_, family_, can_set_);
      return;
    }
    zones_.keys_live = can_set_;
    zones_.bar_tappable = bar_visible_;
    zones_.pill_tappable = can_resume_;
    // Sideways the pill zone is its chip and a margin, so a touch beside it does not drop the override.
    pill_span(w_, ui::text_width(pill_text(), Font::BODY) + 2 * ui::px(10), ui::mm(2), zones_.pill_x0, zones_.pill_x1);
    lower_zones(h_, keys_bottom_, pill_y_, body_h_, now_bottom_, tick_h_, ui::mm(7), zones_.pill_tappable, zones_.pill_top,
                zones_.pill_bottom, zones_.bar_top);
  }

  // + or -: the shown value moves one step at once; the command follows 600 ms after the last tap, from the plugin's 250 ms
  // interval, or (a tile it could not attach, past 16) from this tile's own once-a-second on_tick: later, never sooner.
  void bump(int direction) {
    if (!can_set_ || resume_.waiting(millis())) return;
    const uint32_t now = millis();
    if (!setpoint_.on_tap(direction, now)) return;   // no target, or already at the limit: nothing to send
    paint();
  }

  // set_manual: the service has no entity_id, the climate goes in the fields; `mode` is required, so it is the mode the
  // thermostat is in (or the band's), never a changed one. A command that cannot go (the room cannot be commanded now,
  // the firmware refuses it) shows the real value again.
  void send(int centi) {
    if (!can_set_ || entity_[0] == 0 || centi == NO_VALUE) {
      setpoint_.cancel();
      paint();
      return;
    }
    JsonDocument fields;
    fields["climate_entity_id"] = entity_;
    fields["mode"] = mode_name(cmd_mode_);
    fields["temperature"] = to_degrees(centi);   // degrees, as a number
    if (!tessera::action("cronotermostato.set_manual", "", fields)) {
      setpoint_.cancel();
      paint();
    }
  }

  // "Manual x": back to the program, once; a value waiting to be sent is dropped.
  void resume() {
    if (!can_resume_ || !resume_.press(millis())) return;
    setpoint_.cancel();
    JsonDocument fields;
    fields["climate_entity_id"] = entity_;
    if (!tessera::action("cronotermostato.resume_auto", "", fields)) resume_.on_state(false);
    paint();
    paint_clock(tessera::epoch(), minute_of(tessera::epoch()));
  }

  // The pill: the program ("Auto" and its name), a manual override and when it ends, or why there is none.
  std::string pill_text() const {
    switch (kind_) {
      case Pill::UNAVAILABLE: return plugin_->text("unavailable");
      case Pill::CHOOSE_ROOM: return plugin_->text("no_room");
      case Pill::PAUSED: return plugin_->text("paused");
      case Pill::NO_PROGRAM: return plugin_->text("no_program");
      case Pill::PROBLEM: return std::string(plugin_->text("problem")) + (can_resume_ ? std::string(" ") + cross_ : std::string());
      case Pill::MANUAL: {
        // "Manual until 18:00 x", else "Until 18:00 x", else "Manual x": the cross says the pill is a key.
        const std::string cross = can_resume_ ? std::string(" ") + cross_ : std::string();
        if (show_until_) {
          const std::string time = tessera::clock_text(until_);
          for (const char *key : {"manual_until", "until"}) {
            const std::string text = tessera::fill(plugin_->text(key), "time", time) + cross;
            if (fits(text)) return text;
          }
        }
        return std::string(plugin_->text("manual")) + cross;
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
    // Without keys (a small tile) the tile has one number: the target, else the temperature now.
    else if (alive && !has_keys_ && std::isfinite(current_)) target = format_temp(current_, step_, decimal_, true);
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
    if (!now_line_) now.clear();
    ui::set_text(now_, now);
    ui::set_color(now_, theme::MUTED);
    show(now_, now_line_);
    const int nw = now.empty() ? 0 : ui::text_width(now, Font::BODY);
    const bool dot = alive && hvac_ != 0;
    const int dot_gap = dot ? dot_gap_ : 0;
    const int body_h = ui::line_height(Font::BODY);
    lv_obj_set_pos(set_, mid_x_, set_y_);
    lv_obj_set_width(set_, mid_w_);
    if (now_line_) {
      // The dot stands before "Now 19.8°", the two centred together under the target.
      const int total = nw + (dot ? dot_d_ + dot_gap : 0);
      const int x0 = mid_x_ + std::max(0, (mid_w_ - total) / 2);
      lv_obj_set_width(now_, std::max(1, nw + ui::px(2)));
      lv_obj_set_pos(now_, x0 + (dot ? dot_d_ + dot_gap : 0), now_y_);
      if (dot) lv_obj_set_pos(dot_, x0, now_y_ + (body_h - dot_d_) / 2);
    } else if (dot) {
      // No "now" line: the dot stands beside the target, the two centred together.
      const int tw = std::min(ui::text_width(target, face), mid_w_);
      const int group = tw + dot_gap + dot_d_;
      const int x0 = mid_x_ + std::max(0, (mid_w_ - group) / 2);
      lv_obj_set_width(set_, tw + ui::px(2));   // the label ends where the text does: the dot is not under it
      lv_obj_set_pos(set_, x0, set_y_);
      lv_obj_set_pos(dot_, x0 + tw + dot_gap, set_y_ + (ui::line_height(face) - dot_d_) / 2);
    }
    show(dot_, dot);
    if (dot) ui::set_fill(dot_, hvac_ == 1 ? Tone::BUSY : Tone::ACCENT, theme::TRACK);
    ui::set_color(name_, theme::MUTED);
    for (int i = 0; i < 2; ++i) {
      ui::set_fill(key_[i], Tone::NORMAL, theme::TRACK);
      ui::set_color(key_label_[i], can_set_ ? theme::INK : theme::MUTED);
    }

    // The bar: a band is a block inside the track; with no bar to show the track stays empty (switched off).
    const bool bar = bar_visible_ && alive;
    const std::vector<Span> spans = separate(seg_data_, (size_t) std::min(nseg_, MAX_SEGMENTS), std::max(2, ui::px(2)));
    for (int i = 0; i < MAX_SEGMENTS; ++i) {
      if (!bar || i >= nseg_) {
        show(seg_[i], false);
        continue;
      }
      const Segment &s = seg_data_[i];
      show(seg_[i], true);
      lv_obj_set_pos(seg_[i], spans[i].x0, 0);
      lv_obj_set_size(seg_[i], std::max(1, spans[i].x1 - spans[i].x0), bar_h_);
      const lv_color_t fill = paint_band(seg_[i], s.mode, s.tint);
      spans_x0_[i] = spans[i].x0;
      if (labels_) paint_label(i, s, std::max(1, spans[i].x1 - spans[i].x0), fill);
    }
    ui::set_fill(track_, Tone::NORMAL, theme::TRACK);
    ui::set_fill(tick_, Tone::NORMAL, theme::INK);
    lv_obj_set_style_border_color(tick_, ui::color(theme::CARD), 0);
  }

  // The value on a segment, by the width it has (label_level: nothing, a sigla, the value): the temperature, or the preset
  // when the band has none. Whole if it fits, else cut with dots (two letters at least), else the sigla, else nothing.
  void paint_label(int i, const Segment &s, int width, lv_color_t fill) {
    lv_obj_t *l = seg_label_[i];
    if (!l) return;
    const bool value = s.mode != Mode::OFF && (s.has_temp || !s.preset.empty());
    const LabelLevel level = value ? label_level(width, ui::px(1)) : LabelLevel::NONE;
    const int room = width - 2 * ui::px(1);
    std::string text;
    if (level == LabelLevel::VALUE) {
      const std::string whole = segment_value(s.has_temp, s.temp, s.preset, step_, decimal_);
      const std::string sigla = segment_sigla(s.has_temp, s.temp, s.preset, step_, decimal_);
      if (ui::text_width(whole, Font::BODY) <= room) {
        text = whole;
      } else {
        const std::string cut = fit_cut(whole, room, Font::BODY);
        text = glyphs(cut) >= 3 ? cut : "";
        if (text.empty() && ui::text_width(sigla, Font::BODY) <= room) text = sigla;
      }
    } else if (level == LabelLevel::SHORT) {
      const std::string sigla = segment_sigla(s.has_temp, s.temp, s.preset, step_, decimal_);
      if (ui::text_width(sigla, Font::BODY) <= room) text = sigla;
    }
    label_w_[i] = text.empty() ? 0 : ui::text_width(text, Font::BODY);
    label_seg_[i] = width;
    if (text.empty()) {
      show(l, false);
      return;
    }
    lv_obj_set_width(l, label_w_[i] + ui::px(2));
    lv_obj_set_y(l, std::max(0, (bar_h_ - ui::line_height(Font::BODY)) / 2));
    ui::set_text(l, text);
    ui::set_color(l, text_on(fill));
    place_label(i, spans_x0_[i]);
  }

  // The label in the middle of its segment, or, when the "now" tick would stand on it, in the larger free side of the
  // segment, or none when neither side holds it (the bar and the big target say the same).
  void place_label(int i, int seg_x0) {
    lv_obj_t *l = seg_label_[i];
    if (!l || label_w_[i] <= 0) return;
    const int w = label_w_[i] + ui::px(2), seg_w = label_seg_[i];
    int x = (seg_w - w) / 2;
    bool fits = true;
    if (tick_on_) {
      const int margin = std::max(1, ui::px(1));
      const int a = seg_x0 + x - margin, b = seg_x0 + x + w + margin;   // the label, in the track's pixels
      if (a < tick_x_ + tick_full_ && tick_x_ < b) {
        const int left = tick_x_ - margin - seg_x0, right = seg_x0 + seg_w - (tick_x_ + tick_full_ + margin);
        if (left >= w && left >= right) x = (left - w) / 2;
        else if (right >= w) x = seg_w - right + (right - w) / 2;
        else fits = false;
      }
    }
    show(l, fits);
    if (fits) lv_obj_set_x(l, x);
  }

  // What follows the clock: the tick on the bar, the pill and the "then" line.
  void paint_clock(uint32_t epoch, int minute) {
    // No tick without a band to stand on (empty lists), nor when the bar is off.
    const bool bar = bar_visible_ && has_bar_ && !climate_down_ && nseg_ > 0;
    show(tick_, bar && epoch && minute >= 0);
    tick_on_ = bar && epoch && minute >= 0;
    if (tick_on_) {
      const int x = now_marker(seg_data_, (size_t) nseg_, minute, w_);
      const int full = tick_w_ + 2 * tick_edge_;
      tick_full_ = full;
      tick_x_ = std::min(std::max(0, x - full / 2), w_ - full);
      lv_obj_set_x(tick_, tick_x_);
    }
    if (labels_)
      for (int i = 0; i < nseg_ && i < MAX_SEGMENTS; ++i) place_label(i, spans_x0_[i]);
    if (has_pill_) paint_pill();
    place_zones();
    if (show_then_) {
      ui::set_text(then_, then_text());
      ui::set_color(then_, theme::MUTED);
    }
  }

  void paint_pill() {
    const std::string pill = pill_text();
    ui::set_text(pill_, pill);
    // INK for a manual override or a problem (it asks to be seen), MUTED for the rest (and while "back to automatic" is on
    // its way); never amber or blue as text.
    const bool waiting = resume_.waiting(millis());
    ui::set_color(pill_, (kind_ == Pill::MANUAL && !waiting) || kind_ == Pill::PROBLEM ? theme::INK : theme::MUTED);
    const int tw = ui::text_width(pill, Font::BODY);
    const int cw = std::min(w_, tw + 2 * ui::px(10));
    lv_obj_set_size(pill_bg_, cw, body_h_ + 2 * ui::px(2));
    lv_obj_set_pos(pill_bg_, (w_ - cw) / 2, pill_y_ - ui::px(2));
    ui::set_fill(pill_bg_, Tone::NORMAL, can_resume_ ? theme::TRACK : theme::CARD);
    lv_obj_set_width(pill_, std::min(cw, tw + ui::px(2)));
    lv_obj_center(pill_);
  }

  Cronotermostato *plugin_;
  char entity_[96]{};
  int w_ = 0, h_ = 0, tile_ = -1, now_bottom_ = 0, body_h_ = 0;
  TapGeometry zones_;
  ResumeGate resume_;
  bool can_set_ = false, can_resume_ = false, edit_ = true;
  Mode cmd_mode_ = Mode::HEAT;
  const char *cross_ = "x";
  int bar_y_ = 0, bar_h_ = 0, tick_h_ = 0, tick_w_ = 0, tick_edge_ = 1, pill_y_ = 0, then_y_ = 0, set_y_ = 0, now_y_ = 0;
  int key_y_ = 0, key_h_ = 0, key_w_ = 0, keys_bottom_ = 0, mid_x_ = 0, mid_w_ = 0, dot_d_ = 0, dot_gap_ = 0;
  Family family_ = Family::FULL;
  bool has_keys_ = true, has_bar_ = true, has_pill_ = true, now_line_ = true;
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
  lv_obj_t *name_{}, *pill_{}, *pill_bg_{}, *set_{}, *now_{}, *dot_{}, *then_{}, *track_{}, *tick_{};
  lv_obj_t *key_[2]{}, *key_label_[2]{}, *seg_[MAX_SEGMENTS]{}, *seg_label_[MAX_SEGMENTS]{};
  int label_w_[MAX_SEGMENTS]{}, label_seg_[MAX_SEGMENTS]{}, spans_x0_[MAX_SEGMENTS]{}, tick_x_ = 0, tick_full_ = 0;
  bool tick_on_ = false;
  bool labels_ = false;   // the bar is tall enough for the value of each band, and the tile has more than one cell
};

// ---------------------------------------------------------------------------------------------------------------
// The card. Three views in one: READ (one row per band of today, "08:00-12:00  Heating  21.5°", the band of now marked,
// and the key "Edit" when the panel is allowed), LIST (one row per user band, to touch) and DETAIL (the band: start, end,
// mode, temperature, Delete / Save / Back). The geometry and the zones are cronotermostato_pad.h (tested on a computer);
// what a key does to the bands is EditModel / EditSession in cronotermostato_edit.h. Here they become blocks, labels and the
// action `cronotermostato.set_day`.

// The lists of the room sensor as the edit model wants them: every list whole (up to one more than the model accepts, so
// that a day too long is seen as damaged and never cut), the presets and the flags beside them.
static EditInput read_edit_input(JsonObjectConst r, const std::string &program, const EditInfo &info) {
  EditInput in;
  const size_t cap = MAX_LIST_ITEMS + 1;
  auto numbers = [&](const char *key, std::vector<double> &out, double fallback) {
    JsonArrayConst a = r[key];
    out.reserve(std::min(a.size(), cap));
    for (size_t i = 0; i < a.size() && i < cap; ++i) out.push_back(number(a[i], fallback));
  };
  numbers("today_start", in.lists.start, NAN);
  numbers("today_end", in.lists.end, NAN);
  numbers("today_mode", in.lists.mode, 0);
  numbers("today_temp", in.lists.temp, NO_TEMP);
  numbers("today_flags", in.flags, NAN);
  JsonArrayConst p = r["today_preset"];
  in.preset.reserve(std::min(p.size(), cap));
  for (size_t i = 0; i < p.size() && i < cap; ++i) in.preset.emplace_back(p[i] | "");
  in.info = info;
  in.info.seq = (int64_t) clamp_to(number(r["edit_seq"], 0), 0, 1e15, 0);
  in.info.n = r["edit_n"].isNull() ? -1 : (int) clamp_to(number(r["edit_n"], -1), -1, 1e6, -1);
  in.program_name = program;
  return in;
}

class DayCard : public tessera::Card {
 public:
  explicit DayCard(Cronotermostato *plugin) : plugin_(plugin), edit_option_(plugin->card_edit()) {}

  void open(const tessera::CardContext &c) override {
    w_ = c.width;
    h_ = c.height;
    entity_[0] = 0;
    if (c.entity && std::strlen(c.entity) < sizeof entity_) std::snprintf(entity_, sizeof entity_, "%s", c.entity);
    decimal_ = decimal_of(plugin_);
    const int pad = ui::px(6), gap = ui::px(8);
    const int body_h = ui::line_height(Font::BODY);
    line_ = body_h + ui::px(8);
    chip_w_ = ui::px(5);
    dash_ = ui::covers(EN_DASH, Font::BODY) ? EN_DASH : "-";
    minus_ = ui::covers(MINUS, Font::BODY) ? MINUS : "-";
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
    pad_ = pad;
    gap_ = gap;

    // The pad: the keys of the three views, from the real pixels (7 mm where the glass has them).
    PadInput pi;
    pi.w = w_;
    pi.h = h_;
    pi.key_h = std::max(ui::mm(7), body_h + ui::px(6));
    pi.key_min_w = ui::mm(7);
    pi.gap = std::max(2, ui::px(3));
    pi.line_h = body_h;
    pi.foot_w = ui::mm(9);
    pi.time_w = ui::text_width("88:88", Font::BODY);
    pi.cap_w = std::max(ui::text_width(std::string(plugin_->text("edit_start")) + " 88:88", Font::BODY),
                        ui::text_width(std::string(plugin_->text("edit_end")) + " 88:88", Font::BODY));
    int mode_key_w = 0;
    for (Mode m : {Mode::HEAT, Mode::COOL, Mode::OFF}) mode_key_w = std::max(mode_key_w, ui::text_width(mode_word(plugin_, m, true), Font::BODY));
    pi.mode_w = mode_key_w + 2 * ui::px(10);
    pi.open_w = ui::text_width(plugin_->text("edit_open"), Font::BODY) + 2 * ui::px(12);
    pi.key_max_w = ui::mm(26);
    pi.key_max_h = ui::mm(12);
    pi.wide_gap = ui::mm(8);   // Cancel and Confirm stand a thumb apart
    L_ = make_pad(pi);

    // READ: a line over the rows says the program (and a holiday), with the key "Edit" at its right when it can be offered;
    // the rows follow, as many as the height holds.
    header_ = edit_option_ || c.height >= 5 * line_;
    top_ = edit_option_ ? L_.read_top : (header_ ? line_ : 0);
    rows_ = std::max(1, std::min(MAX_SEGMENTS, (c.height - top_) / line_));
    hdr_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    const Rect hr = edit_option_ ? L_.read_head : Rect{0, 0, w_, line_};
    hdr_w_ = std::max(1, (edit_option_ ? hr.w : w_) - 2 * pad);
    lv_obj_set_width(hdr_, hdr_w_);
    lv_obj_set_pos(hdr_, pad, hr.y + (hr.h - body_h) / 2);
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
    parent_ = c.parent;
    if (edit_option_) build_edit(c.parent);
    paint();
  }

  void on_state(JsonObjectConst data) override {
    const TileInput in = read_input(data);
    const TileView view = tile_view(in);
    JsonObjectConst r = data["entities"]["room"]["attributes"];
    JsonObjectConst a = data["attributes"];
    step_ = number(a["target_temp_step"], 0.5);
    range_ = make_range(number(a["min_temp"], 7), number(a["max_temp"], 35), step_);
    program_ = roomy::text(view.program);
    const EditInfo info = parse_edit_info(std::string(r["edit_info"] | ""));
    holiday_ = std::string(r["edit_info"] | "").compare(0, 8, "holiday;") == 0;   // today_kind, first field of edit_info
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

    // The panel: offered when the option is on, the room can be commanded and edit_info is readable. What the sensor says is
    // kept (while it is allowed) so that "Edit" opens on it; while the panel is open the session takes the news.
    panel_ok_ = edit_option_ && panel_allowed(true, view, info);
    if (panel_ok_) last_ = read_edit_input(r, roomy::plain(program_), info);
    else last_ = EditInput();
    if (session_.opened()) {
      if (!panel_ok_) {
        leave_edit();
      } else {
        session_.model().set_range(range_);
        react(session_.on_state(last_, millis()));
        settle_list();
      }
    }
    paint();
  }

  // The band of now moves with the clock; the rows are the same. The waits of the panel run here too: the end of the wait
  // for Home Assistant (5 s), the words that go away.
  void on_tick(uint32_t epoch) override {
    const uint32_t now = millis();
    bool redo = false;
    if (session_.opened()) {
      if (session_.tick(now)) {
        settle_list();
        redo = true;
      }
      if (transient_.any() && expired(now, transient_at_, MESSAGE_MS)) {
        transient_ = TextKeys();
        redo = true;
      }
      if (session_.state() == SaveState::SAVED && expired(now, saved_at_, MESSAGE_MS)) {
        session_.acknowledge();
        redo = true;
      }
    }
    const int minute = minute_of(epoch);
    if (minute != minute_) {
      minute_ = minute;
      for (int i = 0; i < n_; ++i)
        rows_data_[i].current = minute >= 0 && rows_data_[i].start <= minute && minute < rows_data_[i].end;
      redo = true;
    }
    if (redo) paint();
  }

  void on_theme() override { paint(); }

  // Back from the detail goes to the list (what was changed and not saved is dropped); from the list the card closes.
  bool on_back() override {
    if (view_ == EView::CONFIRM) {   // back from the question is "no": the band stays as it was
      cancel_delete();
      return true;
    }
    if (view_ == EView::DETAIL) {
      go_back_to_list();
      return true;
    }
    return false;
  }

  // One tap per card: tessera::tap_point() says where, pad_zone_at which key. A screen that cannot tell (-1,-1) does nothing.
  void on_tap() override {
    const tessera::Point at = tessera::tap_point();
    const EZone z = pad_zone_at(L_, view_, view_ == EView::LIST ? session_.model().page_rows() : 0, at.x, at.y);
    const uint32_t now = millis();
    if (view_ == EView::READ) {
      if (z.kind == EZ::OPEN && panel_ok_) enter_edit();
      return;
    }
    if (!session_.opened()) return;
    if (z.kind != EZ::SAVE) session_.acknowledge();   // the result of a save and a notice go with the next tap
    transient_ = TextKeys();
    EditModel &m = session_.model();
    if (view_ == EView::CONFIRM) {
      switch (confirm_tap(z.kind, now, confirm_at_)) {
        case ConfirmTap::CANCEL: cancel_delete(); break;
        case ConfirmTap::DELETE: delete_band(now); break;
        default: break;   // a tap beside the keys, or on Confirm too soon, does nothing
      }
      paint();
      return;
    }
    if (view_ == EView::LIST) {
      switch (z.kind) {
        case EZ::ROW: {
          const int idx = m.page_first() + z.index;
          const EditBand *b = m.band((size_t) idx);
          if (!b) break;
          if (b->locked) {
            say(Msg::LOCKED);
            break;
          }
          if (m.select(idx)) view_ = EView::DETAIL;
          break;
        }
        case EZ::PREV: m.prev_page(); break;
        case EZ::NEXT: m.next_page(); break;
        case EZ::ADD: {
          const EditResult res = m.add();
          if (res.msg != Msg::NONE) say(res.msg);
          if (res.changed) view_ = EView::DETAIL;
          break;
        }
        default: break;
      }
    } else {
      KeyStep ks;
      EditResult res;
      bool acted = true;
      if (time_key(z.kind, ks)) {
        res = ks.start ? m.move_start(ks.delta) : m.move_end(ks.delta);
      } else {
        switch (z.kind) {
          case EZ::MODE: res = m.cycle_mode(); break;
          case EZ::TEMP_M: res = m.step_temp(-1); break;
          case EZ::TEMP_P: res = m.step_temp(+1); break;
          case EZ::DELETE: ask_delete(now); acted = false; break;
          case EZ::SAVE: save(now); acted = false; break;
          case EZ::BACK: go_back_to_list(); acted = false; break;
          default: acted = false; break;
        }
      }
      if (acted && res.msg != Msg::NONE) say(res.msg);
    }
    paint();
  }

 private:
  static constexpr uint32_t MESSAGE_MS = 4000;   // how long a word from a key stays in the status line

  struct Line {
    lv_obj_t *hl{}, *chip{}, *time{}, *mode{}, *temp{}, *now{};
  };
  // A key: a block and its label (a child of the block; a label beside the block is "over another part" for the bench).
  struct Key {
    lv_obj_t *bg{}, *lb{};
  };
  // A row of the list: a key with the time, the mode, the word "locked" and the temperature in it.
  struct ListRow {
    lv_obj_t *bg{}, *chip{}, *time{}, *mode{}, *lock{}, *temp{};
  };
  enum Detail { K_S0, K_S1, K_S2, K_S3, K_E0, K_E1, K_E2, K_E3, K_MODE, K_TM, K_TP, K_DEL, K_SAVE, K_BACK, K_COUNT };

  // --- building ------------------------------------------------------------------------------------------------
  Key make_key(lv_obj_t *parent, const Rect &r) {
    Key k;
    k.bg = ui::block(parent, theme::RAISED);
    lv_obj_set_style_pad_all(k.bg, 0, 0);
    lv_obj_set_size(k.bg, r.w, r.h);
    lv_obj_set_pos(k.bg, r.x, r.y);
    k.lb = ui::label(k.bg, Font::BODY, theme::INK);
    lv_obj_set_width(k.lb, r.w);
    lv_obj_set_style_text_align(k.lb, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(k.lb, 0, (r.h - ui::line_height(Font::BODY)) / 2);
    return k;
  }
  lv_obj_t *make_label(lv_obj_t *parent, const Rect &r, lv_text_align_t align) {
    lv_obj_t *l = ui::label(parent, Font::BODY, theme::INK);
    lv_obj_set_width(l, r.w);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_pos(l, r.x, r.y + (r.h - ui::line_height(Font::BODY)) / 2);
    return l;
  }
  const Rect &detail_rect(int k) const {
    switch (k) {
      case K_S0: case K_S1: case K_S2: case K_S3: return L_.s_key[k - K_S0];
      case K_E0: case K_E1: case K_E2: case K_E3: return L_.e_key[k - K_E0];
      case K_MODE: return L_.mode_key;
      case K_TM: return L_.temp_m;
      case K_TP: return L_.temp_p;
      case K_DEL: return L_.del;
      case K_SAVE: return L_.save;
      default: return L_.back;
    }
  }

  void build_edit(lv_obj_t *parent) {
    const int body_h = ui::line_height(Font::BODY);
    const int pad = ui::px(8), gap = ui::px(8);
    lchip_w_ = ui::px(5);
    lpad_ = pad;
    lgap_ = gap;
    // LIST: header, rows, footer.
    lhdr_ = make_label(parent, L_.list_head.empty() ? Rect{0, 0, w_, body_h} : L_.list_head, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_x(lhdr_, ui::px(6));
    lv_obj_set_width(lhdr_, std::max(1, w_ - 2 * ui::px(6)));
    lhdr_w_ = std::max(1, w_ - 2 * ui::px(6));
    lempty_ = make_label(parent, Rect{0, L_.row[0].y, w_, L_.row_h}, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_x(lempty_, ui::px(6));
    lv_obj_set_width(lempty_, std::max(1, w_ - 2 * ui::px(6)));
    // The widest of the three words decides the column; a narrow row takes the short words.
    int mode_long = 0, mode_short = 0;
    for (Mode m : {Mode::HEAT, Mode::COOL, Mode::OFF}) {
      mode_long = std::max(mode_long, ui::text_width(mode_word(plugin_, m, false), Font::BODY));
      mode_short = std::max(mode_short, ui::text_width(mode_word(plugin_, m, true), Font::BODY));
    }
    const int fixed = 2 * pad + lchip_w_ + gap + time_w_ + gap + temp_w_ + gap;
    lrow_short_ = fixed + mode_long > w_;
    lrow_mode_w_ = lrow_short_ ? mode_short : mode_long;
    lock_w_ = ui::text_width(plugin_->text("locked_band"), Font::BODY);
    lrow_lock_ = fixed + lrow_mode_w_ + gap + lock_w_ <= w_;
    for (int i = 0; i < L_.rows; ++i) {
      const Rect &r = L_.row[i];
      ListRow &lr = lrow_[i];
      lr.bg = ui::block(parent, theme::RAISED);
      lv_obj_set_style_pad_all(lr.bg, 0, 0);
      lv_obj_set_size(lr.bg, r.w, r.h);
      lv_obj_set_pos(lr.bg, r.x, r.y);
      const int ty = (r.h - body_h) / 2;
      lr.chip = ui::block(lr.bg, theme::SUBTLE);   // the colour of the band on the bar of the tile
      lv_obj_set_size(lr.chip, lchip_w_, std::max(2, r.h - 2 * ui::px(6)));
      lv_obj_set_pos(lr.chip, pad, ui::px(6));
      lr.time = ui::label(lr.bg, Font::BODY, theme::INK);
      lv_obj_set_width(lr.time, time_w_ + ui::px(2));
      lv_obj_set_pos(lr.time, pad + lchip_w_ + gap, ty);
      lr.mode = ui::label(lr.bg, Font::BODY, theme::INK);
      lv_obj_set_width(lr.mode, lrow_mode_w_ + ui::px(2));
      lv_obj_set_pos(lr.mode, pad + lchip_w_ + gap + time_w_ + gap, ty);
      lr.temp = ui::label(lr.bg, Font::BODY, theme::INK);
      lv_obj_set_width(lr.temp, temp_w_ + ui::px(2));
      lv_obj_set_style_text_align(lr.temp, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_pos(lr.temp, r.w - pad - temp_w_ - ui::px(2), ty);
      lr.lock = ui::label(lr.bg, Font::BODY, theme::MUTED);
      lv_obj_set_width(lr.lock, lock_w_ + ui::px(2));
      lv_obj_set_style_text_align(lr.lock, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_pos(lr.lock, r.w - pad - temp_w_ - gap - lock_w_ - ui::px(2), ty);
    }
    foot_[0] = make_key(parent, L_.prev);
    foot_[1] = make_key(parent, L_.next);
    foot_[2] = make_key(parent, L_.add);
    ui::set_text(foot_[0].lb, "<");
    ui::set_text(foot_[1].lb, ">");
    ui::set_text(foot_[2].lb, "+");
    info_ = make_label(parent, L_.info, LV_TEXT_ALIGN_CENTER);
    // DETAIL: a status line, the captions, the keys and the value of the temperature.
    dhdr_ = make_label(parent, L_.detail_header ? L_.detail_head : Rect{0, 0, w_, body_h}, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_x(dhdr_, ui::px(6));
    lv_obj_set_width(dhdr_, std::max(1, w_ - 2 * ui::px(6)));
    cap_[0] = make_label(parent, L_.s_cap, LV_TEXT_ALIGN_LEFT);
    cap_[1] = make_label(parent, L_.e_cap, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_x(cap_[0], ui::px(2));
    lv_obj_set_x(cap_[1], ui::px(2));
    for (int k = 0; k < K_COUNT; ++k) det_[k] = make_key(parent, detail_rect(k));
    tval_ = make_label(parent, L_.temp_val, LV_TEXT_ALIGN_CENTER);
    const char *steps[4] = {"-1h", "-15", "+15", "+1h"};
    for (int i = 0; i < 4; ++i) {
      std::string t = steps[i];
      if (t[0] == '-') t = std::string(minus_) + t.substr(1);
      ui::set_text(det_[K_S0 + i].lb, t);
      ui::set_text(det_[K_E0 + i].lb, t);
    }
    ui::set_text(det_[K_TM].lb, minus_);
    ui::set_text(det_[K_TP].lb, "+");
    ui::set_text(det_[K_DEL].lb, plugin_->text("edit_delete"));
    ui::set_text(det_[K_BACK].lb, plugin_->text("edit_back"));
    open_ = make_key(parent, L_.open);
    ui::set_text(open_.lb, plugin_->text("edit_open"));
    // CONFIRM: up to three lines and the two keys.
    for (int i = 0; i < 3; ++i) {
      cline_[i] = make_label(parent, L_.conf_line[i].empty() ? Rect{0, 0, w_, body_h} : L_.conf_line[i], LV_TEXT_ALIGN_CENTER);
      lv_obj_set_x(cline_[i], ui::px(6));
      lv_obj_set_width(cline_[i], std::max(1, w_ - 2 * ui::px(6)));
    }
    cline_w_ = std::max(1, w_ - 2 * ui::px(6));
    conf_[0] = make_key(parent, L_.conf_no);
    conf_[1] = make_key(parent, L_.conf_yes);
    ui::set_text(conf_[0].lb, plugin_->text(confirm_words().no));
    ui::set_text(conf_[1].lb, plugin_->text(confirm_words().yes));
  }

  // --- the views -----------------------------------------------------------------------------------------------
  void enter_edit() {
    if (!panel_ok_) return;
    session_.open(last_, range_, L_.rows, entity_);
    transient_ = TextKeys();
    view_ = EView::LIST;
    paint();
  }
  void leave_edit() {
    session_.close();
    session_ = EditSession();   // frees what the copy kept
    transient_ = TextKeys();
    view_ = EView::READ;
  }
  // The copy goes back to what Home Assistant has, unless a save is under way (then it is the copy that was sent).
  void go_back_to_list() {
    if (session_.state() != SaveState::SAVING) session_.model().load(last_);
    session_.model().deselect();
    view_ = EView::LIST;
    paint();
  }
  // The list has no Save key: a copy left changed by a Delete that failed (refused, not sent, no answer) goes back to what
  // Home Assistant has, so the list never shows a band as gone that is still there.
  void settle_list() {
    if (view_ == EView::LIST) session_.discard_unsaved(last_);
  }
  void say(Msg m) {
    transient_ = msg_text(m);
    transient_at_ = millis();
  }
  void say(const TextKeys &k) {
    transient_ = k;
    transient_at_ = millis();
  }

  // The events of on_state: after the news the copy may have changed under the person's finger.
  void react(EditEvent ev) {
    switch (ev) {
      case EditEvent::SAVED:
        view_ = EView::LIST;
        session_.model().deselect();
        saved_at_ = millis();
        break;
      case EditEvent::CHANGED_ELSEWHERE:
      case EditEvent::DAY_CHANGED:
        view_ = EView::LIST;
        break;
      case EditEvent::SAVE_FAILED:
        if (session_.model().selected() < 0) view_ = EView::LIST;
        break;
      default: break;
    }
    // The question is about the band as it was shown: any news closes it, without sending.
    if (view_ == EView::CONFIRM && !confirm_survives(ev)) {
      session_.model().deselect();
      view_ = EView::LIST;
    }
    if ((view_ == EView::DETAIL || view_ == EView::CONFIRM) && session_.model().selected() < 0) view_ = EView::LIST;
  }

  // Save: once, to Home Assistant. The keys explain what stops it.
  void save(uint32_t now) {
    if (session_.state() == SaveState::IDLE) {
      const SaveBlock block = session_.save_block();
      if (block != SaveBlock::NONE || !session_.can_save()) {
        say(block_text(block));
        return;
      }
    }
    send_request(session_.press_save(now));
  }
  void send_request(const SaveRequest &rq) {
    if (!rq.send) return;
    if (!entity_[0]) {   // no room id (too long to keep): nothing is sent with an empty climate_entity_id
      session_.send_failed();
      return;
    }
    JsonDocument fields;
    fields["climate_entity_id"] = entity_;
    fields["day_kind"] = rq.day_kind;
    fields["bands"] = rq.bands;
    if (!rq.base_revision.empty()) fields["base_revision"] = rq.base_revision;
    if (!tessera::action("cronotermostato.set_day", "", fields)) session_.send_failed();
  }
  // Delete opens the question; nothing is sent. What stops a delete is said now, not after the question.
  void ask_delete(uint32_t now) {
    const DeleteGate gate = delete_gate(session_.model());
    if (!gate.ok) {
      if (gate.why.any()) say(gate.why);
      return;
    }
    confirm_at_ = now;
    view_ = EView::CONFIRM;
  }
  void cancel_delete() {
    view_ = EView::DETAIL;
    if (session_.model().selected() < 0) view_ = EView::LIST;
    paint();
  }
  // Confirm: delete and save, the list has no Save key. If the day cannot be saved the copy goes back and the key says why.
  void delete_band(uint32_t now) {
    EditModel &m = session_.model();
    view_ = EView::DETAIL;   // a refusal is said on the detail, as before the question existed
    const DeleteGate gate = delete_gate(m);
    if (!gate.ok) {
      if (gate.why.any()) say(gate.why);
      if (m.selected() < 0) view_ = EView::LIST;
      return;
    }
    const EditResult res = m.remove();
    if (res.msg != Msg::NONE) {
      say(res.msg);
      return;
    }
    if (!res.changed) {
      if (m.selected() < 0) view_ = EView::LIST;
      return;
    }
    const SaveBlock block = m.save_block();
    if (block != SaveBlock::NONE) {
      m.load(last_);
      say(block_text(block));
    } else {
      send_request(session_.press_save(now));
    }
    m.deselect();
    view_ = EView::LIST;
    settle_list();   // a Delete that was not even sent leaves nothing to save
  }

  // --- painting ------------------------------------------------------------------------------------------------
  // The text of a message in the room it has: the long form, else the short one, else the short one cut.
  std::string pick(const TextKeys &k, int width) const {
    const std::string full = plugin_->text(k.full);
    if (ui::text_width(full, Font::BODY) <= width) return full;
    return fit_cut(plugin_->text(k.shortly), width, Font::BODY);
  }
  TextKeys status() const { return status_of(session_.state(), session_.notice(), session_.error(), transient_); }
  // "Casa - 3 stanze", else the count alone, else the count's number.
  std::string title_for(int width) const {
    const std::string one = plugin_->text("room_one"), many = plugin_->text("room_many");
    std::string t = session_.title(one.c_str(), many.c_str());
    if (ui::text_width(t, Font::BODY) <= width) return t;
    const int rooms = session_.rooms();
    if (rooms >= 0) {
      t = std::to_string(rooms) + " " + (rooms == 1 ? one : many);
      if (ui::text_width(t, Font::BODY) <= width) return t;
      t = std::to_string(rooms);
      if (ui::text_width(t, Font::BODY) <= width) return t;
    }
    return "";
  }

  void paint() {
    paint_read();
    paint_list();
    paint_detail();
    paint_confirm();
    log_pad();
  }

  void paint_read() {
    const bool on = view_ == EView::READ;
    // The header: the program's name and "Holiday" when today is one; the name is cut before the key.
    std::string head = roomy::plain(program_);
    if (holiday_) head += (head.empty() ? "" : " \xC2\xB7 ") + std::string(plugin_->text("holiday"));
    if (ui::text_width(head, Font::BODY) > hdr_w_) head = roomy::plain(program_);
    ui::set_text(hdr_, fit_cut(head, hdr_w_, Font::BODY));
    ui::set_color(hdr_, theme::MUTED);
    show(hdr_, on && header_);
    const bool key = on && edit_option_ && panel_ok_;
    show(open_.bg, key);
    if (open_.bg) {
      ui::set_fill(open_.bg, Tone::NORMAL, theme::RAISED);
      ui::set_color(open_.lb, theme::INK);
    }
    ui::set_text(note_, on && note_key_ ? plugin_->text(note_key_) : "");
    ui::set_color(note_, theme::MUTED);
    // The window of rows: all of them, or the ones around the band of now.
    int cur = 0;
    for (int i = 0; i < n_; ++i)
      if (rows_data_[i].current) cur = i;
    // More bands than rows: the last row's place says how many are left out ("+3 more").
    const int vis = n_ > rows_ && rows_ > 1 ? rows_ - 1 : rows_;
    const int first = n_ > vis ? std::min(std::max(cur - vis / 2, 0), n_ - vis) : 0;
    const bool more = on && n_ > vis;
    show(more_, more);
    if (more) {
      lv_obj_set_y(more_, top_ + vis * line_ + (line_ - ui::line_height(Font::BODY)) / 2);
      ui::set_text(more_, tessera::fill(plugin_->text("more"), "n", std::to_string(n_ - vis)));
      ui::set_color(more_, theme::MUTED);
    }
    for (int i = 0; i < rows_; ++i) {
      Line &r = row_[i];
      const int idx = first + i;
      const bool shown = on && i < vis && idx < n_;
      for (lv_obj_t *part : {r.chip, r.time, r.mode, r.temp}) show(part, shown);
      if (!shown) {
        show(r.hl, false);
        show(r.now, false);
        continue;
      }
      const Row &d = rows_data_[idx];
      show(r.hl, d.current);
      show(r.now, d.current);
      paint_band(r.chip, d.mode, d.tint);
      ui::set_text(r.time, format_clock(d.start) + dash_ + format_clock(d.end));
      ui::set_text(r.mode, mode_word(plugin_, d.mode, short_));
      // The value: the temperature, the preset, or both ("21.5° away") when the row has room; the preset takes the room
      // the temperature would leave (and that of "Now" on the row of now). The same value is on the bar of the tile.
      const std::string temp = d.has_temp ? format_temp(d.temp, step_, decimal_, true) : "";
      const int right = w_ - pad_;
      const int x_val = pad_ + chip_w_ + gap_ + time_w_ + gap_ + mode_w_ + gap_;
      const int avail = std::max(temp_w_, right - x_val - (d.current && r.now ? now_w_ + gap_ : 0));
      const RowValue rv = row_value(temp, d.mode == Mode::OFF ? "" : d.preset, avail, [](const std::string &t) { return ui::text_width(t, Font::BODY); });
      const std::string value = rv.cut ? fit_cut(rv.text, avail, Font::BODY) : rv.text;
      const int vw = std::max(temp_w_, std::min(avail, ui::text_width(value, Font::BODY)));   // the box is what the text needs, so "Now" never overlaps it
      lv_obj_set_width(r.temp, vw + ui::px(2));
      lv_obj_set_x(r.temp, right - vw - ui::px(2));
      ui::set_text(r.temp, value);
      if (r.now) {
        lv_obj_set_x(r.now, right - vw - gap_ - now_w_ - ui::px(2));
        ui::set_text(r.now, plugin_->text("right_now"));
      }
      const theme::Role ink = d.current ? theme::INK : theme::INK_SOFT;
      ui::set_fill(r.hl, Tone::NORMAL, theme::INK);
      ui::set_color(r.time, ink);
      ui::set_color(r.mode, ink);
      ui::set_color(r.temp, ink);
      if (r.now) ui::set_color(r.now, theme::INK);
    }
  }

  static void key_state(const Key &k, bool enabled) {
    ui::set_fill(k.bg, Tone::NORMAL, theme::RAISED);
    ui::set_color(k.lb, enabled ? theme::INK : theme::MUTED);
  }

  void paint_list() {
    if (!edit_option_) return;
    const bool on = view_ == EView::LIST && session_.opened();
    const EditModel &m = session_.model();
    const TextKeys st = on ? status() : TextKeys();
    const bool header = L_.list_header;
    show(lhdr_, on && header);
    const int shown = on ? m.page_rows() : 0;
    const int first = m.page_first();
    show(lempty_, on && m.size() == 0);
    ui::set_text(lempty_, on && m.size() == 0 ? plugin_->text("edit_empty") : "");
    ui::set_color(lempty_, theme::MUTED);
    // The tints of the whole day, in its order (a page shows a part of it): the same as the bar of the tile.
    std::vector<int> tints;
    {
      TintBook book;
      for (size_t k = 0; k < m.size(); ++k) {
        const EditBand &e = *m.band(k);
        tints.push_back(book.tint(e.mode, e.mode != Mode::OFF && e.has_temp, to_degrees(e.centi), e.preset));
      }
    }
    for (int i = 0; i < L_.rows; ++i) {
      ListRow &lr = lrow_[i];
      const bool vis = i < shown;
      show(lr.bg, vis);
      if (!vis) continue;
      const EditBand &b = *m.band((size_t) (first + i));
      paint_band(lr.chip, b.mode, tints[(size_t) (first + i)]);
      ui::set_fill(lr.bg, Tone::NORMAL, theme::RAISED);
      const theme::Role ink = b.locked ? theme::MUTED : theme::INK;
      ui::set_text(lr.time, format_clock(b.start) + dash_ + format_clock(b.end));
      ui::set_color(lr.time, ink);
      const bool lock_word = b.locked && !lrow_lock_;   // no room for both: the word stands for the mode
      ui::set_text(lr.mode, lock_word ? plugin_->text("locked_band") : mode_word(plugin_, b.mode, lrow_short_));
      ui::set_color(lr.mode, ink);
      // The value as in the reading list: temperature and preset when there is room, the preset in the room the lock word leaves.
      const std::string temp = b.mode != Mode::OFF && b.has_temp ? format_temp(to_degrees(b.centi), step_, decimal_, true) : "";
      const int right = L_.row[i].w - lpad_;
      const int x_val = lpad_ + lchip_w_ + lgap_ + time_w_ + lgap_ + lrow_mode_w_ + lgap_;
      const bool lock_shown = b.locked && lrow_lock_;
      const int avail = std::max(temp_w_, right - x_val - (lock_shown ? lock_w_ + lgap_ : 0));
      const RowValue rv = row_value(temp, b.mode == Mode::OFF ? "" : b.preset, avail, [](const std::string &t) { return ui::text_width(t, Font::BODY); });
      const std::string value = rv.cut ? fit_cut(rv.text, avail, Font::BODY) : rv.text;
      const int vw = std::max(temp_w_, std::min(avail, ui::text_width(value, Font::BODY)));
      lv_obj_set_width(lr.temp, vw + ui::px(2));
      lv_obj_set_x(lr.temp, right - vw - ui::px(2));
      lv_obj_set_x(lr.lock, right - vw - lgap_ - lock_w_ - ui::px(2));
      ui::set_text(lr.temp, value);
      ui::set_color(lr.temp, ink);
      show(lr.lock, b.locked && lrow_lock_);
      ui::set_text(lr.lock, b.locked && lrow_lock_ ? plugin_->text("locked_band") : "");
      ui::set_color(lr.lock, theme::MUTED);
    }
    for (int i = 0; i < 3; ++i) show(foot_[i].bg, on);
    show(info_, on);
    if (!on) {
      show(lhdr_, false);
      return;
    }
    key_state(foot_[0], m.page() > 0);
    key_state(foot_[1], m.page() + 1 < m.page_count());
    key_state(foot_[2], true);
    // The words: the header takes the status, else the title; the page goes to the footer. Without a header (a short glass)
    // the footer's middle is the only line: the status, else the title and the page.
    const std::string page = m.page_count() > 1 ? page_numbers(m.page(), m.page_count()) : "";
    if (header) {
      ui::set_text(lhdr_, st.any() ? pick(st, lhdr_w_) : title_for(lhdr_w_));
      ui::set_color(lhdr_, st.any() ? theme::INK : theme::MUTED);
      ui::set_text(info_, page);
      ui::set_color(info_, theme::MUTED);
    } else {
      const int iw = std::max(1, L_.info.w);
      std::string t;
      if (st.any()) {
        t = pick(st, iw);
      } else {
        const std::string head = title_for(iw);
        const std::string both = head.empty() ? page : (page.empty() ? head : head + " " + page);
        t = ui::text_width(both, Font::BODY) <= iw ? both : (page.empty() ? head : page);
      }
      ui::set_text(info_, fit_cut(t, iw, Font::BODY));
      ui::set_color(info_, st.any() ? theme::INK : theme::MUTED);
    }
  }

  void paint_detail() {
    if (!edit_option_) return;
    const bool on = view_ == EView::DETAIL && session_.opened() && session_.model().selected() >= 0;
    show(dhdr_, on && L_.detail_header);
    show(cap_[0], on);
    show(cap_[1], on);
    show(tval_, on);
    for (int k = 0; k < K_COUNT; ++k) show(det_[k].bg, on);
    if (!on) return;
    const EditModel &m = session_.model();
    const EditBand &b = *m.band((size_t) m.selected());
    const bool live = !m.frozen() && !b.locked;
    const SaveState state = session_.state();
    const TextKeys st = status();
    // The captions: "Start 06:00", or the time alone when the caption has no room.
    const std::string t0 = format_clock(b.start), t1 = format_clock(b.end);
    ui::set_text(cap_[0], L_.captions ? std::string(plugin_->text("edit_start")) + " " + t0 : t0);
    ui::set_text(cap_[1], L_.captions ? std::string(plugin_->text("edit_end")) + " " + t1 : t1);
    ui::set_color(cap_[0], theme::INK);
    ui::set_color(cap_[1], theme::INK);
    for (int i = 0; i < 4; ++i) {
      key_state(det_[K_S0 + i], live);
      key_state(det_[K_E0 + i], live);
    }
    ui::set_text(det_[K_MODE].lb, mode_word(plugin_, b.mode, true));
    key_state(det_[K_MODE], live);
    std::string temp = "--";
    if (b.mode != Mode::OFF && b.has_temp) temp = format_temp(to_degrees(b.centi), step_, decimal_, true);
    else if (b.mode != Mode::OFF && !b.preset.empty()) temp = fit_cut(b.preset, std::max(1, L_.temp_val.w), Font::BODY);
    ui::set_text(tval_, temp);
    ui::set_color(tval_, theme::INK);
    key_state(det_[K_TM], live && b.mode != Mode::OFF);
    key_state(det_[K_TP], live && b.mode != Mode::OFF);
    key_state(det_[K_DEL], live);
    key_state(det_[K_BACK], true);
    // Save: the state of the save, else a word from a key (when there is no status line), else "Save". Grey when it cannot.
    const int kw = L_.save.w - ui::px(6);
    std::string label = plugin_->text(save_key(state));
    if (!L_.detail_header) {
      // No status line (a short glass): the key carries the word. A refusal says why in its short form ("Changed").
      if (state == SaveState::IDLE && st.any()) label = pick(st, kw);
      else if (state == SaveState::ERROR && st.any() && ui::text_width(plugin_->text(st.shortly), Font::BODY) <= kw) label = plugin_->text(st.shortly);
    }
    ui::set_text(det_[K_SAVE].lb, fit_cut(label, kw, Font::BODY));
    key_state(det_[K_SAVE], state != SaveState::IDLE || session_.can_save());
    if (L_.detail_header) {
      ui::set_text(dhdr_, st.any() ? pick(st, std::max(1, w_ - 2 * ui::px(6))) : title_for(std::max(1, w_ - 2 * ui::px(6))));
      ui::set_color(dhdr_, st.any() ? theme::INK : theme::MUTED);
    }
  }

  // The question before a Delete: the band as it was shown, the warning of a shared program, Cancel and Confirm.
  void paint_confirm() {
    if (!edit_option_) return;
    const bool on = view_ == EView::CONFIRM && session_.opened() && session_.model().selected() >= 0;
    for (int i = 0; i < 3; ++i) show(cline_[i], on && i < L_.conf_lines);
    for (int i = 0; i < 2; ++i) show(conf_[i].bg, on);
    if (!on) return;
    const EditBand &b = *session_.model().band((size_t) session_.model().selected());
    const bool shared = confirm_shared(session_.rooms());
    const ConfirmLines cl = confirm_lines(L_.conf_lines, shared);
    // The three lines stand top to bottom as question, band, warning; the ones that do not fit are left out.
    const bool want[3] = {cl.question, cl.band, cl.warning};
    // Fewer lines than the room holds: the block stays centred in it.
    const int count = (int) cl.question + (int) cl.band + (int) cl.warning;
    const int pitch = L_.conf_lines > 1 ? L_.conf_line[1].y - L_.conf_line[0].y : 0;
    const int y0 = L_.conf_line[0].y + (L_.conf_lines - count) * pitch / 2;
    int slot = 0;
    for (int i = 0; i < 3; ++i) {
      if (!want[i]) continue;
      if (slot >= L_.conf_lines) break;
      lv_obj_t *l = cline_[slot];
      lv_obj_set_y(l, y0 + slot * pitch);
      std::string text;
      if (i == 0) {
        text = pick(confirm_words().question, cline_w_);
      } else if (i == 1) {
        text = band_text(b);
      } else {
        const TextKeys k = confirm_words().shared;
        const std::string n = std::to_string(session_.rooms());
        text = tessera::fill(plugin_->text(k.full), "n", n);
        if (ui::text_width(text, Font::BODY) > cline_w_) text = fit_cut(tessera::fill(plugin_->text(k.shortly), "n", n), cline_w_, Font::BODY);
      }
      ui::set_text(l, text);
      ui::set_color(l, i == 1 ? theme::INK : theme::MUTED);
      ++slot;
    }
    for (int i = slot; i < 3; ++i) show(cline_[i], false);
    for (int i = 0; i < 2; ++i) key_state(conf_[i], true);
  }
  // "08:30-10:30 Heating 21.5°": the long mode word, else the short one, else cut.
  std::string band_text(const EditBand &b) const {
    const std::string time = format_clock(b.start) + dash_ + format_clock(b.end);
    std::string temp;
    if (b.mode != Mode::OFF && b.has_temp) temp = format_temp(to_degrees(b.centi), step_, decimal_, true);
    else if (b.mode != Mode::OFF && !b.preset.empty()) temp = b.preset;
    for (bool shortly : {false, true}) {
      const std::string s = band_summary(time, mode_word(plugin_, b.mode, shortly), temp);
      if (ui::text_width(s, Font::BODY) <= cline_w_) return s;
    }
    return fit_cut(band_summary(time, mode_word(plugin_, b.mode, true), temp), cline_w_, Font::BODY);
  }

  // For the bench (docs/PROVA_SU_SCHERMO.md, tools/bench_flow.py): where the keys are, once per change of what is shown.
  void log_pad() {
    const int shown = view_ == EView::LIST ? session_.model().page_rows() : 0;
    const int page = view_ == EView::LIST ? session_.model().page() : 0;
    const int state = session_.opened() ? (int) session_.state() : -1;
    if (logged_view_ == (int) view_ && logged_page_ == page && logged_shown_ == shown && logged_state_ == state && !panel_changed()) return;
    logged_view_ = (int) view_;
    logged_page_ = page;
    logged_shown_ = shown;
    logged_state_ = state;
    logged_ok_ = panel_ok_;
    lv_area_t a;
    lv_obj_update_layout(parent_);
    lv_obj_get_coords(parent_, &a);
    char line[640];
    int n = std::snprintf(line, sizeof line, "CRONOPAD view=%d page=%d state=%d origin=%d,%d", (int) view_, page, state, (int) a.x1, (int) a.y1);
    auto add = [&](const char *name, const Rect &r) {
      if (!r.empty() && n < (int) sizeof line - 40) n += std::snprintf(line + n, sizeof line - n, " %s:%d,%d,%d,%d", name, r.x, r.y, r.w, r.h);
    };
    if (view_ == EView::READ) {
      if (panel_ok_) add("open", L_.open);
    } else if (view_ == EView::LIST) {
      static const char *const rows[PAD_MAX_ROWS] = {"row0", "row1", "row2", "row3", "row4", "row5", "row6",
                                                      "row7", "row8", "row9", "row10", "row11", "row12", "row13"};
      for (int i = 0; i < shown; ++i) add(rows[i], L_.row[i]);
      add("prev", L_.prev);
      add("next", L_.next);
      add("add", L_.add);
    } else if (view_ == EView::CONFIRM) {
      add("cfm-no", L_.conf_no);
      add("cfm-yes", L_.conf_yes);
    } else {
      static const char *const names[K_COUNT] = {"s-60", "s-15", "s+15", "s+60", "e-60", "e-15", "e+15", "e+60", "mode", "t-", "t+", "del", "save", "back"};
      for (int k = 0; k < K_COUNT; ++k) add(names[k], detail_rect(k));
    }
    ESP_LOGI(TAG, "%s", line);
  }
  bool panel_changed() const { return logged_ok_ != panel_ok_; }

  Cronotermostato *plugin_;
  const bool edit_option_;   // the tile's option `edit`: without it the card is the list of bands and nothing else
  char entity_[96]{};
  int pad_ = 0, gap_ = 0;
  int w_ = 0, h_ = 0, line_ = 0, top_ = 0, rows_ = 0, chip_w_ = 0, time_w_ = 0, temp_w_ = 0, now_w_ = 0, mode_w_ = 0, hdr_w_ = 0;
  bool header_ = false, show_now_ = false, short_ = false, holiday_ = false;
  char decimal_ = '.';
  const char *dash_ = "-";
  const char *minus_ = "-";
  const char *note_key_ = nullptr;
  double step_ = 0.5;
  int n_ = 0, minute_ = -2;
  tessera::Text program_;
  Row rows_data_[MAX_SEGMENTS];
  Line row_[MAX_SEGMENTS];
  lv_obj_t *hdr_{}, *note_{}, *more_{};
  // The panel
  PadLayout L_;
  EView view_ = EView::READ;
  bool panel_ok_ = false;
  SetpointRange range_;
  EditInput last_;         // what the sensor said last (kept while the panel is allowed): "Edit" opens on it, Back reloads it
  EditSession session_;
  TextKeys transient_;     // a word from a key, for MESSAGE_MS
  uint32_t transient_at_ = 0, saved_at_ = 0;
  lv_obj_t *parent_{};
  lv_obj_t *lhdr_{}, *lempty_{}, *info_{}, *dhdr_{}, *tval_{}, *cap_[2]{};
  ListRow lrow_[PAD_MAX_ROWS];
  Key foot_[3], det_[K_COUNT], open_, conf_[2];
  lv_obj_t *cline_[3]{};
  int cline_w_ = 0;
  uint32_t confirm_at_ = 0;   // when the question opened (Confirm waits CONFIRM_GUARD_MS)
  int lhdr_w_ = 0, lrow_mode_w_ = 0, lock_w_ = 0, lchip_w_ = 0, lpad_ = 0, lgap_ = 0;
  bool lrow_short_ = false, lrow_lock_ = false;
  int logged_view_ = -1, logged_page_ = -1, logged_shown_ = -1, logged_state_ = -2;
  bool logged_ok_ = false;
};

void Cronotermostato::on_interval(uint32_t now_ms) {
  for (RoomTile *tile : tiles_)
    if (tile) tile->tick(now_ms);
}

bool Cronotermostato::attach(RoomTile *tile) {
  for (RoomTile *&slot : tiles_) {
    if (!slot) {
      slot = tile;
      return true;
    }
  }
  return false;
}

void Cronotermostato::detach(RoomTile *tile) {
  for (RoomTile *&slot : tiles_)
    if (slot == tile) slot = nullptr;
}

void Cronotermostato::setup() {
  add_tile("room", [this]() { return new RoomTile(this); });
  add_card("day", [this]() { return new DayCard(this); }, false);
}

}  // namespace esphome::cronotermostato
