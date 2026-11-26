---
name: Task channels & concurrency — roadmap (+ UI thread)
overview: VCX izolat, task spawn/wait, Channel partajat (copy + lock/unlock/revert); pregătire UI/UX pe thread dedicat și mesaje prin channel.
todos: []
isProject: true
---

# Plan: **AM-TASK-CHAN** — task-uri, `task.channel()`, UI/UX

**Status:** planificat (fără implementare în VM)  
**Fișier plan:** `task-channels-roadmap.plan.md`  
**Sursă idei:** [task_channels.txt](../my_ideas/task_channels.txt) (design Channel + note pthread)  
**Legături:** [vcx_sketch.txt](../my_ideas/vcx_sketch.txt) (VCX izolat), [roadmap_ideas_long_run.txt](../my_ideas/roadmap_ideas_long_run.txt) (etape cooperative → thread pool), [flatl_async_wasm_fix.txt](../my_ideas/flatl_async_wasm_fix.txt) (main thread / suspendare WASM)

**Notă:** sketch-ul Channel **nu** acoperă UI/UX; **§6** adaugă cerințe pentru viitorul strat UI (thread, canale, reguli de acces).

---

## 1. Poziție în roadmap

| Prerequisite | Stare |
|--------------|--------|
| VCX (`vcx.create`, copy, switch) | ✅ parțial / în curs (native `vcx`) |
| **Task** ca unitate ≠ `call` / `func` / `def` | **Lipsește** |
| Scheduler (cooperative → preemptiv) | **Lipsește** |
| `task.channel()` + tranzacții | **Lipsește** (doar design) |
| Modul / runtime **UI** | **Viitor** — nu blocker pentru Channel MVP backend |

**Ordine recomandată (aliniată la roadmap_ideas_long_run):**

1. **AM-TASK-COOP** — task-uri cooperative + `wait` (fără pthread încă).  
2. **AM-TASK-CHAN-v1** — Channel: `create`, `set`, `var`, mutex pe operație.  
3. **AM-TASK-CHAN-v2** — `lock` / `unlock` / `revert` + jurnal tranzacție.  
4. **AM-TASK-POOL** — worker pool pthread + `task_id` ownership (nu `pthread_t` singur).  
5. **AM-UI-RUNTIME** — model thread UI + canale dedicate (§6).

Channel poate fi prototipat pe **un singur thread** (mutex trivial) înainte de pool — teste pentru copy și tranzacții fără concurență reală.

---

## 2. Model arhitectural (din sketch)

### 2.1 Trei responsabilități

| Componentă | Rol |
|------------|-----|
| **VCX** | Memorie privată per context (variabile, slot tipizat) |
| **Task** | Execuție izolată; primește handle Channel ca argument |
| **Channel** | Stare partajată + sincronizare; **nu** este VCX |

### 2.2 Principii (păstrate din sketch)

1. **Izolare VCX** — niciun task nu citește direct variabilele altui task.  
2. **Sharing explicit** — doar prin handle Channel pasat la `task.spawn(…, channel, …)`.  
3. **Copy semantics** — `channel.var` / `channel.set` = copii (ca VCX); LIST/OBJECT = aceleași reguli COW/deep copy ca la assign.  
4. **Canale independente** — `task.channel()` de fiecare dată obiect nou; lock-uri independente.  
5. **Commit explicit** — doar `unlock()` publică tranzacția; la terminare task cu lock activ → **revert auto + release** (fără commit).  
6. **Lock non-reentrant** — al doilea `lock()` din același task → eroare.

### 2.2.1 Race conditions — VCX vs Channel + mutex (normativ)

**Întrebare tipică:** două task-uri scriu pe același Channel — e destul VCX, sau trebuie mutex?

| Componentă | Protejează race-uri între task-uri? |
|------------|-------------------------------------|
| **VCX** | **Nu.** Fiecare task are VCX **propriu** — nu există „doi scriu pe același slot VCX”. Izolarea VCX **nu** înlocuiește sincronizarea pe starea partajată. |
| **Channel** | **Da** — e singurul loc unde task-urile se întâlnesc pe date comune. Fără sync → race pe `entries` / corupție structură. |

