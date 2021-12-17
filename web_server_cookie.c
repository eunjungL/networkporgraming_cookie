#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *get_content_type(const char *path) {
    const char *last_dot = strrchr(path, '.');
    if (last_dot) {
        if (strcmp(last_dot, ".csv") == 0) return "text/csv";
        if (strcmp(last_dot, ".gif") == 0) return "image/gif";
        if (strcmp(last_dot, ".htm") == 0) return "text/html";
        if (strcmp(last_dot, ".html") == 0) return "text/html";
        if (strcmp(last_dot, ".ico") == 0) return "image/x-icon";
        if (strcmp(last_dot, ".jpeg") == 0) return "image/jpeg";
        if (strcmp(last_dot, ".jpg") == 0) return "image/jpeg";
        if (strcmp(last_dot, ".js") == 0) return "application/javascript"; 
        if (strcmp(last_dot, ".json") == 0) return "application/json";
        if (strcmp(last_dot, ".png") == 0) return "image/png";
        if (strcmp(last_dot, ".pdf") == 0) return "application/pdf"; 
        if (strcmp(last_dot, ".svg") == 0) return "image/svg+xml"; 
        if (strcmp(last_dot, ".txt") == 0) return "text/plain";
    }

    return "text/plain";
}

int create_socket(const char *host, const char *port) {
    printf("configuring local address\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *bind_address;
    getaddrinfo(host, port, &hints, &bind_address);
    
    printf("Creating socket\n");
    int socket_listen;
    socket_listen = socket(bind_address->ai_family, bind_address->ai_socktype, bind_address->ai_protocol);
    if (socket_listen < 0) {
        fprintf(stderr, "socket() failed. (%d)\n", errno);
        exit(1);
    }

    printf("Binding socket to local address\n");
    if (bind(socket_listen, bind_address->ai_addr, bind_address->ai_addrlen)) {
        fprintf(stderr, "bind() failed. (%d)\n", errno);
        exit(1);
    }

    freeaddrinfo(bind_address);
    
    printf("listening\n");
    if (listen(socket_listen, 10) < 0) {
        fprintf(stderr, "listen() failed, (%d)\n", errno);
        exit(1);
    }

    return socket_listen;
}

#define MAX_REQUEST_SIZE 2047

struct client_info {
    socklen_t address_length;
    struct sockaddr_storage address;
    int socket;
    char request[MAX_REQUEST_SIZE + 1];
    int received;
    struct client_info *next;
};

static struct client_info *clients = 0;

struct client_info *get_client(int s) {
    struct client_info *ci = clients;

    while (ci) {
        if (ci->socket == s) break;
        ci = ci->next;
    }
    if (ci) return ci;

    struct client_info *n = (struct client_info*)calloc(1, sizeof(struct client_info));

    if (!n) {
        fprintf(stderr, "out of memory \n");
        exit(1);
    }

    n->address_length = sizeof(n->address);
    n->next = clients;
    clients = n;
    return n;
}

void drop_client(struct client_info *client) {
    close(client->socket);
    struct client_info **p = &clients;
    while (*p) {
        if (*p == client) {
            *p = client->next;
            free(client);
            return;
        }
        p = &(*p)->next;
    }
    fprintf(stderr, "drop_client not fonnd\n");
    exit(1);
}

const char *get_client_address(struct client_info *ci) {
    static char address_buffer[100];
    getnameinfo((struct sockaddr*) &ci->address,
        ci->address_length, address_buffer, sizeof(address_buffer), 0, 0, NI_NUMERICHOST);
    return address_buffer;
}

fd_set wait_on_clients(int server) {
    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(server, &reads);
    int max_socket = server;
    struct client_info *ci = clients;

    while (ci) {
        FD_SET(ci->socket, &reads);
        if (ci->socket > max_socket) max_socket = ci->socket;
        ci = ci->next;
    }

    if (select(max_socket + 1, &reads, 0, 0, 0) < 0) {
        fprintf(stderr, "select() failed. (%d)\n", errno);
        exit(1);
    }

    return reads;
}

void send_400(struct client_info *client) {
    const char *c400 = "HTTP/1.1 400 Bad Request\r\n"
        "Connection: close\r\n"
        "Content-Length: 11\r\n\r\n Bad Request";
    send(client->socket, c400, strlen(c400), 0);

    drop_client(client);
}

void send_404(struct client_info *client) {
    const char *c404 = "HTTP/1.1 404 Not Found\r\n"
        "Connecton: close\r\n"
        "Content-Length: 9\r\n\r\n Not Found";
    send(client->socket, c404, strlen(c404), 0);

    drop_client(client);
}

int STATIC_COOKIE = 0;
int make_cookie() {
    int cookie = ++STATIC_COOKIE;
    char full_path[1024];
    sprintf(full_path, "cookies/%d", cookie);
    FILE *fp = fopen(full_path, "a+");
    fclose(fp);

    return cookie;
}


void serve_resource(struct client_info *client, const char* path) {
    printf("serve_resoucre %s %s\n", get_client_address(client), path);

    char full_path[128];
    sprintf(full_path, "cookies/%s", path);

    FILE *fp = fopen(full_path, "rb");
    if (!fp) {
        send_404(client);
        return;
    }

    fseek(fp, 0L, SEEK_END);
    size_t cl = ftell(fp);
    rewind(fp);

    const char *ct = get_content_type(full_path);

#define BESIZE 1024
    char buffer[BESIZE * 2];

    sprintf(buffer, "HTTP/1.1 200 OK\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Connection: close\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Content-Length: %lu\r\n", cl);
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Content-Type: %s\r\n", ct);
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Set-Cookie: id=%s\r\n", path);
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    int r = fread(buffer, 1, BESIZE, fp);
    while (r) {
        send(client->socket, buffer, r, 0);
        r = fread(buffer, 1, BESIZE, fp);
    }

    fclose(fp);
    drop_client(client);
}

int main() {
    int server = create_socket("127.0.0.1", "8080");

    while(1) {
        fd_set reads;
        reads = wait_on_clients(server);

        if (FD_ISSET(server, &reads)) {
            struct client_info *client = get_client(-1);
            client->socket = accept(server, (struct sockaddr*) &(client->address), &(client->address_length));

            if (client->socket < 0) {
                fprintf(stderr, "accept() failed %d\n", errno);
                return 1;
            }
            printf("New Connection from %s\n", get_client_address(client));
        }
        struct client_info *client = clients;
        while (client) {
            struct client_info *next = client->next;
            if (FD_ISSET(client->socket, &reads)) {
                if (MAX_REQUEST_SIZE == client->received) {
                    send_400(client);
                    continue;
                }
            }
            int r = recv(client->socket, client->request + client->received, MAX_REQUEST_SIZE - client->received, 0);
            if (r < 1) {
                printf("unexpected dissconnet from %s", get_client_address(client));
                drop_client(client);
            } else {
                char* start = client->request + client->received;
                client->received += r;
                client->request[client->received] = 0;
                char *q = strstr(start, "\r\n\r\n");
                // printf("Q: %s  end\n", q);

                if (q) {
                    if (strncmp("GET /", start, 5)) { // POST /
                            char *cookie = strstr(start, "\nCookie: id=");
                            char *body = q + 4;
                            printf("Body: %s\nend\n", body);
                            if (cookie == NULL) {
                                int real_cookie = make_cookie();
                                char full_path[BESIZE];
                                sprintf(full_path, "cookies/%d", real_cookie);
                                FILE *fp = fopen(full_path, "a");
                                if (fp) {
                                    fwrite(body, strlen(body), 1, fp);
                                    fclose(fp);
                                }
                                serve_resource(client, cookie);
                            } else {
                                int cookie_id = 0;
                                char *real_cookie = strchr(cookie, '=');
                                real_cookie +=1;
                                cookie_id = strtol(real_cookie, 0, 10);
                                char cookie_buf[BESIZE];
                                sprintf(cookie_buf, "%d", cookie_id);

                                char full_path[BESIZE * 2];
                                sprintf(full_path, "cookies/%s", cookie_buf);
                                FILE *fp = fopen(full_path, "a");
                                if (fp) {
                                    fwrite(body, strlen(body), 1, fp);
                                    fclose(fp);
                                }

                                serve_resource(client, cookie_buf);
                            }
                        
                    } else {
                        char *path = start + 4;
                        char *end_path = strstr(path, " ");
                        if (!end_path) {
                            send_400(client);
                        } else {
                            char *cookie = strstr(start, "Cookie: id=");
                            if (cookie == NULL) {
                                int real_cookie = make_cookie();
                                char cookie_buffer[BESIZE];
                                sprintf(cookie_buffer, "%d", real_cookie);
                                serve_resource(client, cookie_buffer);
                            } else {
                                int cookie_id = 0;
                                cookie = strchr(cookie, '=');
                                cookie +=1;
                                cookie_id = strtol(cookie, 0, 10);
                                char cookie_buf[BESIZE];
                                sprintf(cookie_buf, "%d", cookie_id);
                                serve_resource(client, cookie_buf);
                            }
                        }
                    }
                }
            }
            client = next;
        }
    }

    printf("\nClosing socket\n");
    close(server);

    printf("finisehd\n");
    return 0;
}