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

## 1. Aggiornare l'integrazione a 0.3.2 su HA di Alex (PRIMA di aggiornare il plugin)

Il tile legge le fasce di oggi (`today_*`) dal sensore di stanza (dalla 0.2.0); la **modifica delle fasce** (plugin 0.2.0) legge anche `edit_info`, `edit_seq`, `edit_n`, `today_flags` e chiama `cronotermostato.set_day`, che esistono dalla **0.3.2**. Con un'integrazione più vecchia della 0.3.1 il plugin nuovo non offre il tasto Modifica (card in sola lettura); con la 0.3.1 il tasto c'è ma Salva dice sempre "Non modificabile da qui" (manca `edit_n`); la barra poi ha bisogno di `today_flags` per riconoscere le fasce automatiche. Quindi non va aggiornato il plugin prima dell'integrazione per non avere un tile che parla a un servizio che non c'è. Copia l'integrazione:

```sh
rsync -rlt --exclude='__pycache__' /home/alexcoscia/cronotermostato/custom_components/cronotermostato/ ~/mnt_hass/homeassistant/custom_components/cronotermostato/
```

Poi riavvia Home Assistant (Impostazioni → Sistema → Riavvia). Un riavvio ricarica anche lo Scheduler: aspetta che sia
tornato tutto disponibile e che i termostati abbiano un valore.

Controllo: in HA, Impostazioni → Dispositivi e servizi → Cronotermostato, la versione è 0.3.2, e in Strumenti per sviluppatori → Azioni compare `Cronotermostato: set_day`.

## 2. Vedere gli attributi `today_*` in HA

1. Strumenti per sviluppatori → Stati.
2. Filtra per `sensor.` e scegli il sensore della stanza di prova (per esempio `sensor.termostato_casa_programma`).
3. Negli attributi devono esserci: `today_start`, `today_end`, `today_mode`, `today_temp`, `today_auto`, `today_flags`, `today_preset`, `edit_info`, `edit_seq`, `edit_n`, `slot_mode`,
   `slot_temp`, `slot_end_time`, `next_mode`, `next_temp`, `next_start_time`, `manual`, `override_until_time`, `status`,
   `holiday`. Le liste hanno la stessa lunghezza; `today_temp` = -100 vuol dire fascia senza temperatura.
4. `status` deve essere `ok` (in prova vedrai anche `paused`, `unassigned` e gli altri).

