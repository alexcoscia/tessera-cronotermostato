# Ideas for later

## Choose the day to edit from the display

Today the card edits only today's day (or the holiday part on a holiday). To pick any day (`mon` to `sun`, `holiday`):

- **Integration**: `cronotermostato.set_day` accepts only today's `day_kind` and answers `stale_day` otherwise (a page left
  open across midnight cannot write on the wrong day). It needs a `day` field, or a new service, that takes any day; decide
  what stays of the `stale_day` guard (for example: it applies only when the caller asks for "today").
- **Plugin**: seven day keys and a holiday key on the card, and the bands of that day. The program sensor gives today's
  bands only (`today_*` attributes), and the manifest is already at the maximum of 16 attributes of an entity option, so
  the other days need another way to be read (a second sensor, or the bands of one chosen day in the same attributes).
- The program is shared: the title ("Home - 3 rooms") must stay, and a change to another day reaches every room of the program too.

Not for the first listing: the store review (tessera-plugins #44) takes the editing of today as it is.