**Mutex este parte din Channel, nu un add-on opțional.**

```text
Task A ──VCX A──┐
                ├──► Channel (mutex) ◄── stare publică
Task B ──VCX B──┘
```

| Nivel | Ce acoperă | Ce **nu** acoperă |
|-------|------------|-------------------|
| **C1** — `pthread_mutex_t` pe fiecare `set` / `var` | Operație **atomică** structural (fără corupție); last-write-wins pe o cheie | Read-modify-write multi-pas pe aceeași cheie |
| **C2** — `lock` / `unlock` / `revert` pe același mutex | Exclusivitate pe **tot** channel-ul pe durata tranzacției; RMW corect | Deadlock între două canale / UI care ține lock prea mult |

**Exemplu RMW (necesită C2, nu doar mutex pe `set`):**

```text
-- task A și B, fără channel.lock():
v = channel.var(ch, 'n')    -- ambele citesc 10
channel.set(ch, 'n', v+1)   -- ambele scriu 11 → un increment pierdut
```

Cu tranzacție:

```text
channel.lock(ch)
v = channel.var(ch, 'n')
channel.set(ch, 'n', …)     -- sau channel.update în v2
channel.unlock(ch)          -- commit
```

**Reguli derivate:**

1. Două task-uri pe **același** Channel → **întotdeauna** mutex (C1 pe op; C2 pe RMW / multi-cheie).  
2. Canale **diferite** → mutex-uri **independente** (fără ordine globală garantată între canale).  
3. VCX rămâne privat; **niciun** path „task A citește VCX-ul lui B”.  
4. Deadlock (A lock ch1, B lock ch2; UI + worker pe `ui.state`) — disciplină + §6.5; timeout pe `lock` = viitor, nu MVP.  
5. Cooperative pe un thread: mutex tot **obligatoriu în API** (pregătire pool P1); pe un singur thread e „trivial”, dar același contract.

### 2.3 API limbaj (țintă, modul `task`)

| Apel | Rol |
|------|-----|
| `task.channel()` | Creează Channel gol |
| `channel.set(name, value)` | Scriere atomică (sau în tranzacție activă → working set) |
| `channel.var(name)` | Citește copie |
| `channel.lock()` | Începe tranzacție exclusivă |
| `channel.unlock()` | Commit + eliberare lock |
| `channel.revert()` | Anulează modificări din tranzacția curentă; **păstrează** lock-ul |
| `channel.update(name, delta, op)` | Opțional v2 — `add` / `subtract` atomic pe o cheie |
| `task.spawn(#block, ...)` | Implementat: argumente poziționale mapate la parametrii numiți ai block-ului; NUMBER/STRING/LIST/OBJECT deep-copy, file handles respinse |
| `task.wait(taskId)` | Implementat: suspendare cooperativă, fără blocare OS |

Sintaxă exactă `spawn` / blocuri `#worker` — de aliniat la executor existent; **nu** amesteca cu `def` overload.

### 2.4 Implementare C (din sketch — decizii)

| Subiect | Decizie |
|---------|---------|
| Sincronizare | `pthread_mutex_t` (ERRORCHECK); **nu** depindem de C11 `<threads.h>` |
| Owner tranzacție | **`flat_task_id`**, nu `pthread_t` (mai multe task-uri pe același worker) |
| Tranzacție | **Nu** modifica `entries` live; **working journal** sau snapshot (MVP snapshot, apoi COW/jurnal — sketch §implementare) |
| Durată de viață | `ref_count` pe Channel; destroy când ref=0 |
| Executor | **Pool de thread-uri** recomandat (sketch final); cooperative înainte |

### 2.5 Handle-uri resursă = **NUMBER** (ca `vcxId`)

**Decizie produs (2026-11-13):** handle-urile runtime (VCX, Channel, task, file, …) sunt **NUMBER** (întreg pozitiv, ID în pool intern) — **fără** tip VAL nou, **fără** `<resource>` în slot.

