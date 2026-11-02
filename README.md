# Cronotermostato

A room tile for the cronotermostato integration of Home Assistant: the target temperature of a room with plus and minus
keys, and a bar with today's bands of its weekly program. A tap on the bar opens the list of today's bands.

> Needs the cronotermostato integration 0.2.0 or later in Home Assistant, and the Tessera app must allow the plugin's
> Home Assistant actions (`cronotermostato.set_manual` and `cronotermostato.resume_auto`) for this display.
> The keys and the pill send those two actions to Home Assistant. They are checked on the plugin bench (the action and its
> fields are right, and nothing leaves before 600 ms), **not yet on a real screen nor against a real thermostat**: try
> them on a spare room first.

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
- Under the numbers, a line that says the state of the program: "Auto" and the program's name, "Manual until 18:00 x",
  "Paused", "No program", "Problem" or "Unavailable". A line "Then Heating 21.5 at 17:00" shows when the tile has the room.
- A bar with today's bands, from 00:00 to 24:00: amber for heating, blue for cooling, grey for off, and a dark tick for
  now.

## What a tap does

| Where | What happens |
|---|---|
| **+** (left) or **-** (right) | The target moves one step (the thermostat's own step and limits) and shows at once. 600 ms after the last tap one command goes out, `cronotermostato.set_manual` with the new temperature: the room is forced **until the end of the current band**, then the program takes over again. If Home Assistant does not report the new target within 5 seconds, the tile shows the real one again. |
| The pill, while it says **Manual ... x** | Back to the program: one command, `cronotermostato.resume_auto`. The pill is drawn as a key (a chip behind it) only then. |
| The bar (and the strip just over it) | Opens the list of today's bands, the one of now marked. |
| Anywhere else | Nothing. |

- There is no separate "Manual" key: pressing **+** or **-** already forces the room, and the pill then says "Manual".
- `set_manual` needs a mode. The tile sends the mode the thermostat is in now (heating or cooling), else the mode of the
  band of now; it never changes the mode. If the thermostat is off and the band is off too, the keys stay inert (a guess
  could switch the room on).
- The keys are dimmed and do nothing when the room has no program assigned, is paused, is unavailable, or the tile has no
  climate entity. The pill acts only for a manual override.
- The zones never overlap. The keys' zones are at least 11 mm wide; the pill and the bar zones run the full width of the
  tile. On a small tile (the 2.8-inch CYD) the pill zone is less than the 7 mm of a comfortable key (the numbers are in the
  report of the step); on the larger screens it is 7 mm.
- A screen that cannot say where the finger touched (`tap_point()` gives -1,-1) does nothing on a tap. Where the zones
  fall on a real finger is to be tried on a real screen.

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
