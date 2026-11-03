// Regenerates tests/parity.inc, tests/overlap.inc, tests/tints.inc, tests/labels.inc, tests/siglas.inc and tests/palette.inc from the editor's own functions
// (integration repository): `stripSegments`, `bandTints`, `segmentDetail` and `presetSigla` of lib/day-editing.js.
//   node tools/gen_parity.mjs [path of the cronotermostato integration repository]
// The default path is ../cronotermostato. parity.inc: the valid days of tests/frontend/cases.json plus two days with
// boundaries on half pixels; overlap.inc: a day with overlapping bands (the editor draws them over each other, the tile
// trims the later one on purpose). Each entry is {name, {{startMin, endMin, left %, width %}, ...}}.
import fs from "fs";
import path from "path";
import { fileURLToPath, pathToFileURL } from "url";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(process.argv[2] ?? path.join(here, "..", "..", "cronotermostato"));
const { stripSegments, bandTints, segmentDetail, presetSigla } = await import(pathToFileURL(path.join(repo, "custom_components/cronotermostato/frontend/lib/day-editing.js")).href);
const cases = JSON.parse(fs.readFileSync(path.join(repo, "tests/frontend/cases.json"), "utf8"));

const band = (start, end) => ({ start, end, mode: "heat", temperature: 20 });
const entry = (name, day) => {
  const segs = stripSegments(day);
  if (!segs.length) return null;
  const items = segs.map((s) => `{${s.startMin},${s.endMin},${s.left.toPrecision(17)},${s.width.toPrecision(17)}}`);
  return `  {"${name.replace(/"/g, "'")}", {${items.join(", ")}}},`;
};
const lines = (list) => list.filter(Boolean).join("\n") + "\n";

const valid = cases.filter((c) => c.expect === "valid" && c.day.length).map((c) => entry(c.name, c.day));
const half = [
  entry("half pixel: two halves", [band("00:00", "12:00"), band("12:00", "24:00")]),
  entry("half pixel: quarters", [band("00:00", "06:00"), band("06:00", "18:00"), band("18:00", "24:00")]),
];
const overlap = [entry("overlap: second starts inside the first", [band("00:00", "10:00"), band("06:40", "24:00")])];

fs.writeFileSync(path.join(here, "..", "tests", "parity.inc"), lines([...valid, ...half]));
fs.writeFileSync(path.join(here, "..", "tests", "overlap.inc"), lines(overlap));
console.log(`parity.inc: ${valid.length + half.length} cases, overlap.inc: ${overlap.length}`);

// tints.inc: {name, {{mode (0 off, 1 heat, 2 cool), has temperature, temperature, preset, tint}, ...}} for the valid days of
// cases.json with at most 12 bands and a few days that go round the 4 tints; the editor's own bandTints says each tint.
const MODE = { off: 0, heat: 1, cool: 2 };
const bandsOf = (day) => day.map((b) => ({ mode: b.mode, temperature: b.temperature ?? null, preset: b.preset ?? null }));
const tintEntry = (name, day) => {
  const tints = bandTints(bandsOf(day));
  const items = day.map((b, i) => {
    const has = b.temperature !== null && b.temperature !== undefined && Number.isFinite(Number(b.temperature));
    return `{${MODE[b.mode] ?? 0}, ${has ? "true" : "false"}, ${has ? Number(b.temperature) : 0}, ${JSON.stringify(b.preset ?? "")}, ${tints[i]}}`;
  });
  return `  {"${name.replace(/"/g, "'")}", {${items.join(", ")}}},`;
};
const mk = (mode, temperature, preset) => ({ mode, ...(temperature === undefined ? {} : { temperature }), ...(preset === undefined ? {} : { preset }) });
const extra = [
  ["Casa on a Tuesday: away, home, away", [mk("heat", undefined, "away"), mk("heat", undefined, "home"), mk("heat", undefined, "away")]],
  ["five temperatures go round four tints", [17, 18, 19, 20, 21, 17, 21].map((t) => mk("heat", t))],
  ["heat and cool count apart", [mk("heat", 20), mk("cool", 24), mk("heat", 21), mk("cool", 24), mk("cool", 25), mk("off")]],
  ["temperature wins over preset", [mk("heat", 20, "eco"), mk("heat", 20, "comfort"), mk("heat", 21, "eco"), mk("heat", undefined, "eco")]],
  ["same preset, other temperature", [mk("heat", 20, "eco"), mk("heat", 22, "eco"), mk("heat", undefined, "eco")]],
  ["temperature 20 and 20.0 are one value", [mk("heat", 20), mk("heat", 20.0), mk("heat", 20.5)]],
  ["preset with blanks is the preset", [mk("heat", undefined, " eco "), mk("heat", undefined, "eco"), mk("heat", undefined, "   ")]],
  ["bands without value are tint 0", [mk("off"), mk("heat", undefined, ""), mk("heat", 20), mk("off")]],
];
const tintDays = [
  ...cases.filter((c) => c.expect === "valid" && c.day.length && c.day.length <= 12).map((c) => tintEntry(c.name, c.day)),
  ...extra.map(([n, d]) => tintEntry(n, d)),
];
fs.writeFileSync(path.join(here, "..", "tests", "tints.inc"), lines(tintDays));

