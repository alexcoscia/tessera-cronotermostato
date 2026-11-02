# Changelog

## 0.1.1
- The tile is now offered from **1x1** (up to 3x2, as before). The layout editor starts a tile at one cell and grows it one
  axis at a time, so with a minimum of 2x2 the resize handles never appeared and the size stayed at 1x1.
- Small sizes are drawn on purpose, by the pixels of the cell: a wide, low tile (2x1 on a small screen) keeps the **+** and
  **-** keys around the target; a narrow, tall one (1x2) shows the target, the dot and the bar; a narrow, low one (1x1)
  shows the target alone (or the temperature now when the room has no target) with the heating or cooling dot. Without
  keys the whole tile opens the list of today's bands and never commands a temperature; the keys still work only in
  heat or cool. From 2x2 nothing changes.
- The full look now also checks the width for the keys, not only the height.

## 0.1.0
- First version: a room tile (2x2 up to 3x2) for the cronotermostato integration. It shows the target and the temperature
  now, the state of the program (Auto and its name, Manual until a time, Paused, No program, Problem) and a bar with
  today's bands. A tap on the bar opens the list of today's bands.
- The **+** and **-** keys force the room with `cronotermostato.set_manual`: one command 600 ms after the last tap, until
  the end of the current band. If Home Assistant does not report the new target within 5 s, the tile shows the real one
  again. The keys work only while the thermostat is in heat or cool, send that same mode, and never switch a room on or
  change its mode.
- The pill "Manual ... x" goes back to the program with `cronotermostato.resume_auto`.
- What you must do: use the integration 0.2.0 or later, choose the room's climate entity and its program sensor
  (`sensor.<room>_program`) in the tile's inspector, and allow the two Home Assistant actions
  (`cronotermostato.set_manual`, `cronotermostato.resume_auto`) for the display.
- Limits: no separate Manual key; times in the list of bands are always 24-hour.
- Checked on the plugin bench only; not yet tried on a real screen nor against a real thermostat.
