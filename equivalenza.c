#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#define NUM_FLUSSI      3       // flussi del modello astratto
#define PROFONDITA      10      // lunghezza massima delle sequenze di P e V
#define S_INIZIALE_MAX  3       // si provano i valori iniziali 0..S_INIZIALE_MAX
#define S_PASSO_MAX     20      // induzione per S_A = 0..S_PASSO_MAX

#define ITERAZIONI      50000
#define RITARDO_US      10      // <ritardo> tra due tentativi di LOCK(SX)
#define NUM_FDE         4       // flussi del test con i thread


/*
1: modello ASTRATTO
Stato del Semaoforo (S, Qs). Un flusso sospeso non esegue più nulla finché una V non lo sveglia.
*/
typedef struct {
    int S;
    int n;                      // = |Qs| -> Lunghezza della coda
    int Q[NUM_FLUSSI];          // Qs: id dei flussi sospesi, in ordine FIFO
} modello;

#define PROSEGUE  -1            // P: il flusso prosegue
#define SOSPESO   -2            // P: il flusso viene sospeso
#define NESSUNO   -3            // V: nessun flusso svegliato

static void accoda_mod(modello* m, int f) {
    m->Q[m->n++] = f;
}

static int estrai_mod(modello* m) {
    if (m->n == 0)
        return NESSUNO;
    int f = m->Q[0];
    m->n--;
    memmove(&m->Q[0], &m->Q[1], m->n * sizeof(int));
    return f;
}

// A
static int PA_mod(modello* m, int f) {
    if (m->S == 0) {
        accoda_mod(m, f);
        return SOSPESO;
    }
    m->S--;
    return PROSEGUE;
}

static int VA_mod(modello* m) {
    if (m->n > 0)
        return estrai_mod(m);
    m->S++;
    return NESSUNO;
}

// B
static int PB_mod(modello* m, int f) {
    m->S--;
    if (m->S < 0) {
        accoda_mod(m, f);
        return SOSPESO;
    }
    return PROSEGUE;
}

static int VB_mod(modello* m) {
    m->S++;
    if (m->S <= 0)
        return estrai_mod(m);
    return NESSUNO;
}

typedef struct {
    char op;                    // 'P' || 'V'
    int f;
} passo;

static int applica_A(modello* m, passo p) {
    return p.op == 'P' ? PA_mod(m, p.f) : VA_mod(m);
}

static int applica_B(modello* m, passo p) {
    return p.op == 'P' ? PB_mod(m, p.f) : VB_mod(m);
}

static int in_relazione(const modello* a, const modello* b) {
    if (a->S < 0 || (a->S > 0 && a->n > 0))     // (I_A)
        return 0;
    if (b->S != a->S - a->n)                    // (R)
        return 0;
    if (b->n != (b->S < 0 ? -b->S : 0))         // in B: S < 0 -> -S = |Qs|
        return 0;
    return a->n == b->n && memcmp(a->Q, b->Q, a->n * sizeof(int)) == 0;   // stessa coda
}

// Esegue p su A e su B: 1 se l'esito è lo stesso e gli stati restano in relazione
static int stesso_passo(modello* a, modello* b, passo p, int* esito) {
    *esito = applica_A(a, p);
    return applica_B(b, p) == *esito && in_relazione(a, b);
}

/*
Passo Induttivo: PER OGNI coppia di stati in relazione con S_A = 0..S_PASSO_MAX
e NUM_FLUSSI flussi (raggiungibile o meno entro PROFONDITA) ogni primitiva di
ogni flusso non sospeso dà lo stesso esito e porta a una coppia ancora in
relazione.
Per (I_A) gli stati di A sono: S_A > 0 con la coda vuota, oppure S_A = 0 con
una coda qualunque. Lo stato di B è fissato da (R).
*/
static long passi_induttivi;

