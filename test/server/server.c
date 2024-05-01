#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 11111 // Replace with desired port number
#define MAX_CLIENTS 10

typedef struct {
  int socket;
  int index;
} client;

client *clients[MAX_CLIENTS];

void *handle_client(void *arg) {
  // void *handle_client(client* c) {

  client *c = (client *)arg;
  char buffer[1024];
  int valread;
  printf("client thread %p\n", c);
  printf("client desc %i\n", c->socket);
  printf("client desc %p\n", &c->socket);
  printf("client index %i\n", c->index);
  printf("client index %p\n", &c->index);

  while (1) {
    valread = read(c->socket, buffer, sizeof(buffer));
    if (valread == 0) {
      // Client disconnected
      printf("Client disconnected\n");
      free(clients[c->index]);
      close(c->socket);
      break;
    } else if (valread == -1) {
      perror("read");
      close(c->socket);
      break;
    }

    // Process the received data from client (e.g., echo it back)
    printf("pointer: %p > index: %d\n", &c, c->index);
    printf("Client: %s\n", buffer);
    send(c->socket, buffer, strlen(buffer), 0);
  }

  pthread_detach(pthread_self());
  return NULL;
}

int main(int argc, char const *argv[]) {
  int server_socket;
  struct sockaddr_in address;
  int opt = 1;
  pthread_t thread;

  if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  // Forcefully attaching socket to the port 8080 to avoid address already in
  // use error
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt))) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  if (bind(server_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  if (listen(server_socket, 3) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  printf("Server listening on port %d\n", PORT);

  while (1) {
    int i;
    int new_socket;
    if ((new_socket = accept(server_socket, (struct sockaddr *)&address,
                             (socklen_t *)&address)) < 0) {
      perror("accept");
      exit(EXIT_FAILURE);
    }

    printf("New connection accepted\n");
    for (i = 0; i < MAX_CLIENTS; i++) {
      if (clients[i] == 0) {

        clients[i] = malloc(sizeof(client));
        clients[i]->index = i;
        clients[i]->socket = new_socket;
        printf("client: %p\n", &clients[i]);
        printf("socket %i\n", clients[i]->socket);
        printf("socket %p\n", &clients[i]->socket);
        printf("index %i\n", clients[i]->index);
        printf("index %p\n", &clients[i]->index);

        if (pthread_create(&thread, NULL, handle_client, clients[i]) != 0) {
          perror("pthread_create");
          exit(EXIT_FAILURE);
        }
        break;
      }
    }
  }

  close(server_socket);
  return 0;
}
