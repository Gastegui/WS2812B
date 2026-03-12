#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define HOST "192.168.1.220"
#define PORT 3333

static volatile atomic_int running = 1;

void handle_sigint(int sig) 
{
    (void)sig;
    running = 0;
}

int main() 
{
    int sock;
    struct sockaddr_in server;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        return 1;
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(3333);
    server.sin_addr.s_addr = inet_addr("192.168.1.136");

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) 
    {
        perror("connect");
        return 1;
    }

    uint8_t init[2] = {0x00, 0x00};
    write(sock, init, 2);

    FILE *cava = popen("cava -p ./cava_config", "r");
    char line[1024];
    
    while (fgets(line, sizeof(line), cava) && running) 
    {
        uint8_t packet[100 * 5];
        int offset = 0;
        int i = 60;
        static uint8_t global_hue = 0;

        char *token = strtok(line, ";");
        while (token != NULL) 
        {
            int n = atoi(token);
            uint8_t hue = global_hue + (uint8_t)((264 - i) * 255 / 32);

            packet[offset++] = (i >> 8) & 0xFF;
            packet[offset++] = i & 0xFF;
            packet[offset++] = 10 + n/2;
            packet[offset++] = 0xFF;
            packet[offset++] = 30;

            i--;
            token = strtok(NULL, ";");
        }
        write(sock, packet, offset);
   }

    pclose(cava);
    close(sock);
    return 0;
}
