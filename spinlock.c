#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>

#define ITERAZIONI     1000000
#define DURATA_FAIRNESS 2   // secondi
#define RITARDO_US      10  // <ritardo> tra due tentativi di LOCK
#define LAVORO_SC       2000 // lunghezza sezione critica

// Risorsa condivisa
long contatore = 0;

/*
Spin Lock (1): LOCK/UNLOCK naive su un bit X
X = 1 verde (libero), X = 0 rosso (occupato)
Test e Assegnazione non sono divisibili, due thread possono trovare X = 1 ed essere entrambi in sezione critica
*/

volatile int X = 1;

void lock_naive(void) {
    while (X != 1)      // ciclo di attesa attiva
        ;
    // <- è qui che può avvenire l'interleaving! (o l'altro processore)
    X = 0;              // poni il semaforo a rosso
}

void unlock_naive(void) {
    X = 1;              // poni il semaforo a verde
}

/*
Spin Lock con TestAndSet
atomic_flag_test_and_set serve all'indivisibilità
qui set = ROSSO
*/
atomic_flag SX = ATOMIC_FLAG_INIT;

void lock_tas(void) {
    while (atomic_flag_test_and_set(&SX))  // era rosso quindi aspetta
        usleep(RITARDO_US);                // <ritardo>
    // qui il semaforo è già a rosso
}

void unlock_tas(void) {
    atomic_flag_clear(&SX);                // poni il semaforo a verde
}


//Mutua Esclusione su contatore condiviso
void* worker_naive(void* arg) {
    (void)arg;
    for (int i = 0; i < ITERAZIONI; i++) {
        lock_naive();
        contatore++;    // sezione critica
        unlock_naive();
    }
    return NULL;
}

void* worker_tas(void* arg) {
    (void)arg;
    for (int i = 0; i < ITERAZIONI; i++) {
        lock_tas();
        contatore++;    // sezione critica
        unlock_tas();
    }
    return NULL;
}

void esegui_contatore(const char* nome, void* (*worker)(void*)) {
    pthread_t p, q;

    contatore = 0;
    pthread_create(&p, NULL, worker, NULL);
    pthread_create(&q, NULL, worker, NULL);
    pthread_join(p, NULL);
    pthread_join(q, NULL);

    printf("%-12s contatore = %ld (atteso %d) %s\n", nome, contatore,
           2 * ITERAZIONI,
           contatore == 2 * ITERAZIONI ? "OK" : "-> Prima condizione (mutua esclusione) VIOLATA");
}

atomic_int fine = 0;

void* worker_fairness(void* arg) {
    long* ingressi = arg;
    while (!atomic_load(&fine)) {
        lock_tas();
        (*ingressi)++;  // sezione critica
        for (volatile int k = 0; k < LAVORO_SC; k++)
            ;
        unlock_tas();
    }
    return NULL;
}

void esegui_fairness(void) {
    pthread_t p, q;
    long ingressi_p = 0, ingressi_q = 0;

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

int main(void) {
    printf("Mutua esclusione\n");
    esegui_contatore("Naive", worker_naive);
    esegui_contatore("TestAndSet", worker_tas);

    printf("\nFairness dello spin lock TestAndSet\n");
    esegui_fairness();

    return EXIT_SUCCESS;
}

