#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>


#include "../main/tipos.h"

void create_stream(struct PIXEL* pixeles, uint8_t* stream, uint16_t start, uint16_t end)
{
    struct PIXEL_TCP pixel_tcp;

    for(uint16_t i=0; i < end; i++)
    {
        pixel_tcp.index = i+start;
        pixel_tcp.hue = pixeles[i].color.hue;
        pixel_tcp.saturation = pixeles[i].color.saturation;
        pixel_tcp.value = pixeles[i].color.value;
        pixel_tcp.modo = pixeles[i].modo;
        pixel_tcp.offset = pixeles[i].offset;
        pixel_tcp.extra = pixeles[i].extra;
        memcpy(pixel_tcp.params_raw, pixeles[i].params.raw, PIXEL_PARAMS_SIZE);

        memcpy(stream + i*PIXEL_TCP_SIZE, &pixel_tcp, PIXEL_TCP_SIZE);
    }
}

int main() {
    int sock;
    struct sockaddr_in server;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        return 1;
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(3333);
    server.sin_addr.s_addr = inet_addr("192.168.1.223");

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect");
        return 1;
    }

    uint16_t inicio = 150;
    uint16_t cantidad = 50;

    struct PIXEL pixel[cantidad];
    uint8_t stream[cantidad * (PIXEL_TCP_SIZE)];
    memset(pixel, 0, sizeof(pixel));

    for(uint16_t i = 0; i < cantidad; i++)
    {
        pixel[i].color = (struct COLOR){40, 150, 50};
        pixel[i].modo = RESPIRACION;
        pixel[i].offset = (inicio+i)*50;
        pixel[i].extra = 30;
        pixel[i].params.respiracion.t_apagar = 2000;
        pixel[i].params.respiracion.t_encender = 3000;
        pixel[i].params.respiracion.brillo_min = 10;
        pixel[i].params.respiracion.t_encendido = 0;
        pixel[i].params.respiracion.t_apagado = 0;
    }

    create_stream(pixel, stream, inicio, cantidad);

    uint8_t size[2] = {0};
    size[0] = cantidad & 0xFF;
    size[1] = (cantidad >> 8) & 0xFF;

    if (send(sock, size, sizeof(size), 0) < 0) {
        perror("send");
        return 1;
    }
    if (send(sock, stream, sizeof(stream), 0) < 0) {
        perror("send");
        return 1;
    }
    close(sock);
    return 0;
}