static int passo_induttivo(modello a, unsigned sospesi) {
    modello b = a;
    b.S = a.S - a.n;

    for (int f = 0; f < NUM_FLUSSI; f++) {
        if (sospesi & (1u << f))
            continue;
        for (int k = 0; k < 2; k++) {
            modello a2 = a, b2 = b;
            passo p = { "PV"[k], f };
            int esito;
            passi_induttivi++;
            if (!stesso_passo(&a2, &b2, p, &esito)) {
                printf("  VIOLATO da S_A = %d, |Qs| = %d con f%d: %c(S)\n",
                       a.S, a.n, f, p.op);
                return 0;
            }
        }
    }

    // con S_A = 0 la coda può contenere un flusso in più
    if (a.S == 0) {
        for (int f = 0; f < NUM_FLUSSI; f++) {
            if (sospesi & (1u << f))
                continue;
            modello a2 = a;
            accoda_mod(&a2, f);
            if (!passo_induttivo(a2, sospesi | (1u << f)))
                return 0;
        }
    }
    return 1;
}

static int verifica_passo_induttivo(void) {
    for (int s = 0; s <= S_PASSO_MAX; s++) {
        modello a = { .S = s, .n = 0 };
        if (!passo_induttivo(a, 0))
            return 0;
    }
    return 1;
}

/*
Equivalenza Semantica: dallo stato iniziale prova ogni sequenza di primitive
lunga fino a PROFONDITA (a ogni passo un qualunque flusso non sospeso esegue
una P o una V). Restituisce 0 se A e B non si distinguono mai
*/
static passo traccia[PROFONDITA];
static long passi_confrontati;

static int esplora(modello a, modello b, unsigned sospesi, int prof) {
    if (prof == PROFONDITA)
        return 0;
    for (int f = 0; f < NUM_FLUSSI; f++) {
        if (sospesi & (1u << f))
            continue;
        for (int k = 0; k < 2; k++) {
            modello a2 = a, b2 = b;
            passo p = { "PV"[k], f };
            int esito;
            traccia[prof] = p;
            passi_confrontati++;
            if (!stesso_passo(&a2, &b2, p, &esito))
                return prof + 1;

            // stesso esito e stessa coda: i flussi sospesi sono gli stessi in A e in B
            unsigned sospesi2 = sospesi;
            if (esito == SOSPESO)
                sospesi2 |= 1u << f;
            else if (esito >= 0)
                sospesi2 &= ~(1u << esito);

            int r = esplora(a2, b2, sospesi2, prof + 1);
            if (r != 0)
                return r;
        }
    }
    return 0;
}

// Come esplora, per ogni valore iniziale del semaforo
static int verifica_sequenze(int* s0) {
    for (int s = 0; s <= S_INIZIALE_MAX; s++) {
        modello iniziale = { .S = s, .n = 0 };
        int r = esplora(iniziale, iniziale, 0, 0);
        if (r != 0) {
            *s0 = s;
            return r;
        }
    }
    return 0;
}

static const char* descrivi(int esito, char* buf, size_t dim) {
    if (esito == PROSEGUE)
        snprintf(buf, dim, "prosegue");
    else if (esito == SOSPESO)
        snprintf(buf, dim, "sospeso");
    else if (esito == NESSUNO)
        snprintf(buf, dim, "nessun risveglio");
    else
        snprintf(buf, dim, "sveglia f%d", esito);
    return buf;
}

// Riesegue una sequenza di primitive su A e su B mostrando esiti e stati affiancati
static void mostra_traccia(int s0, const passo* passi, int n) {
    modello a = { .S = s0, .n = 0 }, b = a;
    char ba[32], bb[32];

    printf("  %-10s | %-17s %2s %4s | %-17s %2s %4s\n", "passo",
           "A", "S", "|Qs|", "B", "S", "|Qs|");
    printf("  %-10s | %-17s %2d %4d | %-17s %2d %4d\n", "inizio",
           "", a.S, a.n, "", b.S, b.n);
    for (int i = 0; i < n; i++) {
        int esito_a = applica_A(&a, passi[i]);
        int esito_b = applica_B(&b, passi[i]);
        char op[16];
        snprintf(op, sizeof op, "f%d: %c(S)", passi[i].f, passi[i].op);
        printf("  %-10s | %-17s %2d %4d | %-17s %2d %4d%s\n", op,
               descrivi(esito_a, ba, sizeof ba), a.S, a.n,
               descrivi(esito_b, bb, sizeof bb), b.S, b.n,
               esito_a != esito_b ? "   <- DIVERSI" : "");
    }
}

 //exec:
