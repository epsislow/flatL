# Task Runtime & Channels — roadmap de implementare

**Versiune document:** 1.0 (plan)  
**Data:** 2026-11-25  
**Status:** fazare agreată; milestones viitoare, nu funcționalități deja disponibile.

## Direcția generală

Implementarea se face în straturi: mai întâi un runtime cooperativ izolat, apoi Channel cu mutex și tranzacții, apoi worker pool cu thread-uri reale. UI Runtime este o etapă separată, dependentă de contractele Channel și de constrângerile platformei.

## F0 — Foundation (înainte de v1.0)

### F0.1 — Audit

- Audit `flat_vm_run_opts()` și durata de viață a `VmState`.
- Audit executor global, frame stack, panic drain.
- Audit VCX active runtime și call-instruction resolver.
- Inventar stare globală VM/native: RNG, locale, native runtime reset, stdio și module native.
- Documentarea dependențelor și a riscurilor înainte de refactor.

### F0.2 — Logger central

- Logger comun cu categorii TASKS și CHANNELS.
- CLI `--show-tasks-log`, `--show-channel-log`.
- Logging off implicit și fast path fără alocare/formatare.
- Output live `stdout`; buffer opțional cu limită și dropped counter.
- Sequence number monoton.
- Teste pentru flags, filtrare, output, buffering și limită.

### F0.3 — Execution context refactor

- Mutarea binding-ului executorului și frame stack-ului din globale în context explicit per execuție/task.
- Mutarea bookkeeping-ului call instruction în contextul execuției.
- VCX active runtime/resolver scoped la context, nu la un singur global activ.
- Panic drain și cleanup strict scoped la contextul task-ului.
- Teste de regresie pentru nested calls, unique/global executor și panic unwind.

**Criteriu de ieșire F0:** contextul de execuție poate fi creat/curățat independent fără să afecteze alt context; logger-ul poate fi activat fără impact când este dezactivat. F0 nu livrează încă `task.spawn`, scheduler sau Channel.

## v1.0 — Cooperative Task Runtime

Scop: task-uri reale, cooperative, pe un singur thread OS.

Livrabile:

1. Model `FlatTask`, `flat_task_id` și lifecycle.
2. VCX separat pentru fiecare task.
3. Scheduler cooperativ și puncte explicite de yield/wait; semantica exactă se specifică înainte de implementare.
4. `task.spawn(block, ...)` și `task.wait(taskId)`.
5. Persistența stării VM necesare suspendării/reluării: instruction pointer, call stack, loop frames și state necesar native execution.
6. Izolare panic între task-uri; un task FAILED nu oprește schedulerul.
7. Extinderea generală a panic block handler cu `.any`, inclusiv forma `#panic.#worker.any`.
8. Cleanup determinist VCX, executor scopes, handles și orice resurse task-owned.
9. Logging real al lifecycle-ului și testare runtime prin `bin/run_tests`.

Criterii de acceptare:

- două task-uri au VCX-uri independente;
- task-ul poate fi pornit, așteptat și observat în stările documentate;
- panic-ul unui task nu oprește alt task;
- handler-ul `.any` este rezolvat în ordinea documentată și poate face jump în același context task;
- nu rămân scope-uri, VCX-uri sau resurse abandonate după succes/eșec.

## v1.5 — Shared Channels & Transactions

Scop: partajare explicită a datelor între task-uri.

Livrabile:

1. Pool de Channel-uri și handle NUMBER.
2. `task.channel()`.
3. `channel.set(ch, name, value)` și `channel.var(ch, name)`.
4. Semantica de copiere FlatL pentru primitive, LIST și OBJECT.
5. Mutex per Channel și sincronizare automată C1 pentru fiecare `set`/`var`.
6. `channel.lock`, `channel.unlock`, `channel.revert` pentru C2.
7. Ownership tranzacțional prin `flat_task_id`.
8. Snapshot/working state și commit explicit.
9. Auto-revert + release la panic sau terminarea task-ului cu lock activ.
10. `channel.log()`/diagnostic și evenimente Channel.
11. Teste pozitive, negative, copy, transaction, panic cleanup, reference count și canale independente.

Criterii de acceptare:

- fiecare operație individuală protejează structura Channel;
- read-modify-write multi-operație este corect doar în tranzacție, conform contractului;
- un task nu poate elibera lock-ul altui task;
- panic/terminare nu lasă lock activ;
- canalele diferite nu se blochează reciproc.

## v2.0 — Parallel Runtime / Worker Pool

Scop: execuție efectivă simultană pe mai multe thread-uri OS.

Livrabile:

1. Worker pool pthread și coadă de task-uri.
2. Mutex/condvar și sincronizare cross-thread pentru scheduler, task registry și pool-urile de handle-uri.
3. Mutex real per Channel (de ex. pthread mutex cu tip/error checking potrivit platformei).
4. Ownership logic bazat pe `flat_task_id`, niciodată exclusiv pe `pthread_t`.
5. Audit și eliminare a stării globale incompatibile cu VM-uri simultane.
6. Shutdown, task-uri waiting și erori worker.
7. Stress tests multi-writer, invariants, deadlock și cleanup.
8. Build/feature behavior documentat pentru platforme fără pthread sau cu limitări WASM.

Criterii de acceptare:

- mai multe task-uri rulează efectiv în paralel;
- fără corupere a Channel sau runtime;
- panic-ul unui task nu afectează pool-ul;
- shutdown-ul eliberează toate resursele;
- testele relevante trec pe platformele suportate.

## v2.5 — UI Runtime (etapă separată)

Scop: reguli pentru UI thread și bridge între UI și logică.

Direcție propusă:

- un singur UI owner thread per proces/instanță WASM;
- apelurile `ui.*` din alt task produc o eroare precum `task.ui.wrong_thread`;
- workers nu apelează direct natives de randare/widget;
- Channel bridge pentru evenimente/stare UI ↔ logică;
- integrare cu suspendare/reluare VM în WASM (Asyncify și constrângerile browser threading);
- prototip în FLEditor: input UI → Channel → FlatL și actualizări înapoi.

Nu intră aici un toolkit UI complet, layout engine sau binding la React.

## Ordine tehnică de lucru

1. Audit înainte de cod.
2. Mini-spec pentru ownership, lifecycle, yield/wait, panic și erori.
3. Teste în `test/cases/` prin `bin/run_tests`.
4. Implementare incrementală, fără amestecarea schedulerului cooperativ cu pthread.
5. Diagnostic structurat pentru stări interne.
6. Teste negative și cleanup/fault paths.
7. Build, suite relevante, apoi regresie completă.
8. Actualizare documentație în același commit cu schimbarea.

Estimările din planul original sunt orientative și nu reprezintă angajamente calendaristice.
