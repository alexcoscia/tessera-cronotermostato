# Prova su schermo vero (checklist per Alex)

Il plugin finora è stato visto solo sul banco (`tools/preview.py`): immagini, zone di tocco e comandi che *verrebbero*
inviati. Qui c'è il percorso per provarlo su uno schermo Tessera vero e su Home Assistant vero.

**Regola d'oro:** si prova SOLO con la stanza di prova `climate.termostato_casa`. Nessun termostato di produzione, nessun
comando a un termostato vero senza il via libera di Alex. Lo Scheduler di HA (che comanda 12 termostati) non si tocca.

## Cosa NON fare

- Non assegnare il tile a una stanza di produzione, né assegnare a una stanza di produzione un programma
  dell'integrazione, finché la prova non è andata bene.
- Non premere + / − / pillola su una stanza vera "per vedere": ogni tasto manda un comando vero a HA.
- Non toccare lo Scheduler né le sue entità.
- Non fare push, non pubblicare release, non creare il repository su GitHub (a meno che Alex lo chieda).
- Non lasciare la stanza di prova in manuale a fine prova: premere la pillola per tornare in automatico (vedi "Tornare
  indietro").

## 0. Prima di cominciare

- Tessera 0.4.93 o più recente (firmware 0.59.0, API plugin 0.24).
- Uno schermo su cui il plugin sta: griglia con almeno 2 colonne (il Waveshare 7" va bene; CYD e Waveshare 4,3" in
  verticale NO, hanno una colonna sola).
- `python3 ~/tessera-plugins/tools/check.py .` stampa `cronotermostato: ok` e `bash tests/run.sh` passa.
- La stanza di prova `climate.termostato_casa` è in `heat` o `cool` (i tasti agiscono solo in questi modi) e ha un
  programma assegnato nell'integrazione.

## 1. Aggiornare l'integrazione a 0.2.0 su HA di Alex

Il tile legge le fasce di oggi (`today_*`) dal sensore di stanza, che esiste dalla 0.2.0. Copia l'integrazione:

```sh
rsync -rlt --exclude='__pycache__' /home/alexcoscia/cronotermostato/custom_components/cronotermostato/ ~/mnt_hass/homeassistant/custom_components/cronotermostato/
```

Poi riavvia Home Assistant (Impostazioni → Sistema → Riavvia). Un riavvio ricarica anche lo Scheduler: aspetta che sia
tornato tutto disponibile e che i termostati abbiano un valore.

Controllo: in HA, Impostazioni → Dispositivi e servizi → Cronotermostato, la versione è 0.2.0.

## 2. Vedere gli attributi `today_*` in HA

1. Strumenti per sviluppatori → Stati.
2. Filtra per `sensor.` e scegli il sensore della stanza di prova (per esempio `sensor.termostato_casa_programma`).
3. Negli attributi devono esserci: `today_start`, `today_end`, `today_mode`, `today_temp`, `today_auto`, `slot_mode`,
   `slot_temp`, `slot_end_time`, `next_mode`, `next_temp`, `next_start_time`, `manual`, `override_until_time`, `status`,
   `holiday`. Le liste hanno la stessa lunghezza; `today_temp` = -100 vuol dire fascia senza temperatura.
4. `status` deve essere `ok` (in prova vedrai anche `paused`, `unassigned` e gli altri).

Se mancano gli attributi `today_*` l'integrazione non è alla 0.2.0 o il riavvio non è avvenuto.

## 3. Mettere il plugin sullo schermo

Due modi (docs/TESTING.md di `~/tessera-plugins`), il primo non pubblica nulla:

**A. Cartella di prova in HA (consigliato, nessun push).** Copia la cartella del plugin in HA come
`/config/tessera-plugins/cronotermostato/` (con `tessera-plugin.yaml`, `plugin.yaml`, `components/`, `translations/`;
`tests/`, `docs/`, `.preview/` non servono). Per esempio:

```sh
rsync -rlt --exclude='__pycache__' --exclude='.git' --exclude='.preview' --exclude='tests' --exclude='docs' ~/tessera-cronotermostato/ ~/mnt_hass/tessera-plugins/cronotermostato/
```

(il percorso esatto del montaggio dipende da come è montata `/config`: verifica prima dove sta la cartella `esphome`; la
cartella `tessera-plugins` va accanto). Nell'app Tessera, Plugins: compare **Cronotermostato** con l'etichetta *Test*.
Dopo ogni modifica: apri il plugin nella scheda Plugins dello schermo e premi **Build again**.

**B. Link del repository.** Richiede un repository su GitHub: NON farlo senza chiedere ad Alex.

Poi:

1. Abilita il plugin per lo schermo; l'app costruisce il firmware (minuti) e aggiorna via Wi-Fi.
2. In Layout metti il tile **Programma stanza** (da 1x1 a 3x2; per i tasti e la forma completa serve circa 2x2, vedi "Sizes" nel README).
3. Nell'ispettore scegli `climate.termostato_casa` e il suo sensore `sensor.<stanza>_programma`.
4. Controlla nel dispositivo il permesso per le azioni HA `cronotermostato.set_manual` e `cronotermostato.resume_auto`.

## 4. Cosa osservare, funzione per funzione

Tieni aperti gli Strumenti per sviluppatori → Stati (climate e sensore) accanto allo schermo, e il log di HA.

| Funzione | Cosa deve succedere |
|---|---|
| Nome | Il nome della stanza in alto (se il tile è abbastanza alto). Sul 2x2 di uno schermo piccolo può sparire: è previsto. |
| Valori | Il setpoint grande uguale a `temperature` del climate; sotto la temperatura attuale; puntino ambra con `hvac_action: heating`, blu con `cooling`. |
| Riga di stato | In automatico "Auto · nome programma"; con riga "Poi ..." se c'è spazio. |
| + / − | Un tocco sposta il valore di un passo (`target_temp_step`), subito sullo schermo, nei limiti min/max del climate. Prima dei 600 ms non parte nulla. |
| Ritardo 600 ms | Tocca più volte di fila: parte UN solo comando, 600 ms dopo l'ultimo tocco, col valore finale. In HA, Strumenti sviluppatori → Eventi o il log mostrano una sola chiamata `cronotermostato.set_manual`. |
| Effetto del comando | Il sensore passa a `manual: true`, `override_until_time` = fine della fascia attuale; la pillola dice "Manuale fino alle HH:MM x". |
| Ritorno a 5 s | Per provarlo: se HA non conferma il nuovo setpoint entro 5 s, lo schermo torna al valore vero. (Si vede solo se il comando non va a buon fine, per esempio col permesso azioni tolto: non forzarlo su stanze vere.) |
| Pillola | Con "Manuale ... x" è un tasto: un tocco manda `cronotermostato.resume_auto`, la pillola torna "Auto · programma" e il setpoint torna a quello della fascia. In automatico NON è un tasto. |
| Tocco sulla barra → card | Il tocco sulla barra (e sulla striscia appena sopra) apre l'elenco delle fasce di oggi, con quella attuale evidenziata; gli orari sono sempre 24h. Più di 16 fasce: ultima riga "+N altre". Si chiude come ogni card di Tessera. |
| Zone di tocco | Prova + e − alle estremità e al centro; il tocco fuori dai tasti e dalla barra non deve fare niente. Annota se le zone sono troppo strette per il dito. |
| Stanza in pausa | Metti in pausa la stanza di prova nell'integrazione: lo schermo dice "In pausa" e i tasti sono spenti (non rispondono). |
| Stanza non assegnata | Togli il programma alla stanza di prova: "Nessun programma", barra vuota, tasti spenti. |
| Climate `off` / `auto` | Con la stanza di prova in `off` o `auto` i tasti sono spenti e non rispondono; nessun comando parte. |
| Climate non disponibile | Se il climate è `unavailable`: "Non disponibile", barra spenta. |
| Festivo | Se oggi è festivo, il sensore ha `holiday: true`: controlla il programma del giorno. |

Dopo ogni prova guarda anche lo Scheduler: non deve essere cambiato nulla nelle sue entità.

## 5. Se un tasto non risponde

1. Il climate è in `heat` o `cool`? In `off`, `auto`, `heat_cool` i tasti sono volutamente spenti.
2. La stanza ha un programma assegnato e non è in pausa? Il sensore deve avere `status: ok`.
3. Il dispositivo ha il permesso per le azioni HA `cronotermostato.set_manual` e `cronotermostato.resume_auto`?
   Senza permesso lo schermo non invia nulla (nell'app Tessera, scheda del dispositivo, permessi delle azioni HA).
4. L'integrazione è alla 0.2.0 e i servizi `cronotermostato.set_manual`/`resume_auto` esistono (Strumenti per sviluppatori
   → Azioni)?
5. Il log dello schermo (nell'app Tessera) e il log di HA: cerca errori su `cronotermostato`.
6. Il tocco cade nella zona giusta? Se la zona di tocco è troppo stretta (sul CYD la pillola è ~4,4 mm), annotalo: è un
   dato per correggere il plugin, non un malfunzionamento di HA.

## 6. Tornare indietro

- **Stanza di prova in manuale:** tocca la pillola "Manuale ... x", oppure da HA chiama l'azione
  `cronotermostato.resume_auto` sulla stanza di prova (Strumenti per sviluppatori → Azioni). La forzatura finisce
  comunque alla fine della fascia.
- **Togliere il plugin dallo schermo:** nell'app Tessera, Plugins, disabilita Cronotermostato per lo schermo e rimuovi il
  tile dal Layout; lo schermo ricostruisce il firmware senza.
- **Togliere la cartella di prova:** cancella `/config/tessera-plugins/cronotermostato/` da HA.
- **Integrazione:** il programma e le stanze dell'integrazione restano dov'erano; per tornare alla 0.1.x ripristina la
  cartella `custom_components/cronotermostato` dal backup/commit precedente (`git -C /home/alexcoscia/cronotermostato`)
  e riavvia HA. Gli attributi `today_*` in più non danneggiano nulla.
- **Scheduler:** non è stato toccato, continua a comandare i suoi termostati.

## 7. Cosa riferire dopo la prova

Schermo e scheda usati, versione dell'app Tessera, cosa ha funzionato, cosa no, foto/screenshot di tile e card, e se le
zone di tocco erano comode. Con questi dati si decide se passare ad altre stanze (una alla volta, con il via libera di
Alex).
