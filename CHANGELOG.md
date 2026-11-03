# Changelog

## 0.2.3
- **Sharp colours per value.** The shades of 0.2.2 (a share of ink mixed into the colour of the mode) were too faint on the
  panel, as they were in the editor. The tile now takes the **palette of the editor in Home Assistant** (integration commit
  4e529dc, `bandTints`): for each mode 4 tints of different hue, not only of different lightness.
  - Heat: amber, brick red, gold, olive. Cool: pale blue, cyan, violet, azure. Off: four greys.
  - Used for the fill of the bar and for the chip of the rows of the card (reading and editing lists).
  - The text on a band is the text colour of the palette for that tint, no longer "ink or the card's ground": at least 4.5 to 1
    on every fill, in both looks (tested).
  - Tint 0, the plain colour of the mode, now comes from the palette too (before: Tessera's amber and blue). The old amber
    and blue were too near gold and azure (CIEDE2000 13 from the azure, 17 from the gold); the palette's first tints are paler
    in the light look and deeper in the dark one, as in the editor.
  - The look (light or dark) of the screen picks the column of colours. Plain colour constants in the code: no word, no translation.
- Tests: the palette is checked against the editor's `styles.js` (`tools/gen_parity.mjs` makes `tests/palette.inc`), text
  4.5 to 1 and tints of one mode at least 15 apart (CIEDE2000) in both looks, heat at least 15 from cool and off. Cool against off
  has less room: the editor's pale blue and pale grey of tint 0 are 7.8 apart in the light look, so the test asks 7 (an off band
  also says "Off" in the list).
- Works with integration 0.3.2 or later; the tile reads the same data.

## 0.2.2
- **Bands that differ in value are told apart.** Until now two neighbouring heating bands with another preset or another
  temperature looked the same on the bar (the colour only followed the mode). Now, like the bar of the editor in Home
  Assistant (the integration after 0.3.2, commit 7c9113d):
  - Neighbouring bands with the same mode and temperature but **another preset are no longer merged** (the same preset
    still is). The preset is read from `today_preset`, which the tile already had: no new attribute (the manifest stays at
    16) and no new permission.
  - **A tint per value.** In a day, the same mode and the same value (the temperature, or the preset when the band has no
    temperature) always get the same shade; the first value met of a mode is its plain colour, the next ones are darker in
    the light look and lighter in the dark look, 4 shades, then it starts again. The text is neutral (ink or the card's
    ground, whichever reads better): on every shade of heat, cool and off, in both looks, it has at least 4.5 to 1 (tested).
  - **A clear joint** of 2 px between neighbouring bands (the track shows through). A band never has less than 1 px.
  - **The value on the band** when it fits: the temperature, or the preset when there is no temperature, cut with dots if
    it is too long; a narrow band shows a sigla (the temperature rounded as shown, or two letters of the preset), a band
    under about 12 px shows nothing. Only from the 2x2 look up and never in a 1x1 tile (colours and joints only); the bar
    grows to one line of text when the tile keeps the same look with it, else it stays thin without labels. The label moves
    aside, or goes, when the "now" tick would stand on it.
  - **The card.** The row of the list shows the same value as the bar: the temperature, the preset (it was missing in the
    reading list), or both ("21.5° away") when there is room, with the colour of the band in the chip. The editing list got
    the same chip and the same value (before, a preset was cut to the width of "21.5°"). The card has no bar of its own.
- The tile keeps the preset of each band (at most 40 bytes, blanks trimmed): `memory` goes from 3000 to 5000.
- Tests: tints, joints, labels and sigla are checked against the editor's own functions (`tools/gen_parity.mjs` makes
  `tests/tints.inc`, `labels.inc` and `siglas.inc`; it needs the integration repository at commit 7c9113d or later, for `bandTints`).
- Works with integration 0.3.2: the tile reads the same data. Only the bar of the editor in Home Assistant is newer.

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
