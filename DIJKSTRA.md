# Dallo pseudocodice al C: semafori di Dijkstra

Miniguida al passaggio dallo pseudocodice
al codice di `dijkstra.c`.

## Librerie usate

| Header          | Cosa fornisce                                         | Riferimento |
|-----------------|-------------------------------------------------------|-------------|
| `<pthread.h>`   | thread, mutex, condition variable                     | `man pthread`, `man pthread_create`, `man pthread_mutex_lock`, `man pthread_cond_wait` |
| `<stdatomic.h>` | `atomic_flag` (TestAndSet), `atomic_int`              | C11 §7.17, [cppreference: atomic_flag](https://en.cppreference.com/w/c/atomic/atomic_flag) |
| `<unistd.h>`    | `usleep`, `sleep`                                     | `man 3 usleep`, `man 3 sleep` |

Compilazione: `cc -Wall -pthread dijkstra.c -o dijkstra`

## Corrispondenze generali

| Pseudocodice                         | C                                                       |
|--------------------------------------|---------------------------------------------------------|
| `concurrent procedure P`             | funzione `void* P(void* arg)` eseguita da un thread     |
| `cobegin P \|\| Q coend`             | `pthread_create(&p, ...)`, `pthread_create(&q, ...)`, poi `pthread_join` su entrambi |
| f.d.e. (flusso di esecuzione)        | thread POSIX (`pthread_t`)                              |
| `<sezione critica>`                  | accesso alla variabile condivisa (`contatore++`)        |
| `<disabilita/abilita interruzioni>`  | **non disponibile in spazio utente** (vedi sotto)       |

## 1. Semaforo di basso livello: `X`, LOCK e UNLOCK

Il bit `X` (1 verde, 0 rosso) diventa un `atomic_flag`. Il flag "set" significa rosso.

```
LOCK(X):                                void LOCK(atomic_flag* SX) {
  loop begin                                while (atomic_flag_test_and_set(SX))
    TestAndSet(X);                              usleep(RITARDO_US);
    if (X era 1) exit;                  }
    <ritardo>
  end
UNLOCK(X):                              void UNLOCK(atomic_flag* SX) {
  Set(X);                                   atomic_flag_clear(SX);
                                        }
```

- `atomic_flag_test_and_set(f)`: imposta il flag in modo indivisibile e
  restituisce il valore precedente -> l'istruzione macchina TestAndSet.
  Se `true` il semaforo era già rosso e si riprova.
- `atomic_flag_clear(f)` sarebbe `Set(X)`: rimette il semaforo a verde.
- `ATOMIC_FLAG_INIT` (o `atomic_flag_clear` in caso di flag nuovo) inizializza il
  flag a verde.
- `<ritardo>` diventa `usleep(µs)`, riduce lo spreco di CPU durante
  l'attesa attiva.

La versione naive con `volatile int X` (`spinlock.c`, `lock_naive`) mostra l'importanza dell'indivisibilità.

## 2. Il semaforo: `S`, `Qs`, `SX`

Si associa a ogni semaforo un intero `S`, una coda `Qs` di f.d.e. in
attesa e uno spin lock `SX` che rende indivisibili P e V:

```c
typedef struct {
    int S;                     // valore del semaforo
    atomic_flag SX;            // spin lock per l'indivisibilità
    descrittore_fde* testa;    // coda Qs: estrazione dalla testa
    descrittore_fde* coda;     // inserimento in coda 
} semaforo;
```

- `Qs` è una lista concatenata di descrittori. La FIFO
  (`accoda` in coda, `estrai` dalla testa) è ciò che garantisce la **fairness**.
- **`INIZ_SEM(S, v)`** diventa `INIZ_SEM(semaforo* s, int valore)`: assegna `S`,
  mette `SX` a verde e svuota la coda.

### Il descrittore di f.d.e.

"sospendere" o "porre in stato di pronto" sono compiti dello scheduler. In spazio utente si simula con una coppia mutex/condition variable 
per ogni f.d.e. in attesa. In C:

```c
typedef struct descrittore {
    pthread_mutex_t m;
    pthread_cond_t c;
    int pronto;                // 1 quando una V lo ha risvegliato
    struct descrittore* succ;
} descrittore_fde;
```

| Operazione del nucleo             | Funzione POSIX                                          | man |
|-----------------------------------|---------------------------------------------------------|-----|
| crea il descrittore               | `pthread_mutex_init`, `pthread_cond_init`               | `man pthread_mutex_init`, `man pthread_cond_init` |
| sospendi il f.d.e.                | `pthread_cond_wait(&c, &m)` in un ciclo `while (!pronto)` | `man pthread_cond_wait` |
| poni il f.d.e. in stato di pronto | `pronto = 1; pthread_cond_signal(&c)`                   | `man pthread_cond_signal` |
| distruggi il descrittore          | `pthread_mutex_destroy`, `pthread_cond_destroy`         | `man pthread_mutex_destroy` |

Il `while` attorno a `pthread_cond_wait` serve perché sono ammessi risvegli spuri (vedi `man pthread_cond_wait`).

## 3. Primitiva P(S)

```
P(S): begin                                 void P(semaforo* s) {
  <disabilita le interruzioni>                  // non disponibile
  LOCK(SX)                                      LOCK(&s->SX);
  if S=0 then                                   if (s->S == 0) {
    <<sospendi il f.d.e. ed                         descrittore_fde d; /* init */
      inserisci il suo descrittore                  accoda(s, &d);
      nella coda Qs>>                               UNLOCK(&s->SX);
                                                    /* while (!d.pronto)
                                                         pthread_cond_wait */
                                                    return;
                                                }
  else S ← S – 1;                               s->S--;
  UNLOCK(SX)                                    UNLOCK(&s->SX);
  <abilita le interruzioni>                 }
end
```

Differenza: nel nucleo la sospensione e il rilascio di `SX` avvengono insieme. 
Bisogna rilasciare `SX` *prima* di sospendere,altrimenti nessuna V potrebbe entrare e il sistema andrebbe in stallo. 
Se una V arriva tra `UNLOCK` e `pthread_cond_wait`, imposta `pronto = 1` sotto il mutex del descrittore: 
il f.d.e. lo trova già a 1, non si sospende e il risveglio non si perde.

## 4. Primitiva V(S)

```
V(S): begin                                 void V(semaforo* s) {
  <disabilita le interruzioni>                  // non disponibile
  LOCK(SX)                                      LOCK(&s->SX);
  if Qs non è vuota then                        descrittore_fde* d = estrai(s);
    <<estrai un descrittore da Qs>>             if (d == NULL)
  else S ← S + 1;                                   s->S++;
  UNLOCK(SX)                                    UNLOCK(&s->SX);
                                                if (d != NULL) {
    <<poni il f.d.e. in stato                       pthread_mutex_lock(&d->m);
      di pronto>>                                   d->pronto = 1;
                                                    pthread_cond_signal(&d->c);
                                                    pthread_mutex_unlock(&d->m);
                                                }
  <abilita le interruzioni>                 }
end
```

- Se qualcuno è in attesa, `S` non viene incrementato. Il f.d.e. risvegliato esce dalla P senza decrementare, quindi il semaforo passa direttamente da
  chi fa la V a chi era in coda ("passaggio del testimone"). Per questo nessun f.d.e. può "rubare" il semaforo a chi è in coda.
- Il risveglio avviene fuori da `SX`: il descrittore è già stato tolto da `Qs`, quindi solo questa V lo conosce, e `SX` resta occupato il meno possibile.

## 5. Interruzioni

| Sistema         | Disabilitare le interruzioni   | LOCK(SX)/UNLOCK(SX) |
|-----------------|--------------------------------|---------------------|
| Uni-processore  | necessarie                     | superflue           |
| Multi-processore| convenienti, non indispensabili | necessarie          |

Un processo utente NON può disabilitare le interruzioni (servono istruzioni privilegiate). 
Se succedesse gli altri f.d.e. aspettano attivamente sulla`LOCK`
In `dijkstra.c` l'indivisibilità poggia quindi solo su `SX`. 

## 6. Dai programmi alle demo

| Programma nelle slide                    | Funzione in `dijkstra.c` | Inizializzazione        |
|------------------------------------------|--------------------------|-------------------------|
| `PME` (mutua esclusione)                 | `esegui_pme`             | `INIZ_SEM(&SR, 1)`      |
| `FAIR_OR_UNFAIR`                         | `esegui_fairness`        | `INIZ_SEM(&SR, 1)`      |
| join con semaforo `Sij` (cooperativo)    | `esegui_join`            | `INIZ_SEM(&Sij, 0)`     |
| risorsa di molteplicità M (competitivo)  | `esegui_competitivo`     | `INIZ_SEM(&SM, M)`      |

## 7. Confronto con i semafori POSIX

Le stesse primitive esistono già pronte in `<semaphore.h>` (`binario_posix.c`):

| Dijkstra      | POSIX                                 | man |
|---------------|---------------------------------------|-----|
| `INIZ_SEM`    | `sem_init` / `sem_open`               | `man sem_open` |
| `P(S)`        | `sem_wait`                            | `man sem_wait` |
| `V(S)`        | `sem_post`                            | `man sem_post` |
| —             | `sem_close`, `sem_unlink`, `sem_destroy` | `man sem_unlink` |

Su macOS `sem_init` non è implementata (restituisce `ENOSYS`), quindi si usano
i semafori con nome. POSIX inoltre non garantisce l'ordine FIFO dei risvegli,
che invece `dijkstra.c` assicura con `Qs`.


