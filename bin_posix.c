#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

// Nome del semaforo POSIX (su macOS sem_init non è implementata, quindi si usano i semafori con nome: sem_open/sem_close/sem_unlink)
#define SEM_NAME "/semaforo_binario"

sem_t *mutex;


void crea_thread(pthread_t* t, void* (*fn)(void*), void* arg) {
    int err = pthread_create(t, NULL, fn, arg);
    if (err != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(err));
        exit(EXIT_FAILURE);
    }
}

void* worker(void* arg) {
    long id = (long)arg;

    // wait / decremento (se interrotta da un segnale, EINTR, si riprova)
    while (sem_wait(mutex) == -1) {
        if (errno != EINTR) {
            perror("sem_wait");
            exit(EXIT_FAILURE);
        }
    }

    // Sezione Critica
    printf("[Thread %ld] Entrato nella sezione critica.\n", id);
    sleep(1);
    printf("[Thread %ld] In uscita dalla sezione critica.\n", id);

    // post / incremento
    if (sem_post(mutex) == -1) {
        perror("sem_post");
        exit(EXIT_FAILURE);
    }

    return NULL;
}

int main(void) {
    pthread_t t1, t2;

    // Qualora fosse rimasto un semaforo dall'esecuzioe precedente lo toglie
    sem_unlink(SEM_NAME);

    // value = 1 (semaforo binario)
    mutex = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0644, 1);
    if (mutex == SEM_FAILED) {
        perror("Errore nell'inizializzazione del semaforo");
        return EXIT_FAILURE;
    }

    crea_thread(&t1, worker, (void*)1);
    crea_thread(&t2, worker, (void*)2);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    // Chiusura e rimozione semaforo
    sem_close(mutex);
    sem_unlink(SEM_NAME);

    return EXIT_SUCCESS;
}

