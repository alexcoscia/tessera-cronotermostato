# Cronotermostato

A room tile for the cronotermostato integration of Home Assistant. It shows the room's target temperature with **+** and
**-** keys, the program that rules the room, and a bar with today's bands (heating, cooling, off). A tap on the bar opens
the list of today's bands.

> **Not tried on a real screen yet.** Everything here was checked on the plugin bench only (the pictures, the touch zones,
> and the commands the keys would send). Try it on a spare room first. See "What is not tried yet" below.

## Needs

- The **cronotermostato integration 0.2.0 or later** in Home Assistant. Older versions do not give the tile today's bands.
- A `climate` entity for the room, and the room's program sensor from the integration, `sensor.<room>_program` (in an
  Italian Home Assistant `sensor.<room>_programma`).
- The screen must be allowed to call two Home Assistant actions: `cronotermostato.set_manual` and
  `cronotermostato.resume_auto`. The keys and the pill send them.

## Set up

1. In the Tessera app, open **Plugins** and enable **Cronotermostato** for the screen. The screen builds once.
2. Open the screen's **Layout** and place the **Room program** tile on a page. It starts at one cell and you can grow it
   with the handles up to 3 columns by 2 rows; the full look needs about 2x2 (see "Sizes").
3. In the tile's inspector, choose the room's **climate entity** (for example `climate.living_room`).
4. Choose the **room program sensor**. The list shows every sensor: pick the one of the same room that ends in
   `_program` (`_programma` in Italian).
5. Allow the actions `cronotermostato.set_manual` and `cronotermostato.resume_auto` for this display, if the app asks.
   Without that permission the keys do nothing.
6. Make sure the integration in Home Assistant is version 0.2.0 or later, and that the room has a program assigned.

## What the tile shows

- The room's name (when the tile is tall enough), the target in large digits between the **+** key (left) and the
  **-** key (right), and under it the temperature now, with a small amber dot while the thermostat heats and a blue one
  while it cools.
- A line that says the state of the program: "Auto" and the program's name, "Manual until 18:00 x", "Paused",
  "No program", "Problem" or "Unavailable". A line "Then Heating 21.5 at 17:00" shows when there is room for it.
- A bar with today's bands, from 00:00 to 24:00: amber for heating, blue for cooling, grey for off, and a dark tick for
  now.

## Sizes

The tile is offered from 1x1 up to 3x2. It starts at 1x1 because the layout editor starts a tile at one cell and grows it
one axis at a time: a larger minimum would show no resize handles at all. What it draws depends on the pixels of the
cell on your screen, not on the number of cells, so the same size can look different on a small and on a large screen.

| Room it has | What is drawn |
|---|---|
| Tall and wide enough (a 2x2 or more; also a 1x1 or 2x1 on a large screen) | The full look above: name, target, temperature now, "Then" line, state line and bar, with the **+** and **-** keys. |
| Wide but low (a 2x1 on a small screen) | **+** at the left, the target between, **-** at the right. No bar and no state line; the temperature now only if it fits under the target (the dot then stands beside the target). A tap between the keys opens the list of today's bands. |
| Narrow but tall (a 1x2, or a 1x1) | The target with the dot beside it, and the bar of today's bands under it. No keys. A tap anywhere opens the list of bands. |
| Narrow and low (a 1x1 on a small screen) | The target (the temperature now if the room has no target) and the dot of heating or cooling beside it, with the bar under it when the height holds it (on the screens of the bench it does). A tap anywhere opens the list of bands. |

Without the keys the tile never commands a temperature. Without the state line a manual override is not shown on the tile
(the list of bands opened by a tap does not show it either): use a 2x2 for a room you force often.

## What a tap does

| Where | What happens |
|---|---|
| **+** (left) or **-** (right) | The target moves one step (the thermostat's own step and limits) and shows at once. 600 ms after the last tap one command goes out, `cronotermostato.set_manual` with the new temperature: the room is forced **until the end of the current band**, then the program takes over again. If Home Assistant does not report the new target within 5 seconds, the tile shows the real one again. |
| The pill, while it says **Manual ... x** | Back to the program: one command, `cronotermostato.resume_auto`. The pill is drawn as a key (a chip behind it) only then. |
| The bar, and the strip just over it | Opens the list of today's bands, the one of now marked. |
| Anywhere else | Nothing. |

## Limits

- **+** and **-** work only while the thermostat is in `heat` or `cool`, and they send that same mode with `set_manual`.
  They never switch a room on and never change its mode. In any other state (off, auto, heat_cool, dry, fan_only,
  unavailable) the keys are dimmed and do nothing.
- The keys are also dimmed and do nothing when the room has no program assigned, is paused, is unavailable, or the tile
  has no usable climate entity.
- There is no separate "Manual" key: pressing **+** or **-** already forces the room, and the pill then says "Manual".
- The tile is offered from 1x1 up to 3x2. The keys and the full look need room: on a small screen only the 2x1 keeps the
  keys, and a 1x1 or 1x2 is read-only (see "Sizes").
- On a screen with one column in its layout (a vertical CYD or the Waveshare 4.3-inch) only 1x1 and 1x2 fit (read-only, see "Sizes"); the 2x1 and larger do not.
- The times in the list of bands are always 24-hour ("17:00"), also on a screen set to AM/PM. The pill and the "then"
  line follow the screen's own clock format.
- With more bands than the list has rows, its last row says how many are left out ("+3 more"), and the rows shown are
  the ones around now.
- On a small tile (the 2.8-inch CYD) the pill's touch zone is about 4.4 mm high, less than the 7 mm of a comfortable key;
  on the larger screens it is 7 mm. The keys' zones are at least 11 mm wide.
- A screen that cannot say where the finger touched does nothing on a tap.

## Good to know

- It lives beside Home Assistant's other schedules (the Scheduler, for example). It only commands the rooms you give a
  program to in the integration.
- A manual change lasts until the end of the current band. To keep a room at a temperature for longer, change the
  program in the integration.

## What is not tried yet

- **A real screen.** The tile was drawn and its touch zones were checked on the plugin bench on several screens, light and
  dark. It was not built for and run on a real display, so how the zones fall under a real finger, the colours in the
  room's light and the speed of the screen are unknown.
- **A real Home Assistant.** The keys and the pill send the two actions with the right fields (checked on the bench), but
  they were not yet sent to a real Home Assistant, nor to a real thermostat. The tap on the bar and the list of bands
  (the card) were drawn on the bench but not tried by hand on a screen.

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

The notes of the first spike on the plugin API are in `docs/SPIKE.md` (history). The checklist for trying the plugin on a
real screen is in `docs/PROVA_SU_SCHERMO.md` (Italian).
