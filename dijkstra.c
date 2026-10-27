#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>

#define ITERAZIONI      100000
#define DURATA_FAIRNESS 2     // secondi
#define RITARDO_US      10    // <ritardo> tra due tentativi di LOCK(SX)
#define LAVORO_SC       2000  // lunghezza della sezione critica (demo fairness)
#define MOLTEPLICITA    3     // molteplicità della risorsa (demo competitiva)
#define NUM_FDE         8     // f.d.e. che competono M


/*
Stesso Spin Lock di spinlock.c ma con P e V indivisibili
*/
void LOCK(atomic_flag* SX) {
    while (atomic_flag_test_and_set(SX))   // era rosso: aspetta
        usleep(RITARDO_US);                // <ritardo>
}

void UNLOCK(atomic_flag* SX) {
    atomic_flag_clear(SX);                 // poni il semaforo a verde
}

/*
Simulazione della logica di sospendere il flusso e porlo in stato di pronto
*/
typedef struct descrittore {
    pthread_mutex_t m;
    pthread_cond_t c;
    int pronto;                  // 1 quando una V(S) lo ha risvegliato
    struct descrittore* succ;
} descrittore_fde;


/*
Semaforo di Dijkstra di alto livello, qui S=intero non negativo, 
SX = Spin Lock che garantisce indivisibilità P e V
Qs = Coda FIFO dei flussi in attesa per garantire Fairness
*/
typedef struct {
    int S;
    atomic_flag SX;
    descrittore_fde* testa;      // Qs: si estrae dalla testa...
    descrittore_fde* coda;       // ...e si inserisce in coda (FIFO)
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

/*
Affidamento solo al LOCK(SX),
abilitare e disabilitare le interruzione è un azione che richiede privilegi
*/
void P(semaforo* s) {
    LOCK(&s->SX);
    if (s->S == 0) {
        // sospendi il f.d.e. ed inserisci il suo descrittore nella coda Qs
        descrittore_fde d;
        pthread_mutex_init(&d.m, NULL);
        pthread_cond_init(&d.c, NULL);
        d.pronto = 0;
        accoda(s, &d);
        UNLOCK(&s->SX);
        // Nel nucleo la sospensione e il rilascio di SX sono un'unica operazione
        pthread_mutex_lock(&d.m);
        while (!d.pronto)               // attesa passiva
            pthread_cond_wait(&d.c, &d.m);
        pthread_mutex_unlock(&d.m);
        pthread_mutex_destroy(&d.m);
        pthread_cond_destroy(&d.c);
        return;
    }
    s->S--;
    UNLOCK(&s->SX);
}

void V(semaforo* s) {
    LOCK(&s->SX);
    descrittore_fde* d = estrai(s);
    if (d == NULL)
        s->S++;     // nessun f.d.e. in attesa
    UNLOCK(&s->SX);

    if (d != NULL) {
        pthread_mutex_lock(&d->m);
        d->pronto = 1;
        pthread_cond_signal(&d->c);
        pthread_mutex_unlock(&d->m);
    }
    // Chi esegue la V(S) continua indisturbato
}

/*
Primo DEMO Mutua Esclusione
*/
semaforo SR;
long contatore = 0;

void* worker_pme(void* arg) {
    (void)arg;
    for (int i = 0; i < ITERAZIONI; i++) {
        P(&SR);
        contatore++;    // sezione critica
        V(&SR);
    }
    return NULL;
}

void esegui_pme(void) {
    pthread_t p, q;

    INIZ_SEM(&SR, 1);
    contatore = 0;
    pthread_create(&p, NULL, worker_pme, NULL);
    pthread_create(&q, NULL, worker_pme, NULL);
    pthread_join(p, NULL);
    pthread_join(q, NULL);

    printf("Dijkstra     contatore = %ld (atteso %d) %s\n", contatore,
           2 * ITERAZIONI,
           contatore == 2 * ITERAZIONI ? "OK" : "-> mutua esclusione VIOLATA");
}

/*
Secondo DEMO Fairness Del Semaforo -> Qs FIFO e nessuno flusso resta indietro. FAIR_OR_UNFAIR con semaforo di Dijkstra
*/
atomic_int fine = 0;

void* worker_fairness(void* arg) {
    long* ingressi = arg;
    while (!atomic_load(&fine)) {
        P(&SR);
        (*ingressi)++;  // sezione critica
        for (volatile int k = 0; k < LAVORO_SC; k++)
            ;
        V(&SR);
    }
    return NULL;
}

void esegui_fairness(void) {
    pthread_t p, q;
    long ingressi_p = 0, ingressi_q = 0;

    INIZ_SEM(&SR, 1);
    pthread_create(&p, NULL, worker_fairness, &ingressi_p);
    pthread_create(&q, NULL, worker_fairness, &ingressi_q);
    sleep(DURATA_FAIRNESS);
    atomic_store(&fine, 1);
    pthread_join(p, NULL);
    pthread_join(q, NULL);

    long totale = ingressi_p + ingressi_q;
    printf("Fairness (%ds): P = %ld (%.1f%%), Q = %ld (%.1f%%)\n",
           DURATA_FAIRNESS,
           ingressi_p, 100.0 * ingressi_p / totale,
           ingressi_q, 100.0 * ingressi_q / totale);
}


/*
Terzo DEMO Stile Cooperativo -> Join con i semafori
*/
semaforo Sij;

void* fde_Pi(void* arg) {
    (void)arg;
    sleep(1);
    printf("[Pi] lavoro terminato, eseguo V(Sij)\n");
    V(&Sij);
    return NULL;
}

void esegui_join(void) {
    pthread_t pi;

    INIZ_SEM(&Sij, 0);
    pthread_create(&pi, NULL, fde_Pi, NULL);  // fork Pi
    printf("[Pj] eseguo P(Sij) al posto della join Pi\n");
    P(&Sij);
    printf("[Pj] Pi ha terminato, proseguo\n");
    pthread_detach(pi);
}


/*
Quarto DEMO Stile Competitivo -> risorsa con molt. M, semaforo parte da M. Al massimo M flussi usano la risorsa insieme
*/
semaforo SM;
atomic_int in_uso = 0;
atomic_int max_in_uso = 0;

void* worker_competitivo(void* arg) {
    long id = (long)arg;

    P(&SM);
    int n = atomic_fetch_add(&in_uso, 1) + 1;
    int max = atomic_load(&max_in_uso);
    while (n > max && !atomic_compare_exchange_weak(&max_in_uso, &max, n))
        ;
    printf("[F.d.E. %ld] uso la risorsa (%d/%d in uso)\n", id, n, MOLTEPLICITA);
    usleep(200000);
    atomic_fetch_sub(&in_uso, 1);
    V(&SM);     // la V la esegue lo stesso flusso che ha fatto la P

    return NULL;
}

void esegui_competitivo(void) {
    pthread_t t[NUM_FDE];

    INIZ_SEM(&SM, MOLTEPLICITA);
    for (long i = 0; i < NUM_FDE; i++)
        pthread_create(&t[i], NULL, worker_competitivo, (void*)i);
    for (int i = 0; i < NUM_FDE; i++)
        pthread_join(t[i], NULL);

    printf("Massimo contemporaneo = %d (M = %d) %s\n",
           atomic_load(&max_in_uso), MOLTEPLICITA,
           atomic_load(&max_in_uso) <= MOLTEPLICITA ? "OK" : "-> molteplicità VIOLATA");
}

int main(void) {
    printf("Mutua esclusione\n");
    esegui_pme();

    printf("\nFairness del semaforo di Dijkstra\n");
    esegui_fairness();

    printf("\nStile cooperativo: join con i semafori\n");
    esegui_join();

    printf("\nStile competitivo: risorsa di molteplicità %d\n", MOLTEPLICITA);
    esegui_competitivo();

    return EXIT_SUCCESS;
}

