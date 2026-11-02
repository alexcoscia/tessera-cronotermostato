# Spike del Passo B (piano 4): modifica delle fasce dal display (2026-11-02)

Prove fatte sul banco (`~/tessera-plugins/tools`, firmware Tessera `main`, API 0.24) su una COPIA del plugin
(`.../scratchpad/spike-edit/`, nulla toccato in `~/tessera-cronotermostato`). Schermi: CYD 2.8" (320x240), Hosyond 4" (480x320),
Guition 4" (480x480), Waveshare 7B (1024x600), Guition 10.1" (1280x800).

Legenda: **VISTO** = osservato sul banco (log `EDITSPIKE ...`, immagini `out-<scheda>/<scheda>/flow-*.png`); **LETTO** = ricavato da
documenti o codice del core (`~/tessera-plugins/docs/*.md`, `~/espscreen/components/smart_display/plugin_api.h`, `plugin_action.h`,
`plugin_host.cpp`), non provato.

Il codice di prova (da buttare): `spike-edit/components/cronotermostato/edit_spike.h` (modello, `Pad` di tasti con hit-test) e
`edit_spike_ui.h` (`EditCard`, `EditPopup`, `Editor`); il banco con tocchi multipli: `scratchpad/harness/flow.py` + `run.sh`.

## Riepilogo

| # | Domanda | Esito |
|---|---|---|
| 1 | Tasti interattivi in card o popup | **ok, ma senza `on_tap` per tasto**: un solo `on_tap` per card/popup + `tessera::tap_point()`; i tasti si disegnano e si "colpiscono" a mano. Una card apre un popup. **Consiglio: card con pannello interno (non popup).** |
| 2 | Righe >= 7 mm | CYD 3 (2 con intestazione), Hosyond 4, Guition 6, 7B 8, 10.1" 14. **Nessuno scorrimento nell'API**: si pagina con `<` `>`. |
| 3 | Testo lungo in `tessera::action` | **ok fino a 1024 byte in tutto** (chiavi + valori); 960 caratteri di fasce passano, 1000 no. |
| 4 | Esito/feedback | Solo lo stato del sensore (o `tessera::send` con risposta, LETTO). La card riceve `on_state` mentre è aperta (**VISTO**), anche con un popup sopra; il popup no. |
| 5 | Tocchi nel banco | `taps:` in `preview.yaml` **non esiste**. Ho pilotato un flusso a più tocchi con uno script Python sul banco (ok). |
| 6 | Ora/numero nativo | **Non c'è.** `+`/`−` confermati (con i limiti sotto). |

## 1. Tasti in card o popup

**Documento** (`FIRMWARE_API.md`, "A card"): «`on_tap()` hears a short tap on the card's room ... `tessera::tap_point()` says where ... so a
list knows its row ... The objects `tessera::ui` makes take no taps, so a tap on a label or a block reaches the card». Regola 3 degli
esempi: niente `LV_EVENT_CLICKED`/`PRESSED` (check.py li rifiuta). `Popup::on_tap` è uguale ("A popup", `plugin_api.h`).
`Plugin::on_press()` esiste ma è un momento per un suono, non per un tasto.

**Codice minimo** (VISTO, funziona su tutte e 5 le schede, anche con tocchi veri attraverso il filtro del core):

```cpp
// create/open: ogni tasto = ui::block + label figlia (se la label è sorella del blocco il banco dice "over another part")
k.bg = ui::block(parent, theme::RAISED);  lv_obj_set_size(k.bg, w, h);  lv_obj_set_pos(k.bg, x, y);
k.lb = ui::label(k.bg, Font::BODY, theme::INK);   // figlia del blocco
// on_tap della card o del popup
const tessera::Point at = tessera::tap_point();            // pixel della stanza (c.parent)
for (const Key &k : keys) if (k.page == page && at.x >= k.x && at.x < k.x + k.w && at.y >= k.y && at.y < k.y + k.h) return act(k.id);
```

