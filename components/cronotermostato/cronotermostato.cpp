// Cronotermostato: the room tile and the card with today's bands. This is the skeleton (plan 3, step 2): the real
// drawing and the keys come in steps 4 and 5. What was tried before is in docs/SPIKE.md.
#include "cronotermostato.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::cronotermostato {

static const char *const TAG = "cronotermostato";
using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

constexpr int MAX_SEGMENTS = 16;   // the most bands the bar draws
constexpr int DAY = 1440;          // minutes

struct Band {
  int start = 0, end = 0, mode = 0;   // minutes from midnight; mode 0 off, 1 heat, 2 cool
  float temp = 0;
};

class RoomTile : public tessera::Tile {
 public:
  explicit RoomTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    std::snprintf(entity_, sizeof entity_, "%s", c.entity);
    tile_ = c.tile;
    w_ = c.width;
    h_ = c.height;
    const int gap = ui::px(4);
    const int body_h = ui::line_height(Font::BODY), value_h = ui::line_height(Font::VALUE);
    bar_h_ = std::max(ui::px(8), ui::mm(2.5f));
    tick_h_ = bar_h_ + 2 * ui::px(2);
    // From the top: name, the keys row, pill, bar. The bar is at the bottom.
    name_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    ui::set_text(name_, c.name);
    lv_obj_set_width(name_, w_);
    lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
    pill_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(pill_, w_);
    lv_obj_set_style_text_align(pill_, LV_TEXT_ALIGN_CENTER, 0);
    bar_y_ = h_ - tick_h_ + (tick_h_ - bar_h_) / 2;
    pill_y_ = h_ - tick_h_ - gap - body_h;
    int top = 0;
    show_name_ = h_ >= body_h + value_h + body_h + tick_h_ + 4 * gap + ui::mm(9);
    if (show_name_) top = body_h + gap; else lv_obj_add_flag(name_, LV_OBJ_FLAG_HIDDEN);
    keys_y_ = top;
    keys_h_ = std::max(ui::mm(9), pill_y_ - gap - top);
    key_w_ = std::min(ui::mm(11), w_ / 4);
    lv_obj_set_pos(name_, 0, 0);
    lv_obj_set_pos(pill_, 0, pill_y_);

