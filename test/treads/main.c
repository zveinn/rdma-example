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
  x->Msg = "THIS IS FROM C YYYEEAABOOOYYYY";
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