static void esegui_modello(void) {
    printf("Esempio: S iniziale 1, tre P e poi tre V\n");
    const passo esempio[] = {
        { 'P', 0 }, { 'P', 1 }, { 'P', 2 }, { 'V', 0 }, { 'V', 0 }, { 'V', 0 },
    };
    mostra_traccia(1, esempio, 6);

    printf("\nPasso induttivo: ogni coppia di stati in relazione con S_A = 0..%d "
           "e ogni coda di al più %d flussi\n", S_PASSO_MAX, NUM_FLUSSI);
    passi_induttivi = 0;
    if (verifica_passo_induttivo())
        printf("  %ld passi: stesso esito e stati ancora in relazione "
               "-> (R) SI CONSERVA\n", passi_induttivi);

    printf("\nEquivalenza semantica: %d flussi, S iniziale 0..%d, "
           "ogni sequenza di P e V lunga fino a %d\n",
           NUM_FLUSSI, S_INIZIALE_MAX, PROFONDITA);
    int s0 = 0;
    passi_confrontati = 0;
    int r = verifica_sequenze(&s0);
    if (r == 0) {
        printf("  %ld passi confrontati: stessi esiti in A e in B "
               "-> EQUIVALENTI\n", passi_confrontati);
    } else {
        printf("  A e B: DIVERSE, controesempio con S iniziale %d\n", s0);
        mostra_traccia(s0, traccia, r);
    }
}


/*
2: thread reali
Stesso schema di dijkstra: S, coda FIFO Qs, spin lock SX per atomicità
*/
void crea_thread(pthread_t* t, void* (*fn)(void*), void* arg) {
    int err = pthread_create(t, NULL, fn, arg);
    if (err != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(err));
        exit(EXIT_FAILURE);
    }
}

void LOCK(atomic_flag* SX) {
    while (atomic_flag_test_and_set(SX))   // era rosso: aspetta
        usleep(RITARDO_US);                // <ritardo>
}

void UNLOCK(atomic_flag* SX) {
    atomic_flag_clear(SX);                 // poni il semaforo a verde
}

typedef struct descrittore {
    pthread_mutex_t m;
    pthread_cond_t c;
    int pronto;                  // 1 quando una V(S) lo ha risvegliato
    struct descrittore* succ;
} descrittore_fde;

typedef struct {
    int S;                       // A: sempre >= 0.  B: se < 0, -S flussi in coda
    atomic_flag SX;
    descrittore_fde* testa;
    descrittore_fde* coda;
} semaforo;

void INIZ_SEM(semaforo* s, int valore) {
    s->S = valore;
    atomic_flag_clear(&s->SX);
    s->testa = s->coda = NULL;
}

static void accoda(semaforo* s, descrittore_fde* d) {
    d->succ = NULL;
    if (s->coda == NULL)
        s->testa = d;
    else
        s->coda->succ = d;
    s->coda = d;
}

static descrittore_fde* estrai(semaforo* s) {
    descrittore_fde* d = s->testa;
    if (d != NULL) {
        s->testa = d->succ;
        if (s->testa == NULL)
            s->coda = NULL;
    }
    return d;
}

