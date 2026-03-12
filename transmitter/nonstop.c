#include <stdatomic.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>


#include "../main/tipos.h"

static volatile atomic_int running = 1;

void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}
uint16_t encode(uint16_t num, struct PIXEL* pixel, uint8_t* stream)
{
    uint16_t pos = 0;
    uint8_t i;

    #define PUT(byte)            \
        do {                     \
            stream[pos]=byte; \
            pos++;            \
        } while(0)

    PUT((num >> 8) & 0xFF);
    PUT(num & 0xFF);

    PUT(pixel->color.hue);
    PUT(pixel->color.saturation);
    PUT(pixel->color.value);
    PUT((uint8_t)pixel->modo);

    PUT((pixel->tiempo >> 24) & 0xFF);
    PUT((pixel->tiempo >> 16) & 0xFF);
    PUT((pixel->tiempo >> 8) & 0xFF);
    PUT(pixel->tiempo & 0xFF);

    PUT((pixel->offset >> 24) & 0xFF);
    PUT((pixel->offset >> 16) & 0xFF);
    PUT((pixel->offset >> 8) & 0xFF);
    PUT(pixel->offset & 0xFF);

    PUT(pixel->extra);

    PUT((pixel->params.fade.t_fade >> 24) & 0xFF);
    PUT((pixel->params.fade.t_fade >> 16) & 0xFF);
    PUT((pixel->params.fade.t_fade >> 8) & 0xFF);
    PUT(pixel->params.fade.t_fade & 0xFF);

    PUT((pixel->params.fade.nada >> 24) & 0xFF);
    PUT((pixel->params.fade.nada >> 16) & 0xFF);
    PUT((pixel->params.fade.nada >> 8) & 0xFF);
    PUT(pixel->params.fade.nada & 0xFF);

    PUT(pixel->params.fade.nada2);

    PUT(pixel->params.fade.uno.hue);
    PUT(pixel->params.fade.uno.saturation);
    PUT(pixel->params.fade.uno.value);

    PUT(pixel->params.fade.dos.hue);
    PUT(pixel->params.fade.dos.saturation);
    PUT(pixel->params.fade.dos.value);

    PUT(pixel->params.fade.tres.hue);
    PUT(pixel->params.fade.tres.saturation);
    PUT(pixel->params.fade.tres.value);

    PUT(pixel->params.fade.cuatro.hue);
    PUT(pixel->params.fade.cuatro.saturation);
    PUT(pixel->params.fade.cuatro.value);

    PUT(pixel->params.fade.cinco.hue);
    PUT(pixel->params.fade.cinco.saturation);
    PUT(pixel->params.fade.cinco.value);

    return pos;
}



int main() {
    int sock;
    struct sockaddr_in server;

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        return 1;
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(3333);
    server.sin_addr.s_addr = inet_addr("192.168.1.130");

    // Connect
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect");
        return 1;
    }

    
    uint16_t pos = 0;
    
    uint16_t inicio = 0;
    int cantidad = 288;
    struct PIXEL pixel[cantidad];
    uint8_t stream[cantidad * 5];


    uint8_t size[2] = {0};
    if (send(sock, size, sizeof(size), 0) < 0) {
        perror("size");
        return 1;
    }
    uint16_t on = 0;
    while(running)
    {
        pos = 0;
        for(uint16_t i = inicio; i < inicio + cantidad; i++)
        {
            int b = i*5;
            stream[b] = (i>>8)&0xFF;
            stream[b+1] = i&0xFF;
            stream[b+2] = on + i;
            stream[b+3] = 250;
            stream[b+4] = 5;
        }

        if (send(sock, stream, sizeof(stream), 0) < 0) 
        {
            perror("stream");
            return 1;
        }
        //usleep(50);
        on++;
    }


    close(sock);
    return 0;
}