    // Plus at the LEFT, minus at the RIGHT (Alex's choice), the setpoint and the temperature now between them.
    for (int i = 0; i < 2; ++i) {
      key_[i] = ui::block(c.parent, theme::TRACK);
      key_label_[i] = ui::label(key_[i], Font::BODY_LARGE, theme::INK);
      lv_obj_center(key_label_[i]);
      lv_obj_set_size(key_[i], key_w_, std::min(ui::mm(12), keys_h_));
      lv_obj_set_pos(key_[i], i == 0 ? 0 : w_ - key_w_, keys_y_ + (keys_h_ - std::min(ui::mm(12), keys_h_)) / 2);
    }
    ui::set_text(key_label_[0], "+");
    ui::set_text(key_label_[1], "-");
    const int mid_x = key_w_ + gap, mid_w = w_ - 2 * (key_w_ + gap);
    const int block = value_h + body_h;
    const int y0 = keys_y_ + std::max(0, (keys_h_ - block) / 2);
    set_ = ui::label(c.parent, Font::VALUE, theme::INK);
    lv_obj_set_width(set_, mid_w);
    lv_obj_set_style_text_align(set_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(set_, mid_x, y0);
    now_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(now_, mid_w);
    lv_obj_set_style_text_align(now_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(now_, mid_x, y0 + value_h);

    // The bar: a track, up to MAX_SEGMENTS coloured blocks, and the "now" tick (taller than the bar).
    track_ = ui::block(c.parent, theme::TRACK);
    lv_obj_set_size(track_, w_, bar_h_);
    lv_obj_set_pos(track_, 0, bar_y_);
    for (int i = 0; i < MAX_SEGMENTS; ++i) {
      seg_[i] = ui::block(c.parent, theme::SUBTLE);
      lv_obj_set_style_radius(seg_[i], ui::px(2), 0);
      lv_obj_add_flag(seg_[i], LV_OBJ_FLAG_HIDDEN);
    }
    tick_ = ui::block(c.parent, theme::INK);
    lv_obj_set_size(tick_, std::max(2, ui::px(2)), tick_h_);
    lv_obj_add_flag(tick_, LV_OBJ_FLAG_HIDDEN);
  }

  void on_state(JsonObjectConst data) override {
    state_ = data["state"] | "";
    JsonObjectConst a = data["attributes"];
    target_ = a["temperature"] | -1000.0f;
    current_ = a["current_temperature"] | -1000.0f;
    JsonObjectConst room = data["entities"]["room"];
    have_room_ = !room.isNull();
    JsonObjectConst r = room["attributes"];
    JsonArrayConst s = r["today_start"], e = r["today_end"], m = r["today_mode"], t = r["today_temp"];
    n_ = 0;
    for (size_t i = 0; i < s.size() && i < (size_t) MAX_SEGMENTS; ++i) {
      band_[n_].start = s[i] | 0;
      band_[n_].end = e[i] | 0;
      band_[n_].mode = m[i] | 0;
      band_[n_].temp = t[i] | 0.0f;
      ++n_;
    }
    manual_ = r["manual"] | false;
    paused_ = std::string(r["status"] | "") == "paused";
    // Moments come as seconds since 1970 (the attribute names end in _time).
    until_ = manual_ ? (uint32_t) (r["override_until_time"] | 0) : (uint32_t) (r["slot_end_time"] | 0);
  }

  void on_tick(uint32_t epoch) override {
    const bool offline = state_ == "unavailable" || state_ == "unknown";
    char buf[24];
    if (target_ > -999 && !offline) std::snprintf(buf, sizeof buf, "%.1f\xC2\xB0", target_); else std::snprintf(buf, sizeof buf, "-");
    ui::set_text(set_, buf);
    if (current_ > -999 && !offline) {
      std::snprintf(buf, sizeof buf, "%.1f\xC2\xB0", current_);
      ui::set_text(now_, tessera::fill(plugin_->text("now"), "n", buf));
    } else {
      ui::set_text(now_, "");
    }
    ui::set_tone(set_, mood_tone(), theme::INK);
    ui::set_color(name_, theme::MUTED);
    ui::set_color(now_, theme::MUTED);
    std::string pill;
    if (offline) pill = plugin_->text("unavailable");
    else if (!have_room_) pill = plugin_->text("no_room");
    else if (paused_) pill = plugin_->text("paused");
    else if (until_) pill = tessera::fill(plugin_->text("until"), "time", tessera::clock_text(until_));
    else pill = plugin_->text(manual_ ? "manual" : "auto");
    ui::set_text(pill_, pill);
    ui::set_tone(pill_, manual_ ? Tone::BUSY : Tone::NORMAL, theme::MUTED);
    for (int i = 0; i < 2; ++i) ui::set_color(key_label_[i], offline ? theme::MUTED : theme::INK);

    // The bar: x = minutes * width / 1440; a band shorter than 1 px still gets one.
    for (int i = 0; i < MAX_SEGMENTS; ++i) {
      if (i >= n_) {
        lv_obj_add_flag(seg_[i], LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      const int x0 = band_[i].start * w_ / DAY, x1 = band_[i].end * w_ / DAY;
      lv_obj_remove_flag(seg_[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(seg_[i], x0, bar_y_);
      lv_obj_set_size(seg_[i], std::max(1, x1 - x0), bar_h_);
      // heat: Home Assistant's amber (BUSY); cool: the look's blue (ACCENT); off: grey (the role SUBTLE).
      ui::set_fill(seg_[i], band_[i].mode == 1 ? Tone::BUSY : band_[i].mode == 2 ? Tone::ACCENT : Tone::NORMAL, theme::SUBTLE);
    }
    ui::set_fill(track_, Tone::NORMAL, theme::TRACK);
    if (epoch) {
      const tessera::LocalTime lt = tessera::local_time(epoch);
      const int minutes = lt.hour * 60 + lt.minute;
      lv_obj_remove_flag(tick_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(tick_, std::min(w_ - 2, minutes * w_ / DAY), bar_y_ - (tick_h_ - bar_h_) / 2);
      ui::set_fill(tick_, Tone::NORMAL, theme::INK);
    }
  }

  void on_theme() override { on_tick(tessera::epoch()); }

  // A tap opens the list of today's bands (the final tile limits this to the bar, step 5).
  void on_tap() override { tessera::open_card(plugin_->plugin_id(), "day", entity_, tile_); }

 private:
  Tone mood_tone() const {
    if (state_ == "heat") return Tone::BUSY;
    if (state_ == "cool") return Tone::ACCENT;
    return Tone::NORMAL;
  }

  const tessera::Plugin *plugin_;
  char entity_[96]{};
  std::string state_;
  uint32_t until_ = 0;
  int w_ = 0, h_ = 0, tile_ = -1, bar_y_ = 0, bar_h_ = 0, tick_h_ = 0, pill_y_ = 0, keys_y_ = 0, keys_h_ = 0, key_w_ = 0;
  bool show_name_ = false, have_room_ = false, manual_ = false, paused_ = false;
  float target_ = -1000, current_ = -1000;
  Band band_[MAX_SEGMENTS];
  int n_ = 0;
  lv_obj_t *name_{}, *pill_{}, *set_{}, *now_{}, *track_{}, *tick_{};
  lv_obj_t *key_[2]{}, *key_label_[2]{}, *seg_[MAX_SEGMENTS]{};
};

// The card: one row per band of today, "08:00-12:00  Heat  21.5".
class DayCard : public tessera::Card {
 public:
  explicit DayCard(const tessera::Plugin *plugin) : plugin_(plugin) {}
  void open(const tessera::CardContext &c) override {
    const int line = ui::line_height(Font::BODY) + ui::px(4);
    rows_ = std::max(1, std::min(MAX_SEGMENTS, c.height / line));
    for (int i = 0; i < rows_; ++i) {
      row_[i] = ui::label(c.parent, Font::BODY, theme::INK);
      lv_obj_set_width(row_[i], c.width);
      lv_obj_set_pos(row_[i], 0, i * line);
    }
  }
  void on_state(JsonObjectConst data) override {
    JsonObjectConst r = data["entities"]["room"]["attributes"];
    JsonArrayConst s = r["today_start"], e = r["today_end"], m = r["today_mode"], t = r["today_temp"];
    for (int i = 0; i < rows_; ++i) {
      if (i >= (int) s.size()) {
        ui::set_text(row_[i], i == 0 ? plugin_->text("no_bands") : "");
        continue;
      }
      char buf[64];
      const int a = s[i] | 0, b = e[i] | 0, mode = m[i] | 0;
      const float temp = t[i] | 0.0f;
      const char *mode_key = mode == 1 ? "heat" : mode == 2 ? "cool" : "off";
      if (temp > -99.0f)
        std::snprintf(buf, sizeof buf, "%02d:%02d-%02d:%02d  %s  %.1f", a / 60, a % 60, b / 60 % 24, b % 60,
                      plugin_->text(mode_key), (double) temp);
      else
        std::snprintf(buf, sizeof buf, "%02d:%02d-%02d:%02d  %s", a / 60, a % 60, b / 60 % 24, b % 60, plugin_->text(mode_key));
      ui::set_text(row_[i], buf);
    }
  }
  void on_theme() override {
    for (int i = 0; i < rows_; ++i) ui::set_color(row_[i], theme::INK);
  }

 private:
  const tessera::Plugin *plugin_;
  int rows_ = 0;
  lv_obj_t *row_[MAX_SEGMENTS]{};
};

void Cronotermostato::setup() {
  add_tile("room", [this]() { return new RoomTile(this); });
  add_card("day", [this]() { return new DayCard(this); }, false);
}

}  // namespace esphome::cronotermostato
