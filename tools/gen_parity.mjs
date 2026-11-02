// Regenerates tests/parity.inc and tests/overlap.inc from the editor's own `stripSegments` (integration repository).
//   node tools/gen_parity.mjs [path of the cronotermostato integration repository]
// The default path is ../cronotermostato. parity.inc: the valid days of tests/frontend/cases.json plus two days with
// boundaries on half pixels; overlap.inc: a day with overlapping bands (the editor draws them over each other, the tile
// trims the later one on purpose). Each entry is {name, {{startMin, endMin, left %, width %}, ...}}.
import fs from "fs";
import path from "path";
import { fileURLToPath, pathToFileURL } from "url";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(process.argv[2] ?? path.join(here, "..", "..", "cronotermostato"));
const { stripSegments } = await import(pathToFileURL(path.join(repo, "custom_components/cronotermostato/frontend/lib/day-editing.js")).href);
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