// <<sospendi il flusso e inseriscilo in Qs>> Si entra con SX GIÀ PRESO, si esce con SX rilasciato
static void sospendi(semaforo* s) {
    descrittore_fde d;
    pthread_mutex_init(&d.m, NULL);
    pthread_cond_init(&d.c, NULL);
    d.pronto = 0;
    accoda(s, &d);
    UNLOCK(&s->SX);

    pthread_mutex_lock(&d.m);
    while (!d.pronto)               // attesa PASSIVA
        pthread_cond_wait(&d.c, &d.m);
    pthread_mutex_unlock(&d.m);
    pthread_mutex_destroy(&d.m);
    pthread_cond_destroy(&d.c);
}

// <<poni il flusso in stato di pronto>>
static void risveglia(descrittore_fde* d) {
    if (d == NULL)
        return;
    pthread_mutex_lock(&d->m);
    d->pronto = 1;
    pthread_cond_signal(&d->c);
    pthread_mutex_unlock(&d->m);
}

// A (P e V come dijkstra.c)
void P_A(semaforo* s) {
    LOCK(&s->SX);
    if (s->S == 0) {
        sospendi(s);
        return;
    }
    s->S--;
    UNLOCK(&s->SX);
}

void V_A(semaforo* s) {
    LOCK(&s->SX);
    descrittore_fde* d = estrai(s);
    if (d == NULL)
        s->S++;
    UNLOCK(&s->SX);
    risveglia(d);
}

// Definizione B
void P_B(semaforo* s) {
    LOCK(&s->SX);
    s->S--;
    if (s->S < 0) {
        sospendi(s);
        return;
    }
    UNLOCK(&s->SX);
}

void V_B(semaforo* s) {
    LOCK(&s->SX);
    descrittore_fde* d = NULL;
    s->S++;
    if (s->S <= 0)
        d = estrai(s);
    UNLOCK(&s->SX);
    risveglia(d);
}

typedef struct {
    const char* nome;
    void (*P)(semaforo*);
    void (*V)(semaforo*);
} primitive;

static const primitive PRIM_A = { "A", P_A, V_A };
static const primitive PRIM_B = { "B", P_B, V_B };

/*
Stesso test su A e su B (concorrenza): risorsa di molteplicità M contesa da
NUM_FDE flussi (con M = 1 è la mutua esclusione). Al massimo M la usano insieme
e alla fine il semaforo torna a M con la coda vuota.
*/
static semaforo SM;
static const primitive* in_prova;
static atomic_int in_uso;
static atomic_int max_in_uso;

void* worker(void* arg) {
    (void)arg;
    for (int i = 0; i < ITERAZIONI; i++) {
        in_prova->P(&SM);
        int n = atomic_fetch_add(&in_uso, 1) + 1;
        int max = atomic_load(&max_in_uso);
        while (n > max && !atomic_compare_exchange_weak(&max_in_uso, &max, n))
            ;
        atomic_fetch_sub(&in_uso, 1);
        in_prova->V(&SM);
    }
    return NULL;
}

void esegui_thread(const primitive* x, int M) {
    pthread_t t[NUM_FDE];

    INIZ_SEM(&SM, M);
    in_prova = x;
    atomic_store(&in_uso, 0);
    atomic_store(&max_in_uso, 0);
    for (int i = 0; i < NUM_FDE; i++)
        crea_thread(&t[i], worker, NULL);
    for (int i = 0; i < NUM_FDE; i++)
        pthread_join(t[i], NULL);

    int max = atomic_load(&max_in_uso);
    printf("  %s, M = %d: massimo contemporaneo = %d, S finale = %d %s\n",
           x->nome, M, max, SM.S,
           max <= M && SM.S == M && SM.testa == NULL ? "OK" : "-> VIOLATA");
}

int main(void) {
    esegui_modello();

    printf("\nThread reali: %d flussi, %d P/V ciascuno\n", NUM_FDE, ITERAZIONI);
    esegui_thread(&PRIM_A, 1);
    esegui_thread(&PRIM_B, 1);
    esegui_thread(&PRIM_A, 3);
    esegui_thread(&PRIM_B, 3);

    return EXIT_SUCCESS;
}
