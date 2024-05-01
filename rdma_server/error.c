#include <stdlib.h>
#include <string.h>

typedef struct {
  int MsgLen;
  int RCode;
  char *Msg;
} error;

error *make_error(char *msg, int rcode) {
  error *x = malloc(sizeof(error));
  x->Msg = msg;
  x->MsgLen = strlen(x->Msg);
  x->RCode = rcode;
  return x;
}

error *get_error() {
  error *x = malloc(sizeof(error));
  // x->Msg = msg;
  // x->MsgLen = strlen(x->Msg);
  // x->RCode = rcode;
  return x;
}