// labels.inc: {width in px, level (0 none, 1 short, 2 value or more)} from segmentDetail, and {preset, sigla} from presetSigla
// (ASCII only: the tile does not change the case of other alphabets).
const levels = [0, 1, 11, 11.9, 12, 13, 33, 33.9, 34, 35, 79, 80, 129, 130, 400, NaN];
const levelLines = levels.map((w) => `  {${Number.isNaN(w) ? "-1" : Math.floor(w)}, ${w !== w ? 0 : ["none", "short"].indexOf(segmentDetail(w)) >= 0 ? ["none", "short"].indexOf(segmentDetail(w)) : 2}},`);
const presets = ["away", "home", "eco", "a", "", "   ", "_x1", "--", "comfort", "12ab", "night-mode", "Z", "a b", "x"];
const siglaLines = presets.map((p) => `  {${JSON.stringify(p)}, ${JSON.stringify(presetSigla(p))}},`);
fs.writeFileSync(path.join(here, "..", "tests", "labels.inc"), lines(levelLines));
fs.writeFileSync(path.join(here, "..", "tests", "siglas.inc"), lines(siglaLines));
console.log(`tints.inc: ${tintDays.length} days, labels.inc: ${levelLines.length} widths, siglas.inc: ${siglaLines.length} siglas`);

// palette.inc: the colours of the editor (styles.js: --crono-<mode>-v<n>-bg and --crono-<mode>-v<n>, v0 is --crono-<mode>-bg and
// --crono-<mode>) as {look (0 light, 1 dark), mode (0 heat, 1 cool, 2 off), tint, fill, text}. The light look is the block
// ":host {", the dark one ":host([dark]) {". The test checks BAND_PALETTE of the tile against them.
const styles = fs.readFileSync(path.join(repo, "custom_components/cronotermostato/frontend/styles.js"), "utf8");
const darkAt = styles.indexOf(":host([dark]) {");
const blocks = [styles.slice(styles.indexOf(":host {"), darkAt), styles.slice(darkAt, styles.indexOf("}", darkAt))];
const paletteLines = [];
blocks.forEach((block, look) => {
  ["heat", "cool", "off"].forEach((mode, m) => {
    for (let t = 0; t < 4; t++) {
      const bg = block.match(new RegExp(`--crono-${mode}${t ? `-v${t}` : ""}-bg:\\s*#([0-9a-fA-F]{6})`));
      const fg = block.match(new RegExp(`--crono-${mode}${t ? `-v${t}` : ""}:\\s*#([0-9a-fA-F]{6})`));
      if (!bg || !fg) throw new Error(`styles.js: colour ${mode} v${t} missing in look ${look}`);
      paletteLines.push(`  {${look}, ${m}, ${t}, 0x${bg[1].toUpperCase()}, 0x${fg[1].toUpperCase()}},`);
    }
  });
});
fs.writeFileSync(path.join(here, "..", "tests", "palette.inc"), lines(paletteLines));
console.log(`palette.inc: ${paletteLines.length} colours`);