Se mancano gli attributi `today_*` l'integrazione non è alla 0.2.0 o il riavvio non è avvenuto; se mancano `edit_info`, `edit_seq`, `edit_n` o `today_flags` non è alla 0.3.2. `edit_info` ha la forma `giorno;stanze;revisione;errore` (per esempio `tue;1;3fa9c01b2d4e;`).

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
4. Controlla nel dispositivo il permesso per le azioni HA `cronotermostato.set_manual`, `cronotermostato.resume_auto` e `cronotermostato.set_day`.

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
3. Il dispositivo ha il permesso per le azioni HA `cronotermostato.set_manual`, `cronotermostato.resume_auto` e `cronotermostato.set_day`?
   Senza permesso lo schermo non invia nulla (nell'app Tessera, scheda del dispositivo, permessi delle azioni HA).
4. L'integrazione è alla 0.3.2 e i servizi `cronotermostato.set_manual`/`resume_auto`/`set_day` esistono (Strumenti per sviluppatori
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

## 7. Prova della modifica delle fasce (plugin 0.2.1, integrazione 0.3.2)

**ATTENZIONE: la modifica cambia un PROGRAMMA, e un programma è condiviso da tutte le stanze che lo usano.** Si prova SOLO
con un **programma di prova assegnato alla sola stanza `climate.termostato_casa`**. Non si modifica dal display un
programma usato da stanze di produzione. Prima della prova, in HA (pannello Cronotermostato):

1. Crea un programma nuovo, per esempio "Prova display", copiando i giorni da uno vero se vuoi fasce realistiche.
2. Assegnalo a `climate.termostato_casa` e a nessun'altra stanza (il titolo nella lista di modifica deve dire
   "Prova display - 1 stanza"; se dice 2 o più, FERMATI: il programma è condiviso con un'altra stanza).
3. Annota le fasce di oggi del programma di prova (o fai uno screenshot dell'editor di HA): servono per tornare indietro.
4. Con l'integrazione già a 0.3.2 (sezione 1) aggiorna il plugin sullo schermo (sezione 3, "Build again") e controlla il
   permesso `cronotermostato.set_day` del dispositivo.

Il tile deve avere l'opzione **Consenti la modifica delle fasce** attiva (lo è di default).

**Cosa osservare**

| Passo | Cosa deve succedere |
|---|---|
| Tocco sulla barra | Si apre la card delle fasce di oggi con il tasto **Modifica** in alto a destra (alto almeno 7 mm). Se il tasto non c'è: stanza in pausa/non assegnata/non disponibile, integrazione < 0.3.2 o opzione spenta. |
| **Modifica** | Elenco delle fasce utente di oggi, una riga per fascia (le fasce "spento" automatiche non sono righe), con il programma e "1 stanza" nel titolo (sul CYD nel mezzo del piè di pagina). Con più fasce delle righe: `<` `>` cambiano pagina. |
| Tocco su una riga | Il dettaglio: Inizio e Fine con -1h -15 +15 +1h; il tasto del modo (Caldo/Freddo/Spento); la temperatura con − e +; Elimina, Salva, Indietro. |
| Passi | Ogni tocco sposta di 15 min o 1 h (la temperatura del passo del termostato). Due tocchi sullo STESSO tasto entro 0,6 s contano come uno: è normale, aspetta un attimo. Un orario che non si può spostare ancora (contro la fascia accanto, meno di 15 min) non si muove e il tasto Salva/la riga di stato dice "Limite". |
| **+** (Aggiungi) | Una fascia di un'ora nel primo buco di un'ora; "Pieno" a 16 fasce, "Nessuno spazio" se non c'è un buco. |
| **Salva** | Un solo `cronotermostato.set_day` (HA: Strumenti per sviluppatori → Eventi/log, o il log del dispositivo `Sent service=cronotermostato.set_day`). Il tasto fa `...`, poi **Salvato** e si torna all'elenco con le fasce nuove; in HA l'editor mostra le stesse fasce. Il sensore cambia `program_revision` (campo 3 di `edit_info`). |
| Errore | Se HA rifiuta il tasto dice il motivo breve (Errore / Cambiato / Fuori limite...) e in `edit_info` (ultimo campo) compare il codice (`stale_day`, `conflict`, `day_locked`, `invalid_bands`, `temp_range`, `invalid_program`). Senza risposta in 5 s: **Non salvato**. |
| Cambiato altrove | Con la lista aperta, cambia una fascia dello stesso programma dall'editor di HA: la lista si ricarica e dice "Cambiato altrove"; le modifiche non salvate si perdono. |
| Elimina | NON toglie niente e non invia niente: apre la **conferma** dentro la card, con la fascia ("08:30-10:30 Riscaldamento 21,5°"), "Programma condiviso da N stanze" se il programma ha più di una stanza (nella prova deve essere 1: l'avviso non c'è) e due tasti grandi: **Annulla** a sinistra, **Conferma** a destra, ben distanziati. |
| **Annulla** | Torna al dettaglio com'era; nessun invio (log del dispositivo: nessun `Sent service=cronotermostato.set_day`). |
| **Conferma** | Toglie la fascia e salva subito (un `set_day`); torna all'elenco. Conferma non sta dove stava Elimina e ignora i tocchi per i primi 600 ms: prova a toccare Elimina due volte di fila, velocemente: il secondo tocco non deve confermare. Annulla funziona subito. |
| Conferma e novità | Con la conferma aperta cambia la stessa fascia dall'editor di HA, oppure metti la stanza in pausa: la conferma si chiude da sola e non invia niente (con la pausa la card torna in sola lettura). Il tasto Indietro/freccia della card, a conferma aperta, vale Annulla. |
| Indietro | Dal dettaglio all'elenco, buttando le modifiche non salvate; dall'elenco chiude la card (freccia in alto a sinistra). |
| Fascia bloccata | Una fascia con preset/temperatura che il display non sa riscrivere si vede, è marcata "Bloccata" e non si apre; un giorno che ne ha una non si salva da qui. |
| Mezzanotte | Se passa mezzanotte con l'elenco aperto: "Nuovo giorno", si ricarica il giorno nuovo. |
| Opzione spenta | Spegni **Consenti la modifica delle fasce** nell'ispettore: la card torna in sola lettura (nessun tasto Modifica). |

Da annotare: le zone di tocco (7 mm: comode?), se Conferma e Annulla (distanza, 600 ms di attesa su Conferma) sono comodi o fastidiosi, quanto passa fra Salva e "Salvato", se i passi 15 min/1 h bastano, e su CYD/
Hosyond se l'elenco a 3-4 righe per pagina è usabile. Non è stata misurata la memoria sul chip: se lo schermo si riavvia
o perde il Wi-Fi aprendo la modifica, annotalo (e il giorno con più fasce).

**Come tornare indietro (modifica)**

- Rimetti le fasce del programma di prova come annotate: dall'editor di HA (pannello Cronotermostato), oppure dallo stesso
  display con le stesse modifiche all'inverso.
- Se il programma di prova non serve più: riassegna `climate.termostato_casa` al programma di prima e cancella "Prova
  display" dal pannello.
- Per fermare nuove modifiche dallo schermo: spegni l'opzione **Consenti la modifica delle fasce** del tile, o togli
  `cronotermostato.set_day` dai permessi del dispositivo.
- Per tornare al plugin 0.1.x: ricostruisci con la cartella di prova del commit precedente (la card torna in sola lettura).

### Colori e valori sulla barra (plugin 0.2.2, colori netti dalla 0.2.3)

Con un programma che ha fasce dello stesso modo e valori diversi (per esempio il programma "Casa" di martedì: riscaldamento
con preset away 00:00-15:00, home 15:00-22:30, away 22:30-24:00), controlla sul vero schermo, in chiaro e in scuro:
1. le fasce con preset o temperatura diversi hanno colori nettamente diversi (riscaldamento: ambra, mattone, oro, oliva; raffrescamento: azzurro, ciano, viola, blu; spento: quattro grigi), e quelle con lo stesso valore lo stesso colore; away e home del programma "Casa" si distinguono a colpo d'occhio, in chiaro e in scuro;
2. fra una fascia e la successiva c'è un filo di separazione ben visibile;
3. nei tile da 2x2 in su la barra riporta temperatura o preset sulle fasce abbastanza larghe, e il testo si legge su ogni tinta;
4. il tile 1x1 mostra solo colori e separatori;
5. nell'elenco della card ogni riga ha il quadratino dello stesso colore e mostra temperatura e/o preset.

## 8. Cosa riferire dopo la prova

Schermo e scheda usati, versione dell'app Tessera, cosa ha funzionato, cosa no, foto/screenshot di tile e card, e se le
zone di tocco erano comode. Con questi dati si decide se passare ad altre stanze (una alla volta, con il via libera di
Alex).
