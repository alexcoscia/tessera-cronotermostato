"""Touch flow on the Tessera bench for the edit panel of the card (plan 4, step D).

It runs the normal bench of tessera-plugins on this plugin and then, on the screen's own grid, drives real taps through the
firmware's touch filter, once in the light look and once in the dark one: it opens the card of a room tile, enters the editing, adds a band, saves, answers the way the
integration would (new bands, a refusal, a change made elsewhere), deletes, waits for the timeout and opens a tile whose
option `edit` is off. Each step is photographed (`flow-<step>-<light|dark>.png`), audited,
and the checks are printed as `FLOW <screen> <look> PASS|FAIL <what>`; the exit status is 1 when a check fails.

The keys are found from the line `CRONOPAD view=... name:x,y,w,h ...` the card logs when what it shows changes.

Where things are (nothing is assumed outside the two folders):
  TESSERA_PLUGINS   the tessera-plugins checkout (default: ../tessera-plugins beside this repository)
  ESPHOME, TESSERA_TREE, TESSERA_BENCH_WORK, RENDER_PORT_BASE   as the bench reads them; tools/bench_flow.sh sets them from
                    the checkout's cache (tools/.cache), so builds are shared with `preview.py`.
Run it with the bench's Python (`tools/bench_flow.sh cyd`), never with the system one.
"""
import ast
import asyncio
import copy
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
PLUGINS = Path(os.environ.get('TESSERA_PLUGINS') or REPO.parent / 'tessera-plugins').expanduser().resolve()
sys.path.insert(0, str(PLUGINS / 'tools'))
import bench  # noqa: E402  (the bench of tessera-plugins)

PAD = re.compile(r'CRONOPAD view=(\d) page=(-?\d+) state=(-?\d+) origin=(-?\d+),(-?\d+)(.*)')
KEY = re.compile(r'(\S+?):(-?\d+),(-?\d+),(\d+),(\d+)')
LOG, FAILS = [], []
CACHE = {}
THEME = ['light']


def say(self, text):
    LOG.append(f'[{THEME[0]}] {text}')
    print('FLOW', self.screen.key, THEME[0], text, flush=True)


def check(self, ok, what):
    say(self, ('PASS ' if ok else 'FAIL ') + what)
    if not ok:
        FAILS.append(f'{self.screen.key} {THEME[0]}: {what}')
    return ok


def pad_now(self, since=0):
    """(view, page, state, {name: (x, y, w, h)} in screen pixels) of the last CRONOPAD line after line `since`."""
    for line in reversed(self.lines[since:]):
        m = PAD.search(line)
        if m:
            ox, oy = int(m.group(4)), int(m.group(5))
            keys = {k[0]: (ox + int(k[1]), oy + int(k[2]), int(k[3]), int(k[4])) for k in KEY.findall(m.group(6))}
            return int(m.group(1)), int(m.group(2)), int(m.group(3)), keys
    return None, None, None, {}


async def wait_pad(self, view, since, timeout=4.0):
    """Until the card has logged its keys for `view` (0 read, 1 list, 2 detail); the keys then."""
    end = asyncio.get_running_loop().time() + timeout
    while asyncio.get_running_loop().time() < end:
        v, page, state, keys = pad_now(self, since)
        if v == view:
            return page, state, keys
        await asyncio.sleep(0.15)
    return None, None, {}


