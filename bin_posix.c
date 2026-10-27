#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>

// Nome del semaforo POSIX (su macOS sem_init non è implementata, quindi si usano i semafori con nome: sem_open/sem_close/sem_unlink)
#define SEM_NAME "/semaforo_binario"

sem_t *mutex;

void* worker(void* arg) {
    long id = (long)arg;

    // wait / decremento
    sem_wait(mutex);

    // Sezione Critica
    printf("[Thread %ld] Entrato nella sezione critica.\n", id);
    sleep(1);
    printf("[Thread %ld] In uscita dalla sezione critica.\n", id);

    // post / incremento
    sem_post(mutex);

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

    pthread_create(&t1, NULL, worker, (void*)1);
    pthread_create(&t2, NULL, worker, (void*)2);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    // Chiusura e rimozione semaforo
    sem_close(mutex);
    sem_unlink(SEM_NAME);

    return EXIT_SUCCESS;
}

