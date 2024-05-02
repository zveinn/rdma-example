#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  int MsgLen;
  int RCode;
  char *Msg;
} error;

void ProcessSlice(int length, uint8_t *data) {
  // Access and process data
  while (1) {
    printf("Element %d: %d %d\n", 0, data[0], (uint32_t *)&data[0]);
    sleep(1);
  }
}

error *get_error() {
  error *x = malloc(sizeof(error));
  x->Msg = "";
  x->MsgLen = strlen(x->Msg);
  x->RCode = 2;
  // x->Msg = malloc(10);
  // strcpy(x->Msg, "meow!");
  return x;
}

uint8_t *get_data(int size) {
  uint8_t *data = (uint8_t *)malloc(size * sizeof(uint8_t));
  // Fill the array with some values (optional)
  for (int i = 0; i < size; i++) {
    data[i] = i;
  }
  return data;
}

void free_data(int *data) { free(data); }

void *doThread(void *argv) {
  int *index = (int *)argv;
  while (1) {
    sleep(2);
    printf("%d %lu\n", *index, pthread_self());
  }
  return NULL;
}
void *doThreadMuch(void *argv) {
  int *index = (int *)argv;
  while (1) {
    usleep(100);
  }
  return NULL;
}

int main(int argc, char **argv, char **env) {
  int i;
  int threads[40];
  int threads2[40];
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

  for (i = 0; i < 10; i++) {
    pthread_t thread;
    threads[i] = i;
    if (pthread_create(&thread, &attr, doThread, &threads[i]) != 0) {
      // fail
    }
  }
  for (i = 0; i < 30; i++) {
    pthread_t thread;
    threads2[i] = i;
    if (pthread_create(&thread, &attr, doThreadMuch, &threads2[i]) != 0) {
      // fail
    }
  }

  while (1) {
    sleep(1);
  }
}
