# Cronotermostato

A room tile for the cronotermostato integration of Home Assistant: the target temperature of a room with plus and minus
keys, and a bar with today's bands of its weekly program. A tap on the bar opens the list of today's bands.

> Needs the cronotermostato integration 0.2.0 or later in Home Assistant, and the Tessera app must allow the plugin's
> Home Assistant actions (`cronotermostato.set_manual` and `cronotermostato.resume_auto`) for this display.
> This is an early version: the tile only **shows** things for now. The **+** and **-** keys and the Auto / Manual
> keys do not work yet (they come in the next version). A tap on the bar should open the list of today's bands, but that
> still has to be tried on a real screen (the bench cannot tap the bar).

## Set up

1. In the Tessera app, open Plugins and enable **Cronotermostato** for the screen. The screen builds once.
2. In Layout, place the **Room program** tile on a page. It needs at least 2 columns by 2 rows, and is not offered larger than 3 columns by 2 rows.
3. In the inspector, choose the room's climate entity (for example `climate.living_room`).
4. Choose the room program sensor, `sensor.<room>_program` (in an Italian Home Assistant `sensor.<room>_programma`).
   The list shows every sensor: pick the one of the same room that ends in `_program`.
5. Allow the actions `cronotermostato.set_manual` and `cronotermostato.resume_auto` for the display, if the app asks.

## What the tile shows

- The room's name (when the tile is tall enough), the target in large digits between the **+** key (left) and the
  **-** key (right), and under it the temperature now, with a small amber dot while the thermostat heats and a blue one
  while it cools.
- Under the numbers, a line that says the state of the program: "Auto" and the program's name, "Manual until 18:00",
  "Paused", "No program", "Problem" or "Unavailable". A line "Then Heating 21.5 at 17:00" shows when the tile has the room.
- A bar with today's bands, from 00:00 to 24:00: amber for heating, blue for cooling, grey for off, and a dark tick for
  now. A tap on the bar (not on the rest of the tile) is meant to open the list of today's bands, the one of now marked
  (to be tried on a real screen). The tap zone is the bar and the strip over it, never over the keys: about 6 mm high on the
  CYD. A screen that cannot say where the finger touched never opens the card.

## Good to know

- The times in the list of bands are always 24-hour ("17:00"), also on a screen set to AM/PM; the pill and the "then" line
  follow the screen's own clock format. A known limit.
- With more bands than the list has rows, its last row says how many are left out ("+3 more"), and the rows shown are the ones around now.

- Needs the cronotermostato integration 0.2.0 or later; older versions do not give the tile today's bands.
- The tile does not fit a screen that has only one column in its layout (a vertical CYD or the Waveshare 4.3-inch).
- It lives beside Home Assistant's own schedules: it only commands the rooms you give a program in the integration.

## Development

The logic that needs no screen (bands to pixels, texts, the + and - timing, what the pill says) is in
`components/cronotermostato/cronotermostato_logic.h`, plain C++17. Its tests build and run on a computer, with the
address and undefined-behaviour sanitizers:

```sh
tests/run.sh
```

`tests/parity.inc` holds bar positions computed by the editor's own `stripSegments` (integration repository,
`tests/frontend/cases.json`); the bar must match them within a pixel. `tests/overlap.inc` holds a day with overlapping
bands: the editor draws them over each other, the tile trims the later one (a deliberate difference). Regenerate both with
`node tools/gen_parity.mjs [path of the integration repository]` (default `../cronotermostato`).
