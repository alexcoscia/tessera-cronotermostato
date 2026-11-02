# Spike del Passo 0: cosa permette l'API 0.24 al tile `cronotermostato` (2026-11-02)

Prove fatte sul banco (`tools/preview.py` di `~/tessera-plugins`) con un tile **provvisorio** 2x2 (`components/cronotermostato/`,
`preview.yaml`, `preview-probes.yaml`): non è il tile finale, resta come base. Manifest `api: "0.24"`, `check.py` verde.

Legenda: **VISTO** = osservato sul banco (immagini in `.preview/`, log del firmware in `.preview/<schermo>/log.txt`, `summary.json`);
**LETTO** = ricavato dai documenti o dal codice di Tessera (`tools/.cache/tessera-main`, `screen_manager/app/plugins.py`,
`components/smart_display/plugin_host.cpp`), non provato.
Schermi del banco: CYD 2.8" (320x240, griglia 2x3), Guition 4" (480x480, 2x3), Waveshare 4.3" in verticale (480x800, 1x4),
Guition 10.1" (1280x800, 5x5), e in più Waveshare 7B (1024x600, 4x4, quello di Alex).

## Riepilogo

| Prova | Esito | Cosa cambia nel piano |
|---|---|---|
| (a) opzione `entity` + liste di numeri | **ok, con limiti** | Gli oggetti `slot`/`next_slot` NON arrivano (solo testi, numeri, booleani, liste di testi/numeri). Le stringhe ISO non diventano epoch. |
| (b) barra di `ui::block` + tacca "adesso" | **ok** | Ambra = `Tone::BUSY`, blu = `Tone::ACCENT`, grigio = `Tone::NORMAL` + `theme::SUBTLE`. 16 segmenti visti. Il testo ambra in chiaro dà il warning "too faint" (anche in `thermo_dial`). |
| (c) `tessera::action` verso un'altra integrazione | **ok, con una correzione** | Passare `entity = ""` e mettere `climate_entity_id` nei campi: i servizi dell'integrazione non accettano `entity_id`. Il ripiego `climate.set_temperature` non serve. |
| (d) card dal tocco | **ok** | La card riceve gli stessi dati del tile (anche `entities.room`). 10 righe sul CYD, 16+ sugli altri. |
| (e) schermi piccoli | **ok, con limiti** | Il 2x2 sul CYD è 302x110 px e ci sta tutto (nome escluso). Su schermi in verticale a 1 colonna il tile 2x2 non si può mettere. |

## (a) Opzione `kind: entity` e liste di numeri in `on_state`

**Codice minimo** (`tessera-plugin.yaml`):

```yaml
tiles:
  - id: room
    domains: [climate]
    attributes: [temperature, current_temperature, min_temp, max_temp, target_temp_step, hvac_action]
    options:
      - id: room
        kind: entity
        domains: [sensor]
        attributes: [today_start, today_end, today_mode, today_temp, today_auto, slot, next_slot, slot_end, manual, override_until, status]
        label: room
```

```cpp
JsonObjectConst r = data["entities"]["room"]["attributes"];
JsonArrayConst s = r["today_start"], e = r["today_end"], m = r["today_mode"], t = r["today_temp"];
band.start = s[i] | 0;  band.temp = t[i] | 0.0f;
```

**Documento** (`docs/MANIFEST.md`, "Tiles"): «An option of kind `entity` (0.15) keeps the id of the entity chosen... The app sends the
chosen one with the tile's data under `entities` and the option's id, with its state, its name, the named attributes and `updated`».
`docs/FIRMWARE_API.md`: «They share what the tile's own entity and data leave of the 2.6 KB». Per le liste: «A list comes whole when
it fits: texts and numbers, not objects» (MANIFEST, `attributes`).

**VISTO sul banco** (riga del log, tile Soggiorno, 541 byte):

```
{"state":"heat","name":"climate.soggiorno","attributes":{"temperature":21.5,...,"hvac_action":"heating"},
 "entities":{"room":{"state":"Settimana","name":"sensor.soggiorno_programma","attributes":{"today_start":[0,390,510,1020,1380],
 "today_end":[390,510,1020,1380,1440],"today_mode":[1,1,1,1,0],"today_temp":[17,21,18,21.5,0],"today_auto":[0,0,0,0,0],
 "slot_end":"2026-11-02T18:00:00+01:00","manual":true,"override_until":"2026-11-02T18:00:00+01:00","status":"manual"}}}}
```

