# Changelog

## 0.2.1
- **Delete asks first.** The key Delete of the band no longer deletes and saves at once: it opens a question inside the card
  with the band ("08:30-10:30 Heating 21.5°"), a warning "Shared program: N rooms" when the program has more than one
  room, and two big keys (7 mm or more): **Cancel** (left) and **Confirm** (right, a wide gap apart). Cancel goes back to the
  band unchanged and sends nothing; Confirm does what Delete did before (deletes and saves with the revision seen; if it
  fails, the list shows the band again).
- Against a stray double tap: Confirm is not where Delete is, and it ignores taps for 600 ms after the question opens.
- The question closes without sending when the room can no longer be commanded (paused, unavailable, no program, unreadable
  `edit_info`) or when the bands change elsewhere, the day changes or a save answer arrives. Damaged data, a locked band or
  a save under way stop Delete before it asks. It fits the small 320x240 boards.
- New texts (English and Italian): `confirm_q`, `confirm_shared`, `confirm_yes`, `confirm_no` and their short forms.
- Needs the same integration as 0.2.0 (0.3.2 or later). No new permission.

## 0.2.0
- **Edit the bands from the display.** The card of the bands has a key **Edit** (new option **Allow editing the bands**, on
  by default). It lists the user bands of today (the automatic "off" bands are the integration's, never a row), with pages
  `<` `>` and **+** to add one. A tap on a row opens the band: start and end with -1 h, -15 min, +15 min, +1 h; the mode
  (heat, cool, off); the temperature with the step of the thermostat; Delete, Save and Back. Save sends
  `cronotermostato.set_day` with the day it saw and the revision of the program it saw, and says Saving, Saved or the reason
  it was not saved.
- The program is shared: the change reaches every room that uses it ("Home - 3 rooms" is shown). Only today (or the
  holiday part when today is one) can be edited here; the other days stay in Home Assistant.
- If the program changes elsewhere while the list is open, the list reloads ("Changed elsewhere"); a locked band is shown
  but not changed, and a day that has one cannot be saved from here.
- Needs the integration **0.3.2 or later**: update the integration first, then the plugin. With the option off, or with an older integration, the card is the read-only
  list of 0.1.x.
- The new permission is `cronotermostato.set_day` (the manifest names it).

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
