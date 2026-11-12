# Semafori in C

## File

- `spinlock.c`: spin lock naive e con TestAndSet, mutua esclusione e fairness.
- `bin_posix.c`: semaforo binario con i semafori POSIX con nome.
- `dijkstra.c`: semaforo di Dijkstra secondo lo schema (S, coda FIFO Qs, spin lock SX), con quattro demo: mutua
  esclusione, fairness, join in stile cooperativo e risorsa di molteplicità M
  in stile competitivo.
- `DIJKSTRA.md`: guida dallo pseudocodice al C.
- `equivalenza.c`: Dimostrazione di S < 0 -> -S = |Qs| ed Equivalenza Semantica tra due definizioni di semafori

## Dimostrazione Equivalenza
Due definizioni (A, B) del semaforo di Dijkstra a confronto

```
A:  P(S): if S = 0 then <<sospendi il flusso e inseriscilo in Qs>>
          else S <- S - 1
    V(S): if Qs non è vuota then <<sveglia un flusso di Qs>>
          else S <- S + 1

B:  P(S): S <- S - 1
          if S < 0 then <<sospendi il flusso e inseriscilo in Qs>>
    V(S): S <- S + 1
          if S <= 0 then <<sveglia un flusso di Qs>>
```

### Cosa significa "equivalenti"

Di un semaforo un flusso osserva solo l'**esito** delle primitive: se la sua P
prosegue o lo sospende, e quale flusso viene svegliato da una V. A e B sono
semanticamente equivalenti se, a partire dallo stesso valore iniziale, ogni
sequenza di P e V dà gli stessi esiti in entrambe.

Il valore di S non è un esito ma lo stato interno, ed è proprio lì che le due
differiscono: in A è sempre S >= 0 (S conta i "permessi" liberi), in B S può
scendere sotto zero.

Ipotesi: P e V indivisibili (quindi ogni esecuzione concorrente è una sequenza
di primitive), S iniziale >= 0, stesso criterio di estrazione da Qs (qui FIFO
in entrambe).

### Invarianti

```
(I_A)  in A: S_A >= 0, e se S_A > 0 la coda è vuota
(R)    S_B = S_A - |Qs|, con la stessa coda Qs
```

Da (I_A) e (R) segue la proprietà di B: **se S_B < 0 allora -S_B = |Qs|**.
Infatti S_B < 0 dà |Qs| > S_A >= 0, quindi la coda non è vuota, quindi per
(I_A) S_A = 0, quindi S_B = -|Qs|. Allo stesso modo, se S_B >= 0 la coda è
vuota e S_B = S_A.

### Passo base

All'inizio S_A = S_B = valore iniziale >= 0 e la coda è vuota: (I_A) e (R)
valgono.

### Passo induttivo

Si suppone che (I_A) e (R) valgano prima di una primitiva. Per (I_A) i casi
sono solo quattro: una P trova S_A > 0 oppure S_A = 0, una V trova la coda
non vuota oppure vuota.

| Caso | Prima | A | B | Dopo, (R) |
|------|-------|---|---|-----------|
| P, S_A > 0 | Qs vuota, S_B = S_A | S_A - 1, prosegue | S_B - 1 >= 0, prosegue | S_B - 1 = (S_A - 1) - 0 |
| P, S_A = 0 | S_B = -\|Qs\| | sospende e accoda | S_B - 1 < 0, sospende e accoda | S_B - 1 = 0 - (\|Qs\| + 1) |
| V, Qs non vuota | S_A = 0, S_B = -\|Qs\| < 0 | estrae e sveglia | S_B + 1 <= 0, estrae e sveglia | S_B + 1 = 0 - (\|Qs\| - 1) |
| V, Qs vuota | S_B = S_A | S_A + 1, nessun risveglio | S_B + 1 > 0, nessun risveglio | S_B + 1 = (S_A + 1) - 0 |

In ogni caso:

- l'esito è lo stesso in A e in B;
- la coda resta la stessa: viene accodato lo stesso flusso e, con lo stesso
  criterio sulla stessa coda, estratto lo stesso flusso;
- (R) vale ancora (ultima colonna);
- (I_A) vale ancora: S_A cala solo se era > 0, la coda cresce solo con
  S_A = 0, S_A cresce solo con la coda vuota.

### Conclusione

Per induzione sulla lunghezza della sequenza, (I_A) e (R) valgono dopo ogni
sequenza di P e V e ogni primitiva ha lo stesso esito in A e in B. Avendo la
stessa coda, i flussi sospesi sono gli stessi, quindi a ogni passo le
primitive eseguibili sono le stesse. A e B sono semanticamente equivalenti.

### Cosa controlla `equivalenza.c`

1. passo induttivo: da ogni coppia di stati in relazione (S_A da 0 a
   `S_PASSO_MAX`, ogni coda possibile di `NUM_FLUSSI` flussi) ogni primitiva dà
   lo stesso esito e porta a stati ancora in relazione.
2. equivalenza semantica: dallo stato iniziale si provano TUTTE le sequenze di
   P e V lunghe fino a `PROFONDITA`, controllando esiti e invarianti a ogni
   passo.
3. thread reali: le due definizioni come semafori veri (stesso schema presente
   in `dijkstra.c`) sottoposte allo stesso test.

Il codice controlla valori limitati (numero di flussi, S, lunghezza delle
sequenze): la dimostrazione generale è quella scritta sopra.

## Compilazione

```sh
cc -Wall -pthread spinlock.c -o spinlock && ./spinlock
cc -Wall -pthread bin_posix.c -o bin_posix && ./bin_posix
cc -Wall -pthread dijkstra.c -o dijkstra && ./dijkstra
cc -Wall -pthread equivalenza.c -o equivalenza && ./equivalenza
```

