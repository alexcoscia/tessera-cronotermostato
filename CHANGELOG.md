# Changelog

## 0.1.0
- First version: the room tile (2x2 up to 3x2) shows the target and the temperature now, the state of the program (Auto
  and its name, Manual until a time, Paused, No program, Problem) and a bar with today's bands.
- The **+** and **-** keys force the room with `cronotermostato.set_manual` (one command 600 ms after the last tap, until the
  end of the band; the shown value goes back to the real one after 5 s without an answer). The pill "Manual ... x" goes
  back to the program with `cronotermostato.resume_auto`. A tap on the bar is meant to open the list of today's bands.
- Checked on the plugin bench only; not yet tried on a real screen nor against a real thermostat.
- Needs the cronotermostato integration 0.2.0 or later, and the app must allow the two Home Assistant actions for the display.
