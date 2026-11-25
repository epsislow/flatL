# Task Runtime & Channels — decizii de design

**Status:** decizii de proiect agreate în discuțiile din 2026-11-25; detaliile marcate „de definit” necesită mini-spec înainte de implementare.

## 1. Model de execuție

- Task este o unitate independentă de execuție, distinctă de `call`, `func` sau `def`.
- Fiecare task are propriul context de execuție și propriul VCX.
- Un task nu citește și nu modifică direct variabilele VCX ale altui task.
- Partajarea explicită a datelor se face prin Channel handle transmis task-ului.
- Handle-urile runtime pentru task, VCX și Channel sunt NUMBER (ID-uri în pool-uri interne), nu un tip VAL nou.
- Schedulerul inițial este cooperativ și rulează pe un singur thread OS. Worker pool-ul este o etapă ulterioară.

API țintă, cu semnături finale încă de aliniat la convențiile native existente:

```flat
ch = task.channel()
channel.set(ch, "counter", 10)
value = channel.var(ch, "counter")

id = task.spawn(#worker, ch, 10)
task.wait(id)
```

Nu introducem `send`/`recv`, message queues sau un model de streaming în acest design. Channel este un container de valori partajate, accesat prin `set` și `var`.

## 2. VCX și Channel sunt responsabilități diferite

- VCX oferă memorie privată task-ului.
- VCX nu este mecanism de sincronizare pentru date partajate.
- Channel este punctul de partajare și trebuie sincronizat.
- `channel.set()` și `channel.var()` urmează semantica de copiere FlatL; LIST/OBJECT trebuie să respecte regulile existente COW/deep-copy.
- Canalele diferite au mutex-uri independente.

## 3. Mutex și race conditions

**Mutex-ul face parte din Channel, nu este un add-on opțional.** Contractul de sincronizare există chiar dacă primul scheduler este cooperativ, pe un singur thread.

### C1 — sincronizare automată per operație

- Fiecare Channel deține propriul mutex.
- `channel.set()` și `channel.var()` obțin și eliberează automat mutex-ul.
- Operația individuală este atomică structural; protejează structura internă împotriva modificării simultane.
- Pentru scrieri concurente pe aceeași cheie, semantica este last-write-wins.
- Această protecție nu face atomică o secvență read-modify-write cu mai multe apeluri.

### C2 — tranzacții explicite

API:

- `channel.lock(ch)` — începe tranzacția exclusivă;
- `channel.unlock(ch)` — commit și eliberează lock-ul;
- `channel.revert(ch)` — anulează modificările tranzacției curente, dar păstrează lock-ul.

Reguli agreate:

- Lock non-reentrant; un al doilea `lock` de către același task este eroare.
- Ownership-ul este `flat_task_id`, nu `pthread_t`, deoarece mai multe task-uri cooperative pot rula pe același thread OS.
- La terminarea task-ului cu tranzacție activă se face automat revert și release, niciodată commit implicit.
- La panic în timpul tranzacției se face automat revert și release.
- Mutex-urile canalelor diferite sunt independente.
- Deadlock detection/timeout nu intră în MVP; lock ordering și evitarea deadlock-urilor trebuie documentate.
- Implementarea tranzacției poate începe cu snapshot și evolua ulterior spre working journal/COW, fără schimbarea contractului public.

## 4. Panic handling pentru block-uri și task-uri

FlatL are deja un mecanism `#panic` asociat block-urilor. Se dorește extinderea lui cu `.any` pentru un handler al oricărui panic produs de block-ul specificat.

Sintaxa agreată:

```flat
#worker
    -- codul de execuție al task-ului
    ...

#panic.#worker.any
    -- prinde orice panic al block-ului #worker
    -- poate inspecta eroarea
    jump #workerErrorHandler

#workerErrorHandler
    -- continuă în contextul aceluiași task
```

Semantica agreată:

1. `#panic.#worker.any` este un block separat asociat block-ului `#worker`, nu un sub-block indentat în `#worker`.
2. Indentarea închide block-urile FlatL; nu se folosește `#end` pentru închiderea unui block.
3. Handler-ul nu reia automat execuția la instrucțiunea care a provocat panic-ul.
4. Handler-ul poate inspecta diagnosticul și poate face `jump` la alt block.
5. Jump-ul de recuperare continuă în contextul aceluiași task și cu același VCX.
6. Dacă nu există handler `.any` aplicabil, se continuă mecanismul general de panic handling existent.
7. `.any` trebuie să fie o extensie generală a mecanismului block panic, nu o funcționalitate specială exclusiv task-urilor.

De verificat în mini-spec: comportamentul când panic-ul apare într-un block apelat din block-ul task-ului, identificarea block-ului de origine, ordinea handler-elor specifice și fallback-ul existent.