| Resursă | În limbaj | Validare |
|---------|-----------|----------|
| **VCX context** | `ctx = vcx.create(n)` → **NUMBER** | `vcx.create.*` dacă id invalid la apel |
| **Channel** | `ch = task.channel()` → **NUMBER** | `task.channel.destroyed` / id invalid |
| **Task** (spawn) | `h = task.spawn(…)` → **NUMBER** | `task.wait.invalid` |
| **File** | `fp = file.open(…)` → **NUMBER** (migrare) | Astăzi sketch: `fp:<resource>` — **de aliniat** la același model |

**Reguli:**

- Limbajul păstrește doar tipurile VAL existente; semantica handle = **convenție de utilizare** + verificare în native (ca la `vcx.set(ctx)`).
- Slot tipizat opțional: `fp:u32` sau netipizat `fp` — **nu** tip special „resource” în `FlatSlotType`.
- `channel.set` / `channel.var` primesc **STRING** name + **VAL** copiat; primul arg al metodelor Channel este id NUMBER (receptor stil `file.read(fp)` — API TBD: `task.channel.set(ch, name, val)` sau `ch` ca id pe stack; detaliu la natives).

**Migrare `file`:** [file_sketch.txt](../my_ideas/file_sketch.txt) menționează „native resources, not … numeric identifiers” — **inversăm** pentru consistență cu VCX: refactor `fp` → id numeric, închidere invalidă id, pool `FlatFileHandle` (sau echivalent). Plan separat sau fază în module `file` — **nu** blochează C1 Channel.

**VM / FGIR:** **fără** opcode sau tag VAL pentru „channel”; doar NUMBER în registru/var ca la context id.

---

## 3. Faze implementare

### Faza **T0** — **AM-TASK-COOP** (fără Channel)

| # | Livrabil |
|---|----------|
| T0.1 | Model `FlatTask` + VCX per task la spawn |
| T0.2 | Scheduler cooperativ (yield / wait) în VM sau runtime |
| T0.3 | `task.spawn` / `task.wait` minim |
| T0.4 | Cleanup VCX la terminare task (legătură function executor) |

**Estimare:** 2–4 săptămâni (depinde de integrare FGIR).

### Faza **C1** — **AM-TASK-CHAN-v1**

| # | Livrabil |
|---|----------|
| C1.1 | `FlatChannel` + `set`/`var` + mutex per operație |
| C1.2 | Natives `task.channel`, `channel.set`, `channel.var` |
| C1.3 | Channel id **NUMBER** la `spawn` / `channel.*` (§2.5, model `vcxId`) |
| C1.4 | Teste: doi task cooperativi, config read-only partajat |

### Faza **C2** — **AM-TASK-CHAN-v2** (tranzacții)

| # | Livrabil |
|---|----------|
| C2.1 | `FlatChannelTransaction` + lock/unlock/revert |
| C2.2 | Revert la panic / terminare task |
| C2.3 | `channel.update` sau `atomic_add` (opțional) |
| C2.4 | Teste: transfer balance, revert pe condiție, auto-revert la panic |

### Faza **P1** — **AM-TASK-POOL**

| # | Livrabil |
|---|----------|
| P1.1 | Worker pool + coadă task |
| P1.2 | Waiting pe lock cross-thread |
| P1.3 | Stress: multi-writer `set`, last-write-wins documentat |

### Faza **UI** — **AM-UI-RUNTIME** (§6)

Definită separat; **poate consuma** Channel din C1/C2 fără a aștepta mdim/strict types.

---

## 4. Erori propuse

| Cod | Când |
|-----|------|
| `task.channel.not_found` | Cheie inexistentă la `var` (sau sentinel documentat) |
| `task.channel.lock.busy` | `lock` blocat (opțional timeout viitor) |
| `task.channel.lock.not_owner` | `unlock`/`revert` fără ownership |
| `task.channel.lock.reentrant` | al doilea `lock` în același task |
| `task.channel.lock.none` | `unlock` fără tranzacție activă |
| `task.channel.destroyed` | Channel distrus cu ref live |
| `task.spawn.arg` | Block label invalid sau argumente suplimentare (încă nesuportate în MVP) |
| `task.channel.id` | Id 0, negativ, sau necunoscut la orice op Channel |
| `task.wait.invalid` | Task ID necunoscut, self-wait sau apel fără context de task |

