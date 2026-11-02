#pragma once
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::cronotermostato {

class RoomTile;

// The plugin: an ESPHome component and a tessera::Plugin. It registers its tile types in setup(), and drives the waiting
// of its tiles (the pause after + and -, the wait for Home Assistant) from on_interval, as the plugin rules ask: a tile
// owns no timer of its own.
class Cronotermostato : public Component, public tessera::Plugin {
 public:
  static constexpr int MAX_TILES = 16;
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_interval(uint32_t now_ms) override;
  // A tile made / gone. attach() is false when MAX_TILES are already there: that tile then sends at once.
  bool attach(RoomTile *tile);
  void detach(RoomTile *tile);

 private:
  RoomTile *tiles_[MAX_TILES]{};
};

}  // namespace esphome::cronotermostato
