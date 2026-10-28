# Implementazione in C Semaforo Binario POSIX

<p align="center">
    Prima implementazione dei semafori binari in C e spin lock.
</p>

## File

- `spinlock.c`: spin lock naive e con TestAndSet, mutua esclusione e fairness.
- `bin_posix.c`: semaforo binario con i semafori POSIX con nome.
- `dijkstra.c`: semaforo di Dijkstra secondo lo schema di implementazione
  delle slide (S, coda FIFO Qs, spin lock SX), con quattro demo: mutua
  esclusione, fairness, join in stile cooperativo e risorsa di molteplicità M
  in stile competitivo.
- `DIJKSTRA.md`: guida dallo pseudocodice al codice C.

## Compilazione

```sh
cc -Wall -pthread dijkstra.c -o dijkstra && ./dijkstra
```