Prefixul exact (`task.*` vs `channel.*`) — de unificat la implementare cu SPECIFICATION panic families.

---

## 5. Non-goals (MVP Channel)

- Partajare prin referință directă între VCX-uri.  
- Channel ca substitut pentru parametrul `def` sau globale.  
- Reentrant locks / RW locks (viitor posibil).  
- `compare_exchange` în v1 (propus în sketch ca extensie).  
- Distributed / cross-process Channel.

---

## 6. UI/UX și thread-uri (extensie plan — **nu** în task_channels.txt)

### 6.1 Motivație

FlatL va avea la un moment dat un strat **UI/UX** (editor, wasm, eventual aplicații desktop). Interfața are constrângeri diferite de worker-ii de calcul:

- **Input, layout, draw** trebuie serializeate pe un **UI thread** (în browser = main thread; pe desktop = thread desemnat „UI”).  
- Worker-ii nu trebuie să apeleze direct natives de randare / widget.  
- **Channel** devine transportul pentru evenimente și stare UI ↔ logică.

### 6.2 Model propus: rol de execuție

| Rol | Unde rulează | Acces |
|-----|----------------|-------|
| **UI task / main** | UI thread (main în WASM) | `ui.*` (viitor), citire evenimente, commit frame |
| **Worker task** | Pool | logică, I/O greu, `math.*`, **fără** `ui.*` |
| **Bridge** | Channel(e) partajate | mesaje unidirecționale sau request/reply |

**Decizie produs (MVP UI):**

- Un singur **UI owner thread** per proces / instanță WASM.  
- Orice apel `ui.*` din alt task → **`task.ui.wrong_thread`** (sau panic compile dacă se poate detecta static — amânat).

### 6.3 Canale UI (convenție nume — Limbaj)

Nu obligatoriu în runtime; convenție documentată + tipuri de mesaj în viitor:

| Channel | Direcție | Conținut (exemple) |
|---------|----------|---------------------|
| `ui.in` | UI → logică | `click`, `key`, `resize`, `focus` |
| `ui.out` | logică → UI | `invalidate`, `set_label`, `dialog` |
| `ui.state` | bidirecțional cu tranzacții | model aplicație mic (counters, flags) |

Worker:

```flat
# sketch — API ui.* TBD
event = ui_in.var('last_event')
```

UI loop (pe main thread):

```flat
# pseudocod
ui.loop(#on_frame)
```

În **WASM**, bucla se integrează cu **Asyncify** / yield (vezi flatl_async_wasm_fix): VM suspendă fără a distruge task-ul UI; worker-ii pot continua pe pool doar dacă runtime-ul permite (TBD same-origin threading în WASM).

### 6.4 Integrare cu task.spawn

| Opțiune | Descriere |
|---------|-----------|
| **A — Implicit** | Programul `#main` rulează pe UI thread; `task.spawn` creează workers |
| **B — Explicit** | `task.spawn(worker, … ; "background")` vs `; "ui"` (AM-TAGURI viitor) |
| **C — Affinity API** | `task.set_affinity(ui)` doar intern runtime |

**Recomandare plan:** **A** la început + verificare thread la `ui.*`; **B** doar dacă apare nevoia de mai multe roluri.

### 6.5 Priorități UI vs fairness

- Scheduler: **UI task** trebuie rulat la fiecare frame sau la eveniment; workers cu prioritate mai joasă.  
- **Nu** ține lock Channel în bucla UI mult timp — risc deadlock cu worker care așteaptă `ui.state.lock()`.  
- Pattern: UI citește `ui.in` fără lock lung; batch updates pe `ui.out` cu `lock` scurt sau `set` atomic per cheie.

