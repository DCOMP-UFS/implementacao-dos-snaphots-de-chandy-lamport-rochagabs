#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // Para a função sleep()

// Compilação: mpicc -o snap snap.c -lpthread
// Execução:   mpiexec -n 3 ./snap

// Estruturas e funções para a fila de relógios
typedef struct Clock {
    int p[3];
    int receiverId; // Id do receptor do relógio
    struct Clock *next;
} Clock;

typedef struct {
    Clock *head;
    Clock *tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond_cons;
    pthread_cond_t cond_produ;
} FilaClock;

FilaClock entradaFila, saidaFila;

int inicia_snapshot = 0;
int *snap_valores;
Clock relogio = {{0, 0, 0}, 0};

void enfileirar(FilaClock *fila, Clock relogio) {
    Clock *newClock = (Clock *)malloc(sizeof(Clock));
    if (newClock == NULL) {
        fprintf(stderr, "Erro ao alocar memória para novo relógio.\n");
        exit(1);
    }
    *newClock = relogio;
    newClock->next = NULL;

    pthread_mutex_lock(&fila->mutex);

    while (fila->count >= 3) {
        //printf("Fila de entrada cheia. Aguardando espaço para produção...\n");
        sleep(1); // Espera por 1 segundo antes de tentar novamente
        pthread_cond_wait(&fila->cond_cons, &fila->mutex);
    }

    if (fila->head == NULL) {
        fila->head = newClock;
        fila->tail = newClock;
    } else {
        fila->tail->next = newClock;
        fila->tail = newClock;
    }

    fila->count++;
    pthread_cond_signal(&fila->cond_produ);
    pthread_mutex_unlock(&fila->mutex);
}

Clock desenfileirar(FilaClock *fila) {
    Clock relogio;
    pthread_mutex_lock(&fila->mutex);

    while (fila->head == NULL) {
        //printf("Fila de saída vazia. Aguardando relógios para consumir...\n");
        sleep(1); // Espera por 1 segundo antes de tentar novamente
        pthread_cond_wait(&fila->cond_produ, &fila->mutex);
    }

    Clock *temp = fila->head;
    relogio = *temp;
    fila->head = fila->head->next;

    if (fila->head == NULL) {
        fila->tail = NULL;
    }

    fila->count--;
    pthread_cond_signal(&fila->cond_cons);
    pthread_mutex_unlock(&fila->mutex);

    free(temp);
    return relogio;
}

void ini_fila(FilaClock *fila) {
    fila->head = NULL;
    fila->tail = NULL;
    fila->count = 0;
    pthread_mutex_init(&fila->mutex, NULL);
    pthread_cond_init(&fila->cond_cons, NULL);
    pthread_cond_init(&fila->cond_produ, NULL);
}

