# Development

The logic that needs no screen (bands to pixels, texts, the + and - timing, what the pill says) is in
`components/cronotermostato/cronotermostato_logic.h`, plain C++17. Its tests build and run on a computer, with the
address and undefined-behaviour sanitizers:

```sh
tests/run.sh
```

`tests/parity.inc` holds bar positions computed by the editor's own `stripSegments` (integration repository,
`tests/frontend/cases.json`); the bar must match them within a pixel. `tests/overlap.inc` holds a day with overlapping
bands: the editor draws them over each other, the tile trims the later one (a deliberate difference). `tests/tints.inc`,
`labels.inc` and `siglas.inc` hold what the editor's `bandTints`, `segmentDetail` and `presetSigla` say (the tile matches
them; one known difference: it does not change the case of letters outside ASCII). Regenerate all with
`node tools/gen_parity.mjs [path of the integration repository]` (default `../cronotermostato`, at commit 4e529dc or later).

The touch flow of the bench (`tools/bench_flow.py`) opens the card of the first room tile with real taps, enters the
editing, adds a band, saves, answers the way the integration does (new bands, a refusal, a change made elsewhere), deletes
(Delete -> Cancel sends nothing, an early Confirm sends nothing, Confirm sends; a change made elsewhere or a pause while the
question is open closes it without sending), waits for the timeout and opens a tile whose option `edit` is off. It checks the `cronotermostato.set_day` call (day,
revision, text of the bands) and photographs every step, light and dark, into `.preview-flow/<board>/`. It needs the bench
of `tessera-plugins` (set up once with `preview.py --setup`) and its Python:

```sh
tools/bench_flow.sh cyd            # or guition, waveshare7b, jc8012p4a1: pictures in .preview-flow/<board>/<board>/flow-*.png
TESSERA_PLUGINS=/path/to/tessera-plugins tools/bench_flow.sh cyd   # when it is not beside this repository
```

It prints `FLOW <board> PASS|FAIL <check>` lines and ends with status 1 if a check fails. The card logs one line
`CRONOPAD view=... name:x,y,w,h ...` when what it shows changes: the script reads the keys from it. The geometry and the
zones of the editing are in `components/cronotermostato/cronotermostato_pad.h`, tested by `tests/test_pad.cpp`.

The notes of the first spike on the plugin API are in `SPIKE.md` (history). The checklist for trying the plugin on a
real screen is in `PROVA_SU_SCHERMO.md` (Italian, for the author).