### 6.6 Faze UI (după Channel v1)

| # | Livrabil |
|---|----------|
| UI.1 | Document `doc_src/ui_thread.md` — reguli main vs worker |
| UI.2 | Hook runtime: identificare UI thread + panic cross-call |
| UI.3 | Prototip `ui.in` / `ui.out` ca Channel pre-creat sau `task.channel()` în bootstrap `#main` |
| UI.4 | Integrare fleditor WASM: evenimente tastatură → channel → script FlatL |

**Non-goals UI v0:** toolkit complet, layout engine, binding la React — doar **model thread + Channel**.

### 6.7 Legături cu alte planuri

| Plan | Relație |
|------|---------|
| **AM-VECT-RECT** | calcule grele pe worker; rezultat `channel.set('matrix', …)` |
| **AM-TAGURI** | eventual tag pe `spawn` pentru rol (`background`) |
| **VCX** | fiecare task UI/worker cu VCX separat; UI state local în VCX, publicare prin Channel |

---

## 7. Teste și documentație

| Suite | Conținut |
|-------|----------|
| `task/010` | channel create/set/var, copy semantics |
| `task/020` | lock/unlock/revert, balance transfer |
| `task/030` | panic + auto-revert la terminare task |
| `task/040` | două canale independente |
| `task/050` | (post-UI) wrong_thread pe native ui mock |

Doc: `doc_src/natives/task.md`, `doc_src/natives/channel.md` (sau secțiune unică), §6 → `ui_thread.md`.

---

## 8. Criterii done

**AM-TASK-CHAN-v2 (fără UI):**

- [ ] Channel + tranzacții conform sketch lifecycle  
- [ ] `task_id` ownership, nu doar pthread  
- [ ] Auto-revert la exit task cu lock  
- [ ] Teste 010–040 pe cooperative + pool  

**AM-UI-RUNTIME (minim):**

- [ ] §6 documentat și acceptat  
- [ ] UI thread guard + un canal demonstrativ în WASM/editor  

---

## 9. Estimare (orientativ)

| Fază | Efort |
|------|-------|
| T0 cooperative | 2–4 săpt |
| C1 Channel basic | 1–2 săpt |
| C2 tranzacții | 2–3 săpt |
| P1 pool | 2–3 săpt |
| UI.1–UI.4 | 2–4 săpt (paralel cu P1, după C1) |

**Total Channel production-ready:** ~2–3 luni focalizat (fără UI complet).

---

## 10. Următor pas concret

1. Mini-spec natives: pool Channel + return **NUMBER**; validare id la intrare (§2.5).  
2. Implementare **C1** pe **un thread** (mutex pentru API, fără pool) + teste copy LIST.  
3. Draft **`ui_thread.md`** (§6) înainte de orice native `ui.*` — fixează main vs worker pentru WASM și desktop.

---

## 11. Ce păstrăm / clarificăm față de sketch

| Sketch | Plan |
|--------|------|
| `task.channel()` API | ✅ țintă C1/C2 |
| Snapshot tranzacție | MVP; jurnal COW după |
| pthread pool | P1, după cooperative |
| UI / thread principal | **§6** — adăugat aici |
| `task.start` din roadmap vechi | Renumește la **`task.spawn`** (unificare la implementare) |
| Handle Channel ca tip nou VAL | **Respins** — **NUMBER** id (§2.5) |
| `fp:<resource>` | **Migrare** la NUMBER id ca VCX |

---

*Ultima actualizare plan: 2026-11-25 — §2.2.1 race / mutex (VCX ≠ sync; Channel + C1/C2); §2.5 handle NUMBER; UI §6.*

### Archive 282 update — cooperative task/Channel safety

The current runtime now rejects cyclic task wait dependencies, tracks direct Channel-ID spawn arguments as task-held references, releases those references at task termination/cancellation/destruction, and emits structured Channel lifecycle/operation events for `--show-channel-log`. These additions are still single-thread cooperative semantics; they do not make `FlatRuntime`, `FlatVm`, or native execution safe for a pthread worker pool.
