# FlatL — Task Runtime & Channels plans

Acest director păstrează deciziile de arhitectură, direcția și ordinea de implementare pentru Task Runtime, Channel și viitoarea integrare UI. Scopul este ca lucrul să poată continua într-o conversație nouă fără să depindă de istoricul chatului.

## Documente

- [01 — Decizii și contracte de design](01_DECISIONS.md): ce am agreat explicit și ce nu trebuie schimbat accidental.
- [02 — Roadmap și fazare](02_ROADMAP.md): milestones v1.0, v1.5, v2.0 și v2.5, livrabile și criterii de acceptare.
- [03 — Audit inițial al runtime-ului](03_RUNTIME_AUDIT.md): constatările tehnice din arhiva 57 și limitele arhitecturii actuale.
- [04 — Stadiul implementării](04_IMPLEMENTATION_STATUS.md): ce există în arhiva curentă, ce nu este încă implementat și următorul pas.
- [05 — Roadmap original](05_SOURCE_ROADMAP.md): copia planului inițial furnizat pentru Task Channels, păstrată pentru trasabilitate.

## Reguli pentru continuarea lucrului

1. Citește acest director înainte de orice modificare majoră la Task Runtime.
2. Verifică sursele curente; documentele descriu intenția și constatările la momentul arhivei indicate, nu înlocuiesc auditul codului actual.
3. Păstrează API-urile și semantica marcate ca decizii agreate. Orice schimbare trebuie documentată aici.
4. Nu marca o fază completă doar pe baza build-ului: sunt necesare teste pozitive, negative, panic/cleanup și verificări de resurse.
5. Testele funcționale se integrează în `test/cases/` și se execută prin `bin/run_tests`.
6. Nu introduce Python-based tests sau runner separat.
7. Nu implementa worker threads înainte ca starea VM/executor/native runtime să fie izolată per execuție.

**Baseline al acestui set de documente:** arhiva `flatL-main-251-task-runtime-foundation.zip`, derivată din `flatL-main(57).zip`.