- Le liste arrivano come array JSON di numeri (interi e decimali: `21.5` resta decimale); `manual` come booleano; `slot_end` e
  `override_until` come **testo** ISO di 25 caratteri (tagliato a 48 byte).
- Chiavi viste in `entities.room.attributes` (log `room attribute keys:`): `today_start, today_end, today_mode, today_temp, today_auto,
  slot_end, manual, override_until, status`. **`slot` e `next_slot` mancano** (nell'esempio "Gigante" avevano un oggetto `{start,end,mode,temp}`).
  Il motivo è in `plugins.py`, `entity_part`: un valore che non è testo, numero, booleano o lista di testi/numeri non viene copiato.
  Nel sensore reale (`sensor.py`) `slot` e `next_slot` sono dizionari: arriverebbero assenti.
- Le stringhe ISO **non** sono convertite in epoch: `entity_part` converte solo gli attributi il cui nome finisce per `_at`, `_time`
  o `date`. `slot_end` e `override_until` non rientrano. Nel tile provvisorio ho preso "HH:MM" dai caratteri 11-15 del testo
  (visto: "until 18:00" nella pillola).
- La chiave `updated` (documentata) **non si vede** sul banco: la forma osservata ha solo `state`, `name`, `attributes`.
  `name` è l'id dell'entità quando nello stato finto non c'è `friendly_name`.

**Limite di byte** (VISTO, stesso log, `on_state N bytes`; limite 2600, `X_BUDGET` in `plugins.py`, riga 70):

| Fasce nel giorno | Byte del messaggio | Esito |
|---|---|---|
| 3 | 433 | intero |
| 5 | 541 | intero |
| 16 | 618 | intero |
| 120 | 2362 | intero, 5 liste da 120 |
| 300 | 1878 | **accorciato**: le liste arrivano tutte con 96 elementi (`size=96`) |

Regola LETTA in `plugins.py` (`LIST_STEPS = (None, 192, 96, 48, ...)`): se non ci stanno, tutte le liste si accorciano insieme dalla
fine al primo passo che sta nei 2,6 KB; se anche con 0 non ci sta, il tile riceve `{"wait": "too_large"}` (non visto). Un giorno
vero ha poche fasce (3-10): circa 40-50 byte per fascia con 5 liste, quindi ampio margine.

**Cosa cambia nel piano**
- Le liste parallele `today_*` vanno bene, tutte con la stessa lunghezza. Il piano le prevede già.
- Gli attributi `slot` e `next_slot` (dizionari) non servono al tile: la fascia attuale si ricava da `today_*` e dall'ora. Se servono
  `slot`/`next_slot`, l'integrazione 0.2.0 deve esporli piatti (per esempio `slot_temp`, `slot_mode`, `next_start`, ...).
- Per mostrare "fino alle HH:MM" in modo coerente con l'orologio dello schermo (24 ore o AM/PM) conviene che l'integrazione 0.2.0
  esponga anche `slot_end_time` e `override_until_time` (nome che finisce per `_time`): arrivano come epoch e si scrivono con
  `tessera::clock_text(epoch)`. In alternativa si tagliano le ore dal testo ISO (funziona, ma ignora il formato dello schermo).
- Non basarsi su `updated`.

## (b) Barra 0-24 con `ui::block` e `ui::set_fill`, tacca "adesso"

**Codice minimo** (tile provvisorio):

```cpp
track_ = ui::block(c.parent, theme::TRACK);              // in create(): il fondo, i segmenti, la tacca
for (int i = 0; i < MAX_SEGMENTS; ++i) { seg_[i] = ui::block(c.parent, theme::SUBTLE); lv_obj_add_flag(seg_[i], LV_OBJ_FLAG_HIDDEN); }
tick_ = ui::block(c.parent, theme::INK);
// in on_tick():
lv_obj_set_pos(seg_[i], start * w / 1440, bar_y);  lv_obj_set_size(seg_[i], std::max(1, (end - start) * w / 1440), bar_h);
ui::set_fill(seg_[i], mode == 1 ? Tone::BUSY : mode == 2 ? Tone::ACCENT : Tone::NORMAL, theme::SUBTLE);
const auto lt = tessera::local_time(epoch);  lv_obj_set_pos(tick_, (lt.hour * 60 + lt.minute) * w / 1440, ...);
```

**Documento** (`FIRMWARE_API.md`, "Drawing"): «`GOOD` (0.9) is Home Assistant's green, `BUSY` its amber, `ALERT` its red, `ACCENT` the look's
accent... a block (a bar, a dot) with `ui::set_fill(block, tone, normal)` (0.9); `normal` is the role for `NORMAL`». In `thermo_dial`
il riscaldamento è `Tone::BUSY` e il raffrescamento `Tone::ACCENT` (`mood_tone`, `thermo_dial.cpp` riga 82).

**VISTO** (`.preview/overview.png`, tile in chiaro; scuro nelle `tile-*-dark.png`):
- Ambra riscaldamento = `Tone::BUSY`; blu raffrescamento = `Tone::ACCENT` (azzurro "Home Assistant"); grigio spento = `Tone::NORMAL`
  con ruolo `theme::SUBTLE`; fondo della barra = `theme::TRACK`; tacca "adesso" = `theme::INK`, alta quanto la barra più 2 px sopra e sotto.
  Tutto leggibile su CYD, Guition 4", Guition 10.1", Waveshare 7B, chiaro e scuro.
- **16 segmenti** (esempio "Lunga", 16 fasce da 90 minuti, caldo/freddo/spento alternati): tutti disegnati e distinguibili anche sul
  CYD (302 px, circa 19 px a fascia). Oltre 16 non provato: il tile provvisorio mostra le prime 16 (esempio "Enorme": 120 fasce, la barra
  mostra solo le prime 16 stipate a sinistra). Nel tile finale vanno unite le fasce adiacenti con lo stesso modo, o va stabilito un massimo.
- Blocchi con raggio: i segmenti adiacenti mostrano un taglio (l'angolo arrotondato). Con `lv_obj_set_style_radius(seg, ui::px(2), 0)` il
  controllo `check.py` non protesta; un raggio 0 ai giunti rende la barra continua (da decidere nel Passo 4).
- Fascia di 1 minuto: `std::max(1, ...)` pixel, appare come un puntino.

**Memoria (`memory`)**: **non misurabile sul banco** ("Speed and memory on the chip... show on a screen only", `PREVIEW.md`). Stima dai
documenti (`FIRMWARE_API.md`, "Rules" 8: etichetta circa 100 byte, blocco circa 80): 20 blocchi (16 fasce + fondo + tacca + 2 tasti)
circa 1600, 6 etichette circa 600, più i membri (16 fasce, puntatori) circa 500: **circa 2700; dichiarato 3000**. Il manifest accetta da 64 a
16384. Il costo vero si vede solo su uno schermo (editor, barra della memoria del Layout).

**Colori e warning del banco**: il testo ambra (setpoint e pillola "fino alle") in chiaro dà `warning: too faint to read on its ground`
(`--strict` esce con 2). VISTO che succede **identico in `thermo_dial`** (Soggiorno 2x2/1x1/2x1 in chiaro, `faint[21.5°]`), quindi è il tono
ambra su fondo chiaro, non un errore del tile. Nel piano: o si accetta il warning come già in `thermo_dial`, o il numero resta `INK` e
l'ambra va sull'icona, sui tasti e sulla barra.

## (c) `tessera::action` verso `cronotermostato.set_manual` / `resume_auto`

**Documenti**
- `MANIFEST.md`: «`permissions.home_assistant_actions` Home Assistant actions the screen calls (`tessera::action`)... The screen does not
  check `read_entities` or `home_assistant_actions`: code built into the firmware can do what the firmware can. Those two are the maker's
  honest description».
- `FIRMWARE_API.md`, "Actions": «`entity` is the action's `entity_id`... pass `""` when the fields name their own target... `entity_id` in
  the fields beside a non-empty `entity` is refused».
- Nessun limite al dominio del servizio: `plugin_action::service_name` accetta qualsiasi `dominio.azione` di `a-z0-9_`.

**VISTO sul banco** (`preview-probes.yaml`, il banco tocca il centro del tile; `summary.json` > `taps[].actions`, log `plugin_action`):

| Prova (opzione `probe`) | Chiamata | Esito visto |
|---|---|---|
| `set_manual` | `action("cronotermostato.set_manual", "", {climate_entity_id, mode, temperature})` | **inviata**: `cronotermostato.set_manual {'climate_entity_id': 'climate.studio', 'mode': 'heat'}` (la `temperature`, numero, va nei `data_template`: il log dice "3 values") |
| `set_manual_entity` | `action(..., entity_, {mode, temperature})` | inviata, ma con `{'entity_id': 'climate.bagno', ...}` |
| `dup` | `entity_` non vuota **e** `entity_id` nei campi | **rifiutata**: `Not sent: ... entity_id is given twice`, `action()` restituisce false |
| `entity_id_field` | `entity_ = ""` e `entity_id` nei campi | inviata (`entity_id` come campo) |
| `resume` | `action("cronotermostato.resume_auto", "", {climate_entity_id})` | **inviata**: `cronotermostato.resume_auto {'climate_entity_id': 'climate.mansarda'}` |
| `fallback` | `action("climate.set_temperature", entity_, {temperature})` | inviata |
| servizio non elencato nel manifest (`cronotermostato.pause`, prova poi tolta dal codice) | `action(..., entity_, f)` | **inviata** (il firmware non controlla). Il banco avvisa solo "the code calls ..., which permissions.home_assistant_actions does not name" |

**LETTO, non provato su Home Assistant** (nessun comando reale ne' chiamata a HA): i servizi dell'integrazione hanno
`ROOM_SCHEMA = vol.Schema({vol.Required("climate_entity_id"): _CLIMATE})` e `MANUAL_SCHEMA` con `climate_entity_id`, `mode`,
`temperature`, `preset` (`services.py`, `services.yaml`). Un `vol.Schema` normale rifiuta le chiavi extra: quindi la forma con
`entity = entity_` (che manda `entity_id`) verrebbe **rifiutata da HA**. La forma giusta è la prima riga della tabella.

**Cosa cambia nel piano**
- Il Passo 5 chiama: `tessera::action("cronotermostato.set_manual", "", fields)` con `fields["climate_entity_id"] = <climate del tile>`,
  `mode`, `temperature`; e `tessera::action("cronotermostato.resume_auto", "", fields)` con solo `climate_entity_id`. L'entità del tile (il
  climate) si copia da `TileContext::entity` in `create()`.
- Con `entity = ""` il tile **non** aspetta la risposta di HA ("without one... nobody waits"): niente stato "occupato" del core; il ritorno
  al valore reale dopo 5 s (Passo 3) resta compito del tile.
- Il ripiego `climate.set_temperature` non serve. Alternativa se si volesse `entity` nel tile: far accettare `entity_id` all'integrazione.
- `permissions.home_assistant_actions: [cronotermostato.set_manual, cronotermostato.resume_auto]` resta da elencare (descrizione onesta,
  verificata dal banco).
- Il banco non vede l'esito in HA né i `data_template`: la prova reale richiede lo schermo vero e il via libera di Alex.

## (d) Card aperta dal tocco, con l'elenco delle fasce

**Codice minimo**: `add_card("day", ...)`, `cards: [{ id: day, name: card_name }]`; dal tile `tessera::open_card(plugin_id(), "day", entity_, tile_)`;
la card legge `data["entities"]["room"]["attributes"]["today_*"]` in `on_state` e scrive una riga di `ui::label` per fascia.

**Documento** (`FIRMWARE_API.md`, "A card"): «`on_state` gets a plugin tile's data»; «A tap on the card (0.12) `on_tap()`... `tessera::tap_point()`»;
nel tile «`tessera::tap_point()` (0.10) says where the finger was, in the tile's own pixels».

**VISTO** (`tap-2-camera-2x2.png`, log `card on_state`):
- La card si apre, con il titolo del tile ("Camera") e il tasto indietro del core.
- `card on_state` riceve **lo stesso messaggio del tile**, con `entities.room.attributes.today_*`: nessun dato in più da portare.
- Righe disponibili (altezza della card / riga `BODY` + 4 px): **CYD 300x176 = 10 righe** (riga 17 px); **Guition 4" 440x372 = 16 righe**
  (riga 23 px, ho limitato io a 16); **Guition 10.1" 615x704 = 16 righe** (riga 20 px, limitato io; ne starebbero circa 35). Con 3-10 fasce di un
  giorno vero ci stanno anche sul CYD.
- Una card aperta "per id" dal banco senza tile è vuota (non ha opzioni): normale, la prova vera è il tocco sul tile.

**NON visto**: `tessera::tap_point()` sulla barra (il banco tocca solo il centro del tile). Per distinguere "tocco sulla barra" da "tocco sui
tasti" il Passo 5 usa `tap_point()` (documentato) e va provato su schermo; `thermo_dial` fa già così con i suoi tasti.

**Cosa cambia nel piano**: fattibile come previsto. Nessun `tap_actions` serve. Se la lista supera le righe, tagliare dall'inizio o dalla fine in
`on_state` (la card non scorre).

## (e) Schermi piccoli: 2x2 con `+`, valori al centro, `-`, barra sotto

**VISTO** (misure da `summary.json`, immagini in `.preview/overview.png`):

| Schermo | Griglia | Tile 2x2 (px) | Esito |
|---|---|---|---|
| CYD 2.8" lying (320x240, 143 dpi, look compatto) | 2x3 | 302x110 | tutto: `+` a sinistra, setpoint e "Now x" al centro, `-` a destra, pillola e barra sotto; **il nome non ci sta** (nascosto) |
| Guition 4" (480x480, 170 dpi) | 2x3 | 448x248 | tutto, con il nome |
| Waveshare 7B (1024x600) | 4x4 | 490x244 | tutto, con il nome |
| Guition 10.1" (1280x800) | 5x5 | 494x277 (3x2: 746x277) | tutto, con il nome |
| Waveshare 4.3" **in verticale** (480x800) | **1x4** | nessuno | **il 2x2 non si può mettere**: `warning: no tile of the plugin fits a 1 x 4 grid` |

- Il 2x2 sul CYD è largo e basso (302x110): la barra occupa tutta la larghezza e resta leggibile; i tasti sono circa 11 mm.
- **LETTO** (`boards.json`): le schede in verticale con 1 sola colonna sono CYD, Hosyond 4", Waveshare 3.5"/4.3"/5", Tab5, jc3248, jc4880, jc8048,
  Freenove, WT32. Per loro il tile 2x2 non esiste (scelta di Alex: niente tile più piccolo). Quelle in orizzontale hanno almeno 2 colonne. La
  Waveshare 3.5" in orizzontale è 2x2: il tile riempie lo schermo intero.
- Il numero del setpoint usa `Font::VALUE`; non provato `Font::DIGITS` (solo cifre e `°`, `-`, `,`, `.`).

**Cosa cambia nel piano**
- Domanda 4 del piano (quali schermi): CYD in orizzontale, schermi da 4" e più in orizzontale vanno; i verticali a una colonna no, per scelta del 2x2.
  Chiedere ad Alex solo se vuole anche un tile 1 colonna per i verticali.
- Il nome stanza sparisce sotto una certa altezza (regola del tile provvisorio: `h < nome + valore + pillola + barra + 4 spazi + 9 mm`); nel tile finale la
  stessa regola, e il nome è già nel titolo della card.

## Altre cose viste che il piano deve sapere

- **Stato non disponibile**: il tile provvisorio mostra `-` ma lascia pillola e barra del programma (esempio "Offline"): nel tile finale vanno nascoste
  e i tasti resi inerti (come nel piano, Passo 5).
- **Il banco costruisce** il tile `api: "0.24"` (usa Tessera `dev` da solo); la prima build è stata di 28 s perché la cache c'era già.
- **`--strict` non uscirà pulito** finché il setpoint è ambra in chiaro (vedi (b)).
- **Peso del firmware**: `flash_kb: 6` dichiarato come in `thermo_dial`; non misurato.

## Cosa non è stato verificato

- Nessuna prova su schermo vero né su Home Assistant (nessun termostato comandato, HA non toccato): il comportamento reale dei servizi con
  `climate_entity_id`, il formato di `temperature` nei `data_template`, la memoria sul chip e il costo reale di `memory`, `tap_point()` sulla barra, la
  chiave `updated`, lo stato `{"wait": "too_large"}`, più di 16 segmenti, `Font::DIGITS`.
- Il firmware di Alex (Tessera 0.4.93) non è stato confrontato con la cache del banco (`tessera-main` del 18 ottobre, e `dev` per l'API 0.24).

## Come rifare le prove

```sh
python3 ~/tessera-plugins/tools/preview.py . --board cyd            # tile e card, preview.yaml
cp preview-probes.yaml preview.yaml                                  # (poi ripristinare con git) le prove (c) con i tocchi
python3 ~/tessera-plugins/tools/preview.py . --board cyd --out /tmp/spike-probes
```