// Funções das threads
void *entradaThread(void *args) {
    long id = (long)args;
    while (1) {
        Clock receivedClock;
        MPI_Recv(&receivedClock, sizeof(Clock), MPI_BYTE, MPI_ANY_SOURCE, 0,
                MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        enfileirar(&entradaFila, receivedClock);
    }
    return NULL;
}

void *saidaThread(void *args) {
    long id = (long)args;
    while (1) {
        Clock clockToSend = desenfileirar(&saidaFila);
        MPI_Send(&clockToSend, sizeof(Clock), MPI_BYTE, clockToSend.receiverId, 0,
                MPI_COMM_WORLD);
        printf("Processo: %d, Clock: (%d, %d, %d)\n", clockToSend.receiverId,
            clockToSend.p[0], clockToSend.p[1], clockToSend.p[2]);
    }
    return NULL;
}

int max(int a, int b) { return a > b ? a : b; }

void Event(int pid, Clock *clock) {
    clock->p[pid]++; // evento ocorreu
    relogio.p[relogio.receiverId]++;
    printf("Evento ocorreu no processo: %d, Clock: (%d, %d, %d)\n", pid,
            clock->p[0], clock->p[1], clock->p[2]);
}

void Send(int pid, int my_id, Clock *clock) {
    clock->p[my_id]++;
    clock->receiverId = pid;
    relogio.p[relogio.receiverId]++;
    enfileirar(&saidaFila, *clock);
    printf("Processo: %d enviou Clock para %d: (%d, %d, %d)\n", my_id, pid,
            clock->p[0], clock->p[1], clock->p[2]);
}

void Receive(int pid, int my_id, Clock *clock) {
    clock->p[my_id]++;
    Clock clock_received;
    clock_received = desenfileirar(&entradaFila);
    relogio.p[relogio.receiverId]++;
    for (int i = 0; i < 3; i++) {
        clock->p[i] = max(clock->p[i], clock_received.p[i]);
    }
    printf("Processo: %d recebeu Clock de: %d. (%d, %d, %d)\n", my_id, pid,
            clock->p[0], clock->p[1], clock->p[2]);
}

void comecar_snapshot() {
    inicia_snapshot = 1;
    snap_valores = (int *)malloc(3 * sizeof(int));
    for (int i = 0; i < 3; i++) {
        snap_valores[i] = relogio.p[i];
    }

    printf("Snapshot iniciado por processo %d\n", relogio.receiverId);
}

void termina_snapshot() {
    inicia_snapshot = 0;
    free(snap_valores);
}

void registra_estado() {
    if (inicia_snapshot) {
        printf("Snapshot do processo %d: {%d, %d, %d}\n", relogio.receiverId,
            snap_valores[0], snap_valores[1], snap_valores[2]);
    }
}

void tirar_snapshot() {
    comecar_snapshot();
    registra_estado();
    termina_snapshot();
}   

// Representa o processo de rank 0
// Representa o processo de rank 0
void process0() {
    Clock clock = {{0, 0, 0}};
    Event(0, &clock); //(1,0,0)
    comecar_snapshot();
    Send(1, 0, &clock); //(1,1,0)
    Receive(1, 0, &clock);
    Send(2, 0, &clock);
    Receive(2, 0, &clock);
    Send(1, 0, &clock);
    Event(0, &clock); // Incrementa o relógio após o evento
    registra_estado();
    termina_snapshot();
    printf("Processo: %d, Clock: (%d, %d, %d)\n", 0, clock.p[0], clock.p[1],
            clock.p[2]);

}

// Representa o processo de rank 1
void process1() {
    Clock clock = {{0, 0, 0}};
    Send(0, 1, &clock);
    comecar_snapshot();
    Receive(0, 1, &clock);
    Receive(0, 1, &clock);
    registra_estado();
    printf("Processo: %d, Clock: (%d, %d, %d)\n", 1, clock.p[0], clock.p[1],
            clock.p[2]);
}

// Representa o processo de rank 2
// Representa o processo de rank 2
void process2() {
    Clock clock = {{0, 0, 0}};

    Event(2, &clock); // Registra o evento antes de enviar a mensagem

    Send(0, 2, &clock);
    comecar_snapshot();
    Receive(0, 2, &clock);

    registra_estado(); // Inicia o snapshot após receber a mensagem
    printf("Processo: %d, Clock: (%d, %d, %d)\n", 2, clock.p[0], clock.p[1],
            clock.p[2]);
}


int main(void) {
    int my_rank;

    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    relogio.receiverId = my_rank;

    ini_fila(&entradaFila);
    ini_fila(&saidaFila);

    pthread_t threadEntrada, exitThread;

    // Criação das threads para as filas de entrada e saída
    pthread_create(&threadEntrada, NULL, entradaThread, NULL);
    pthread_create(&exitThread, NULL, saidaThread, NULL);

    // Lógica dos processos
    if (my_rank == 0) {
        process0();
    } else if (my_rank == 1) {
        process1();
    } else if (my_rank == 2) {
        process2();
    }

    // Finalização das threads
    pthread_join(threadEntrada, NULL);
    pthread_join(exitThread, NULL);

    tirar_snapshot();

    // Destruindo mutex e condições
    pthread_mutex_destroy(&entradaFila.mutex);
    pthread_mutex_destroy(&saidaFila.mutex);

    pthread_cond_destroy(&entradaFila.cond_cons);
    pthread_cond_destroy(&entradaFila.cond_produ);

    pthread_cond_destroy(&saidaFila.cond_cons);
    pthread_cond_destroy(&saidaFila.cond_produ);


    /* Finaliza MPI */
    MPI_Finalize();

    return 0;
}