# Changelog

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
- Limits: no separate Manual key; not for screens with one column; times in the list of bands are always 24-hour.
- Checked on the plugin bench only; not yet tried on a real screen nor against a real thermostat.