Un panic nepreluat marchează task-ul `FAILED`; nu oprește automat schedulerul sau celelalte task-uri.

## 5. Erori și cleanup

- Fiecare task păstrează statusul și panic reason propriu.
- `task.wait(id)` trebuie să permită detectarea și inspectarea eșecului task-ului așteptat.
- Eroarea originală nu trebuie suprascrisă de o eroare secundară apărută în cleanup; eroarea secundară se atașează ca diagnostic.
- Orice resursă deținută de task trebuie curățată determinist la `COMPLETED`, `FAILED` sau, dacă va exista, `CANCELLED`.
- Cleanup-ul unui task nu trebuie să curețe frame-urile, VCX-ul sau resursele altui task.
- Trebuie testate explicit panic în/înainte/după tranzacție, task terminat cu lock activ, panic în handler și eșec în timpul cleanup-ului.

Stări lifecycle țintă: `CREATED`, `RUNNABLE`, `RUNNING`, `WAITING`, `COMPLETED`, `FAILED`; `CANCELLED` rămâne opțional și trebuie specificat înainte de expunerea API-ului.

## 6. Runtime logging și observabilitate

Logging-ul este un mecanism central, opțional, dezactivat implicit.

### Categorii CLI agreate

```bash
flvm program.fl --show-tasks-log
flvm program.fl --show-channel-log
flvm program.fl --show-tasks-log --show-channel-log
```

Numele pot fi ajustate numai dacă se păstrează claritatea și compatibilitatea cu test runner-ul. Categoriile se activează independent.

### Format evenimente

- Format structurat și stabil, lizibil de oameni și ușor de parsat de test runner.
- Fiecare eveniment are `seq` monoton crescător în proces.
- Evenimentele task includ `taskId`; evenimentele Channel includ `chanId` și `taskId` când operația aparține unui task.
- Evenimentele trebuie să descrie lifecycle, scheduler transitions, set/read, lock/commit/revert, panic/handler/jump și cleanup.
- Exemple conceptuale:

```text
{seq: 1, taskId: 3, event: started, block: "#worker1"}
{seq: 2, chanId: 21, taskId: 3, event: set, var: "x", type: "list"}
{seq: 3, chanId: 21, taskId: 4, event: read, var: "x", type: "list"}
```

### Output și memorie

- Output live în `stdout` când opțiunea CLI este activată.
- Logging dezactivat implicit: fără formatare, alocări sau păstrare de istoric când categoria este dezactivată.
- Buffering este opțional și limitat; bufferul plin nu trebuie să crească nelimitat memoria. Se contorizează evenimentele eliminate.
- Bufferul nu este modul implicit; live output este modul implicit pentru `--show-*`.
- API-ul intern central configure/enable/disable trebuie să permită controlul categoriilor. Activarea dinamică în timpul programului este posibilă ca mecanism intern, dar nu este încă API FlatL decis.

### case.toml

Dorim ca testele să poată activa logurile și verifica evenimentele fără să depindă de mesaje arbitrare. Extinderea exactă a schemei este de decis după auditarea runner-ului. Propunere conceptuală:

```toml
[run]
show_tasks_log = true
show_channel_log = true
log_buffer_size = 4096

[expect.runtime]
tasks_completed = 2
active_channel_locks = 0
leaked_task_contexts = 0
```

Pentru verificări precise, preferăm evenimente structurate/snapshot de runtime decât parsarea fragilă a textului de diagnostic.

## 7. Diagnostic helpers țintă

- `task.log(taskId)` — status, block de intrare/curent dacă există, VCX, motiv WAITING, panic reason, lock deținut și cleanup.
- `channel.log(chId)` — ID, reference count, număr chei, lock owner, tranzacție activă, modificări pending și lifecycle.

Acestea sunt helpers de diagnostic propuse; nu sunt încă implementate și nu trebuie confundate cu opțiunile CLI de logging automat.

## 8. Teste

Toate testele funcționale se adaugă în `test/cases/` și se execută prin `bin/run_tests`. Nu se creează teste Python sau runner paralel.

Categorii obligatorii:

- task lifecycle și handle-uri invalide;
- VCX isolation;
- Channel create/set/var și cheie absentă;
- copy semantics pentru NUMBER/STRING/LIST/OBJECT;
- canale independente;
- lock/unlock/revert și read-modify-write;
- lock reentrant, unlock/revert fără ownership și owner greșit;
- panic handler specific, `.any` și fallback general;
- auto-revert și release la panic/terminare;
- un task FAILED nu afectează alte task-uri;
- resource lifecycle, reference count și handle-uri stale;
- worker pool stress/multi-writer după introducerea thread-urilor.

Testele trebuie să verifice rezultate și stări, nu doar exit code `0`. Testele de concurență verifică invariants, fără să presupună o ordine de scheduling fixă.