**Una card può aprire un popup**: VISTO (`tessera::show_popup(plugin, make, PopupWidth::WIDE)` dall'`on_tap` della card; il popup compare
sopra, la card resta viva sotto).

**Card o popup?** Card, per questi motivi:
- Popup: la stanza è alta quanto lo schermo permette (`c.height`) e **la prima riga deve tenersi `c.corner` dal bordo destro** (tasto di
  chiusura): serve una riga d'intestazione in più. VISTO sul CYD: stanza 258x178, pannello con intestazione = 212 px, **la riga Elimina/Salva/Chiudi
  è tagliata** (`flow-6-popup.png`). Sulle altre schede ci sta (Hosyond 418x258 serve 217; Guition 390x390 serve 264; 7B 934x434; 10.1" 1202x440).
  Un popup non scorre mai (docs: «It never scrolls»).
- Il popup **non riceve `on_state`** (solo `open`, `on_interval`, `on_tick`, `on_theme`, `on_tap`, `on_dismiss`): dovrebbe leggere un modello
  condiviso aggiornato dalla card sotto (funziona, VISTO, ma accoppia i due oggetti e il puntatore va azzerato in `on_dismiss`).
  Si chiude da solo con tocco accanto, standby, avviso, altro popup (LETTO: non l'ho provato).
- La card ha `on_back()` (VISTO: `true` = resto aperto, torno dal dettaglio all'elenco) e il titolo con il nome del tile.
- Il popup lascia vuoto il tasto-fondo (`RAISED` sullo stesso colore della card: ho dovuto usare `TRACK`).
- Il popup serve solo se si vuole la modale larga su schermi grandi: non vale la complicazione.

Misure della **card** (stanza sotto la barra, `wide=false`), tasto alto `ui::mm(7)`:

| Scheda | Stanza card | Tasto 7 mm | Pannello a 4 righe (Inizio, Fine, Modo+Temp, Elimina/Salva/Indietro) |
|---|---|---|---|
| CYD 2.8" | 300x176 | 40 px | 169 px: ci sta per 7 px (VISTO, `flow-2-detail.png`) |
| Hosyond 4" | 460x255 | 41 | 173 |
| Guition 4" | 440x372 | 48 | 201 |
| Waveshare 7B | 700x492 | 48 | 201 |
| Guition 10.1" | 615x704 | 42 | 177 |

Quindi il pannello **non può avere una riga d'intestazione/stato sul CYD**: lo stato va nell'etichetta del tasto Salva ("...", "Salvato", "Errore")
o nel titolo. Il nome del programma e "usato da N stanze" vanno nel **titolo** della card (`tessera::open_card(..., title)` lo permette,
LETTO) o nell'elenco (non nel pannello).

## 2. Righe tappabili

VISTO (log `EDITSPIKE geometry`), con una riga di piè (`<` `>` `+`) e, nell'elenco, una riga d'intestazione:

| Scheda | Righe di fascia senza intestazione | Con intestazione |
|---|---|---|
| CYD | 3 | 2 |
| Hosyond 4" | 4 | 3 |
| Guition 4" | 6 | 5 |
| Waveshare 7B | 8 | 7 |
| Guition 10.1" | 14 | 13 |

Passo 43-51 px (tasto 7 mm + 3 px di spazio). Un giorno vero ha 3-8 fasce modificabili (le "spento" automatiche non contano): sul CYD e sull'Hosyond
**serve paginare**. **Scorrimento: nessuna API** (`LIMITS.md`: «Cannot, on purpose: Block taps or scroll freely»; popup: «never scrolls»). Si pagina con
`<` `>` (VISTO, funzionano) o si mostra "+N altre" e si entra per pagina. Uno slider LVGL è consentito (`-DLV_USE_SLIDER=1` in `packages/core.yaml`,
«an LVGL slider on the card keeps its drags») e potrebbe sostituire i tasti ±5 min per l'ora: **non provato**; `LV_USE_ROLLER`/spinbox non sono compilati.

## 3. Testo lungo in `tessera::action`

**Documento** (`FIRMWARE_API.md`, "Actions"): «Up to 16 fields, each named with a-z, 0-9 and _, together at most 1 KB». **Codice** (`plugin_action.h`):
`MAX_FIELDS=16, MAX_KEY=32, MAX_BYTES=1024`; i byte contati sono `chiave + valore` di ogni campo (`bytes += field.key.size() + field.value.size()`); un testo
va in `data` (resta testo, nessun template: «Nothing in it ever runs as a template»).

**VISTO** (card dello spike, tasto "intestazione" invia testi di lunghezza crescente, tre campi `climate_entity_id`, `today_kind`, `bands`; 56 byte tra chiavi e
valori con `climate.soggiorno` e `tuesday`):

| Testo `bands` | Esito |
|---|---|
| 300, 600, 900, 960 caratteri | `Sent service=cronotermostato.set_day entity= (3 values)` |
| 1000, 1100 caratteri | `Not sent: ... the fields are larger than 1 KB` (`tessera::action` ritorna `false`) |

Quindi il limite è **1024 - (chiavi + altri valori)**: con un id climate di 30 caratteri restano circa 940 caratteri. Con il formato
`HH:MM-HH:MM modo temp;` (22-23 caratteri, es. `07:00-22:00 heat 20.5;`) entrano ~40 fasce; il programma ne ha al massimo 16 (`MAX_BANDS`) = ~370 caratteri.
Meno comodo ma sicuro anche con 5 fasce a 4 campi.

**Come si compone**: `std::string` locale in una funzione (la regola dei docs: «A short text you build to show is fine as a `std::string` in a function»),
non un membro. Quello che il pannello tiene (fasce, selezione) sono `struct` a campi fissi (24 x 16 byte) nell'oggetto card (`tessera::Card` va in PSRAM da solo).
Costo: LETTO dal codice (non misurabile sul banco, la memoria è quella del computer): la stringa è copiata in `JsonDocument` (1x), in `Field` (2x), in `encode` (3x):
circa 3-4 volte la lunghezza, a picco e per un istante, nella memoria interna; con ~370 caratteri sono ~1,5 KB, accettabile. LVGL: ~30 tasti + 8 etichette
= ~70 oggetti, circa 6-7 KB (stima con 100 B per etichetta e 80 per blocco come in AGENTS.md; non misurata sul chip).

**Permessi**: `permissions.home_assistant_actions` deve nominare `cronotermostato.set_day` (VISTO: il banco lo controlla). Con `entity = ""` (come fa già il tile)
**nessun tile aspetta la risposta**; `climate_entity_id` va nei campi.

**Attenzione, nuovo ostacolo (VISTO con `check.py`)**: l'opzione `room` ha già 15 attributi e il **massimo è 16** («tiles[0].options[0].attributes: at most 16 entries»).
Il piano ne vuole altri 4 (`program_name`, `program_rooms`, `today_kind`, `last_error`) = 19. `program_name` non serve: è già lo **stato** del sensore
(`entities.room.state`, VISTO nel log). Restano 3 da far stare in 1 posto libero: l'integrazione 0.3.0 deve **fondere** attributi (es. un solo attributo di testo
`edit` = `kind;rooms;error`, o unire `today_kind` e `holiday`) o togliere `slot_temp`/`next_*`/`override_until_time` se il tile non li usa più.

## 4. Feedback e conflitti

- L'unico canale di ritorno "normale" è lo **stato del sensore**: il core manda `on_state` alla card quando il tile cambia.
  **VISTO**: la card aperta riceve `on_state` (log `EDITSPIKE on_state n=4 updates=4 ... page=1`) senza ricostruirsi e restando sulla stessa pagina; con un
  popup sopra la card **continua a riceverlo** (`updates=5`, `page=0`). Quindi: dopo Salva il modello confronta le fasce arrivate con quelle inviate (hash) e dice
  "Salvato"; `last_error` non vuoto = "Non salvato: <motivo>"; passati 5 s senza cambio = "Non salvato (nessuna risposta)" (da `on_tick`, **VISTO** il passo "Salvataggio..."
  nell'etichetta del tasto; il timeout non l'ho aspettato sul banco).
- `tessera::action` risponde solo `bool` "inviata". Un secondo canale esiste, **LETTO e non provato**: `tessera::send(this, {"ask": "call_service:cronotermostato.set_day", ...})`
  con `permissions.ha_commands`, che ottiene la risposta del servizio (`on_message`, `{"re", "ok", "result"}`) se il servizio dichiara `SupportsResponse`; **richiesta max 512 byte**
  (~450 caratteri di fasce: ancora sufficiente per 16 fasce).
- **Conflitto** (un'altra modifica da HA mentre il pannello è aperto): il core non chiude nulla. Nello spike `on_state` ha sostituito le fasce e il pannello è rimasto
  sul dettaglio con la **copia di lavoro vecchia** e `sel` che indica un indice ormai diverso: un Salva avrebbe sovrascritto la modifica altrui. Il tile finale deve
  (a) tenere il nome/ora della fascia aperta e l'hash delle fasce "viste", (b) a `on_state` con hash diverso mostrare "Cambiato altrove" e tornare all'elenco (o chiudere
  il dettaglio), (c) possibilmente mandare l'hash base al servizio (`stale_day` già previsto per il giorno; stesso meccanismo per il contenuto, es. `conflict`).
- Il popup non vede `on_state`: sarebbe da cablare a mano (altro motivo per la card).

## 5. Collaudare nel banco

- `preview.yaml` **non ha `taps:`**: le chiavi lette da `bench.py` sono `tiles`, `states`, `answers`, `time_zone`, `cards`, `commands`, `pictures`, `savers`. Il banco
  tocca ogni tile una volta al centro e apre le card per id (con la sola entità: la card riceve `{state, name}`, non i dati del sensore: nello spike `n=0` fasce).
- **Come l'ho fatto**: `harness/flow.py` (usa `bench.py` e il suo `render_finger` = tocco vero attraverso il filtro del core) e parte dal tile, tocca la barra, apre la card
  con i **dati veri**, e poi preme i tasti leggendone la posizione dal log (`EDITSPIKE ... page= origin= id:x,y,w,h`). Riprodotto su 5 schede, 10 immagini a flusso ciascuna
  (elenco, dettaglio, modificato, salvataggio, nuovo stato durante la card, popup, popup dopo nuovo stato, testi lunghi). Per cambiare lo stato con la card aperta si rimanda
  lo stesso `record` con valori diversi (`sender.synchronize`): arriva solo `on_state`, la card resta aperta. **Con un record ricalcolato la card si chiude** (artefatto dello script).
- **Collaudo proposto**: (1) test di logica pura in C++ (`tests/test_logic.cpp`): modello di modifica, validazione, codifica del testo, stato di salvataggio, conflitto: è lì
  che stanno i difetti; (2) per l'interfaccia, **portare `flow.py` nel repo del plugin** (`tools/`) con la sequenza aprire riga -> + -> Salva e le immagini come prova di regressione;
  (3) prova su schermo vero, perché il banco non ha dito, memoria del chip né latenza della rete.
- **Limite dei tocchi (VISTO)**: due tocchi sullo **stesso punto** a meno di ~600 ms sono "un tocco" (docs: «a bounce on the same spot within 600 ms is one tap»). Con 4 tocchi
  ogni 0,45 s sullo stesso tasto ne sono arrivati **2**; alternando due tasti **4 su 4**. Quindi `+5` premuto di fila perde i tocchi: servono passi grandi (`+1h`, e meglio `+15`)
  oltre a `+5`, o lo slider. Il tile ha lo stesso filtro per `+`/`-` della temperatura (0,5 °C a tocco: con 600 ms è lento ma tollerabile).
- Il banco controlla anche l'aspetto (`audit`): dopo aver messo le label come figlie dei blocchi l'ultima corsa ha 0 avvisi su tutte e 5 le schede.

## 6. Controlli nativi per ora/numero

Nessuno nell'API 0.24: `plugin_api.h` non ha selettori, stepper o tastierini; `SettingsPage::number/choice` sono righe della pagina impostazioni, legate a entità ESPHome
(non a un dato per fascia); `LV_USE_ROLLER`/spinbox non sono compilati nel firmware (`packages/core.yaml`). Restano tasti disegnati, o uno slider LVGL (non provato).
**`+`/`−` confermati**, con passi di 5 min e di 1 h per l'ora (4 tasti per riga), e passo del termostato (`target_temp_step`, che il tile ha già) per la temperatura,
tasto Modo che gira caldo -> freddo -> spento.

## Tipo di pannello consigliato

**Card "modifica" con due pagine interne** (elenco -> dettaglio), tutto disegnato dal plugin con un solo `on_tap` e `tap_point()`; `on_back()` riporta dal dettaglio all'elenco.
Il banco di prova ha le due pagine in `edit_spike_ui.h`. Il programma e le stanze nel **titolo**. Tasti alti `ui::mm(7)` (di più dove c'è posto: sul 7B e sul 10.1" metà schermo resta vuota).

```
CYD 2.8" (300x176, tasto 40 px)                    CYD 2.8" - dettaglio (169 px)
+-----------------------------------+              +-----------------------------------+
| <   Casa - 3 stanze               |  titolo      | 06:00  [-1h] [ -5 ] [ +5 ] [+1h] |
+-----------------------------------+              | 08:30  [-1h] [ -5 ] [ +5 ] [+1h] |
| 00:00-06:00  Caldo  17.0          |  riga 1      | [ Caldo ]   21.0    [ - ] [ + ]  |
| 06:00-08:30  Caldo  21.0          |  riga 2      | [Elimina] [ Salva ] [ Indietro ] |
| 17:00-22:30  Caldo  21.5          |  riga 3      +-----------------------------------+
| [ < ]  [ > ]  [ + ]               |  piè (40 px)
+-----------------------------------+              (Salva diventa "...", "Salvato", "Errore")

Guition 4" (440x372): titolo + 6 righe + piè      Waveshare 7B / 10.1": stesso, tasti 48 px, 8-14 righe
```

Lo scorrimento non esiste: sul CYD e sull'Hosyond si pagina con `<` `>`. Le fasce automatiche "spento" non si mostrano tra le modificabili (l'integrazione le ricalcola).

## Formato del testo da inviare

```
bands = "00:00-06:00 heat 17.0;06:00-08:30 heat 21.0;17:00-22:30 heat 21.5"      // ; tra le fasce, spazio tra i campi
campi: climate_entity_id, today_kind (giorno della settimana o holiday), bands   // + (da concordare con l'integrazione) la base per il conflitto
```

22-23 caratteri a fascia, 16 fasce = ~370 caratteri, ben sotto i ~940 disponibili (1024 meno chiavi e altri valori). `off` senza temperatura (`22:00-24:00 off`). Se servisse stringere:
`0700-2200 h 20.5;` (17 caratteri). Il testo è sempre ASCII: nessun problema di font.

## Cosa NON ho verificato

- Tocchi con un dito vero, la latenza reale tra tocco, `on_state` e risposta di HA; memoria interna/PSRAM sul chip (solo stime dal codice); fluidità sul CYD con ~70 oggetti LVGL.
- Slider LVGL per l'ora; tocco accanto al popup (chiusura); popup su schermo in verticale; schede in verticale e griglie diverse dalla propria (provate solo le 5 schede in orizzontale).
- `tessera::send` con `call_service` e `SupportsResponse` come canale di risposta; il timeout "Non salvato (nessuna risposta)" a 5 s (la logica c'è, l'attesa non l'ho vista a schermo).
- Il limite di 16 attributi l'ho visto con `check.py`, ma non cosa fa il core con un sensore che ne ha di più.
- Il codice dello spike è da buttare: niente test C++, nessuna traduzione (testi italiani nel codice), niente `memory` per la card, ASCII `-` al posto del vero meno.
