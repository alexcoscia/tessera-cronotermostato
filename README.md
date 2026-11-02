# Cronotermostato

A room tile for the cronotermostato integration of Home Assistant: the target temperature of a room with plus and minus
keys, and a bar with today's bands of its weekly program. A tap on the bar opens the list of today's bands.

> Needs the cronotermostato integration 0.2.0 or later in Home Assistant, and the Tessera app must allow the plugin's
> Home Assistant actions (`cronotermostato.set_manual` and `cronotermostato.resume_auto`) for this display.
> This is an early version: the drawing and the keys are still being built.

## Set up

1. In the Tessera app, open Plugins and enable **Cronotermostato** for the screen. The screen builds once.
2. In Layout, place the **Room program** tile on a page. It needs at least 2 columns by 2 rows.
3. In the inspector, choose the room's climate entity (for example `climate.living_room`).
4. Choose the room program sensor, `sensor.<room>_program` (in an Italian Home Assistant `sensor.<room>_programma`).
   The list shows every sensor: pick the one of the same room that ends in `_program`.
5. Allow the actions `cronotermostato.set_manual` and `cronotermostato.resume_auto` for the display, if the app asks.

## Good to know

- Needs the cronotermostato integration 0.2.0 or later; older versions do not give the tile today's bands.
- The tile does not fit a screen that has only one column in its layout (a vertical CYD or the Waveshare 4.3-inch).
- It lives beside Home Assistant's own schedules: it only commands the rooms you give a program in the integration.