async def press(self, name, wait=0.8):
    """A real tap on the middle of a key; the actions that came of it."""
    _, _, _, keys = pad_now(self)
    if name not in keys:
        check(self, False, f'key {name} is on the screen (keys: {sorted(keys)})')
        return None
    x, y, w, h = keys[name]
    before = len(self.actions)
    await self.tap(x + w // 2, y + h // 2)
    await asyncio.sleep(wait)
    return [a for a in self.actions[before:]]


def action_fields(text):
    """The fields of a `service {dict}` line the bench keeps for an action."""
    service, rest = text.split(' ', 1)
    return service, ast.literal_eval(rest)


async def shots(self, name, audit=True):
    """The step in the look the flow runs in (the card takes the page's ground when it opens, so the look is chosen before
    the card is opened, not switched under it), audited."""
    theme = THEME[0]
    img = await self.render(f'flow-{name}-{theme}', keep=False)
    img.save(self.out / f'flow-{name}-{theme}.png')
    if audit:
        self.faults(await self.audit(card=True), f'flow {name}, {theme}')


def room_attrs(values, index):
    return values[index]['x']['entities']['room']['attributes']


def user_bands_of(attrs):
    out = []
    for i, s in enumerate(attrs['today_start']):
        if not int(attrs['today_flags'][i]) & 1:
            out.append(dict(s=s, e=attrs['today_end'][i], mode=attrs['today_mode'][i], temp=attrs['today_temp'][i]))
    return out


def with_bands(values, index, bands, revision, error='', seq=0, status=None, kind=None):
    """A copy of the messages in which the room sensor says `bands` (user bands; the gaps become automatic "off" bands)
    under the revision and the last error of the integration."""
    new = copy.deepcopy(values)
    a = room_attrs(new, index)
    start, end, mode, temp, flags, preset = [], [], [], [], [], []
    cursor = 0
    for b in sorted(bands, key=lambda b: b['s']):
        if b['s'] > cursor:
            start.append(cursor); end.append(b['s']); mode.append(0); temp.append(-100); flags.append(1); preset.append('')
        start.append(b['s']); end.append(b['e']); mode.append(b['mode']); temp.append(b['temp']); flags.append(0); preset.append('')
        cursor = b['e']
    if cursor < 1440:
        start.append(cursor); end.append(1440); mode.append(0); temp.append(-100); flags.append(1); preset.append('')
    a.update(today_start=start, today_end=end, today_mode=mode, today_temp=temp, today_flags=flags, today_preset=preset,
             edit_n=len(start), edit_seq=seq)
    old = a['edit_info'].split(';')
    a['edit_info'] = f'{kind or old[0]};{old[1]};{revision};{error}'
    if status:
        a['status'] = status
    return new


async def send_values(self, values):
    record, _, _, bars, _ = CACHE['layout']
    await self.sender.synchronize(self.inbox.object_id, record, bench.REGION, values, bars)
    await asyncio.sleep(1.2)


async def open_card(self, grid, compiled, index):
    """Taps the bar of tile `index` (on its page) until the card has opened; the log position of its first pad line."""
    page = grid.page_of(compiled[index]['slot'])
    await self.call('render_page', page=page)
    await self.page_done(page)
    await asyncio.sleep(0.6)
    _, area = await self.box(index)
    x1, y1, x2, y2 = area
    for dy in (6, 10, 14, 20, 26, 32, 40, 50, 60, 72, 84):
        mark = len(self.lines)
        await self.tap((x1 + x2) // 2, y2 - dy)
        await asyncio.sleep(0.8)
        if any('CRONOPAD view=0' in l for l in self.lines[mark:]):
            return mark
    return None


async def flow(self, app, examples, grid, theme):
    THEME[0] = theme
    record, compiled, origins, bars, left = CACHE['layout']
    values = CACHE['values']
    names = {i: origins[i][0]['name'] for i in range(len(origins))}
    first = next(i for i, n in names.items() if n == 'Soggiorno')
    read_only = next((i for i, n in names.items() if n == 'Sola lettura'), None)
    say(self, f'tiles: {names}')
    await self.dark(theme == 'dark')
    await asyncio.sleep(0.8)
    await send_values(self, values)

    # 1. the card opens read-only, with the key Edit
    mark = await open_card(self, grid, compiled, first)
    if mark is None:
        check(self, False, 'the bar opens the card')
        return
    page, state, keys = await wait_pad(self, 0, mark)
    check(self, 'open' in keys, 'the card shows the key Edit')
    await shots(self, '1-read')

    # 2. the list of the user bands (4 of them: the automatic one is a gap)
    start = len(self.lines)
    await press(self, 'open')
    page, state, keys = await wait_pad(self, 1, start)
    rows = [k for k in keys if k.startswith('row')]
    say(self, f'list: {len(rows)} rows on the page, keys {sorted(keys)}')
    check(self, len(rows) >= 3 and 'add' in keys and 'prev' in keys and 'next' in keys, 'the list has rows and < > +')
    await shots(self, '2-list')
    if len(rows) < 4:   # a small glass: a second page
        await press(self, 'next')
        await shots(self, '2b-list-page2')
        await press(self, 'prev')
        check(self, True, 'pages with < >')

    # 3. + : a new band in the gap (08:30-09:30 like its neighbour), then end +1h and the temperature + one step
    start = len(self.lines)
    await press(self, 'add')
    page, state, keys = await wait_pad(self, 2, start)
    check(self, 'save' in keys and 'del' in keys and 'e+60' in keys, 'the detail of the new band opens')
    await shots(self, '3-detail')
    await press(self, 'e+60')
    await press(self, 't+')
    await shots(self, '4-edited')

    # 4. Save: one set_day with the day and the revision seen
    sent = await press(self, 'save', 0.9)
    ok = bool(sent) and len(sent) == 1
    check(self, ok, 'Save sends one action')
    if ok:
        service, f = action_fields(sent[0])
        check(self, service == 'cronotermostato.set_day', f'the action is set_day ({service})')
        check(self, f.get('climate_entity_id') == 'climate.soggiorno', f'climate_entity_id ({f.get("climate_entity_id")})')
        check(self, f.get('day_kind') == 'mon', f'day_kind is the one seen ({f.get("day_kind")})')
        check(self, f.get('base_revision') == 'r1', f'base_revision is the one seen ({f.get("base_revision")})')
        want = '08:30-10:30 heat 21.5'
        check(self, want in f.get('bands', ''), f'bands has the new band "{want}": {f.get("bands")}')
        check(self, f.get('bands', '').startswith('00:00-06:00 heat 17'), 'bands keeps the first band')
        check(self, set(f) <= {'climate_entity_id', 'day_kind', 'bands', 'base_revision'}, f'no other field ({sorted(f)})')
    await shots(self, '5-saving')
    # a second tap while it waits does not send again
    again = await press(self, 'save', 0.4)
    check(self, not again, 'a second tap on Save while it waits sends nothing')

    # 5. Home Assistant brings the new bands: Saved, back to the list with them
    new_attrs_bands = user_bands_of(room_attrs(values, first)) + [dict(s=510, e=630, mode=1, temp=21.5)]
    start = len(self.lines)
    await send_values(self, with_bands(values, first, new_attrs_bands, 'r2'))
    page, state, keys = await wait_pad(self, 1, start)
    check(self, page is not None, 'after the answer the card shows the list again')
    await shots(self, '6-saved')

    # 6. Delete: the band just added (row 2 of page 0): delete and save at once
    await asyncio.sleep(0.8)
    start = len(self.lines)
    await press(self, 'row2')
    page, state, keys = await wait_pad(self, 2, start)
    check(self, 'del' in keys, 'a tap on a row opens the band')
    sent = await press(self, 'del', 0.9)
    ok = bool(sent) and len(sent) == 1
    check(self, ok, 'Delete sends one action')
    if ok:
        _, f = action_fields(sent[0])
        check(self, '08:30-10:30' not in f.get('bands', '') and f.get('base_revision') == 'r2', f'the band is gone and base_revision is r2: {f.get("bands")}')
    await shots(self, '7-deleting')
    base = user_bands_of(room_attrs(values, first))
    await send_values(self, with_bands(values, first, base, 'r3'))

    # 7. a refusal: the integration says conflict
    start = len(self.lines)
    await press(self, 'row0')
    await wait_pad(self, 2, start)
    await press(self, 't+')
    await press(self, 'save', 0.9)
    await send_values(self, with_bands(values, first, base, 'r3', error='conflict', seq=1))
    await shots(self, '8-error')
    state_after = pad_now(self)[2]
    check(self, state_after == 3, f'the refusal puts the save in ERROR (state {state_after})')

    # 8. a change made elsewhere while the band is open: back to the list with the news
    await press(self, 'back')   # leave the detail (the copy is dropped)
    await asyncio.sleep(0.5)
    start = len(self.lines)
    await press(self, 'row1')
    await wait_pad(self, 2, start)
    await press(self, 's+15')
    elsewhere = [dict(b) for b in base]
    elsewhere[1]['e'] = elsewhere[1]['e'] - 30
    start = len(self.lines)
    await send_values(self, with_bands(values, first, elsewhere, 'r9'))
    page, state, keys = await wait_pad(self, 1, start)
    check(self, page is not None, 'a change elsewhere takes the card back to the list')
    await shots(self, '9-elsewhere')

    # 9. no answer: after 5 s the key says Not saved
    await asyncio.sleep(0.6)
    start = len(self.lines)
    await press(self, 'row0')
    await wait_pad(self, 2, start)
    await press(self, 't+')
    await press(self, 'save', 0.6)
    await asyncio.sleep(6.5)
    await shots(self, '10-timeout')
    check(self, pad_now(self)[2] == 4, f'without an answer the save times out (state {pad_now(self)[2]})')

    # 10. the room cannot be commanded any more while the list is open: the card goes back to the read-only list
    await press(self, 'back')
    await asyncio.sleep(0.4)
    start = len(self.lines)
    await send_values(self, with_bands(values, first, elsewhere, 'r9', status='paused'))
    page, state, keys = await wait_pad(self, 0, start)
    check(self, page is not None and 'open' not in keys, 'a paused room: back to the read-only card, no key Edit')
    await shots(self, '11-paused')
    await send_values(self, values)
    await self.call('render_close_cards')
    await asyncio.sleep(0.5)

    # 11. a tile with the option edit off: the card never offers Edit
    if read_only is None:
        say(self, 'no tile "Sola lettura" in preview.yaml: the option edit off is not tried')
    else:
        mark = await open_card(self, grid, compiled, read_only)
        if mark is None:
            check(self, False, 'the bar of the tile with edit off opens the card')
        else:
            await asyncio.sleep(1.0)
            lines = [l for l in self.lines[mark:] if 'CRONOPAD' in l]
            check(self, bool(lines) and all(' open:' not in l and 'view=0' in l for l in lines),
                  'a tile with edit off: the card stays read-only (no key Edit)')
            await shots(self, '12-readonly')
    await self.call('render_close_cards')
    await self.dark(False)
    (self.out / 'flow.log').write_text('\n'.join(LOG) + '\n')


_layout_of, _messages = bench.layout_of, bench.messages


def layout_of(*a, **k):
    r = _layout_of(*a, **k)
    CACHE['layout'] = r
    return r


async def messages(*a, **k):
    r = await _messages(*a, **k)
    CACHE['values'] = r
    return r


bench.layout_of, bench.messages = layout_of, messages
_view = bench.Bench.view


async def view(self, app, examples, grid, own):
    await _view(self, app, examples, grid, own)
    if own:
        for theme in ('light', 'dark'):
            try:
                await flow(self, app, examples, grid, theme)
            except Exception:
                import traceback
                check(self, False, f'the {theme} flow ran to its end: ' + traceback.format_exc()[-900:])
            await self.call('render_close_cards')


bench.Bench.view = view

if __name__ == '__main__':
    sys.argv = ['bench.py'] + sys.argv[1:]
    code = bench.main()
    if FAILS:
        print('FLOW failed checks:', *FAILS, sep='\n  ')
        code = code or 1
    sys.exit(code)
