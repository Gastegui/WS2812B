#ifndef TIPOS_H
#define TIPOS_H

#include <stdbool.h>
#include <stdint.h>

#define TIEMPO uint32_t


struct COLOR
{
    uint8_t hue;
    uint8_t saturation;
    uint8_t value;
};

enum MODO: uint8_t
{
    APAGADO,
    ESTATICO,
    PULSO,
    RESPIRACION,
    FADE,
    ARCOIRIS
};

struct PARAMS_ESTATICO
{
    TIEMPO nada;
    TIEMPO nada2;
    uint8_t nada3;
    struct COLOR color;
};

struct PARAMS_PULSO
{
    TIEMPO t_encendido;
    TIEMPO t_apagado;
    uint8_t nada;
    struct COLOR color;
};

struct PARAMS_RESPIRACION
{
    TIEMPO t_encender;
    TIEMPO t_apagar;
    uint8_t brillo_min;
    struct COLOR color;
    TIEMPO t_encendido;
    TIEMPO t_apagado;
};

struct PARAMS_FADE
{
    TIEMPO t_fade;
    TIEMPO nada;
    uint8_t nada2;
    struct COLOR uno;
    struct COLOR dos;
    struct COLOR tres;
    struct COLOR cuatro;
    struct COLOR cinco;
};

struct PARAMS_ARCOIRIS
{
    TIEMPO t_tiempo_ciclo;
    TIEMPO nada;
    uint8_t nada2;
};

#define PIXEL_PARAMS_SIZE sizeof(struct PARAMS_FADE)
struct PIXEL
{
    struct COLOR color;
    enum MODO modo;
    TIEMPO offset;
    uint8_t extra;
    union PARAMS
    {
        uint8_t raw[PIXEL_PARAMS_SIZE];
        struct PARAMS_ESTATICO estatico;
        struct PARAMS_PULSO pulso;
        struct PARAMS_RESPIRACION respiracion;
        struct PARAMS_FADE fade;
        struct PARAMS_ARCOIRIS arcoiris;
    } params;
};


struct __attribute__((packed)) PIXEL_TCP
{
    uint16_t index;

    uint8_t hue;
    uint8_t saturation;
    uint8_t value;

    uint8_t modo;

    TIEMPO offset;

    uint8_t extra;

    uint8_t params_raw[PIXEL_PARAMS_SIZE];
};
#define PIXEL_TCP_SIZE sizeof(struct PIXEL_TCP)

struct __attribute__((packed)) PIXEL_TCP_STREAM
{
    uint16_t index;
    uint8_t hue;
    uint8_t saturation;
    uint8_t value;
};
#define PIXEL_TCP_STREAM_SIZE sizeof(struct PIXEL_TCP_STREAM)

struct PIXEL_QUEUE{
    uint16_t num;
    struct PIXEL pixel;
};

#endif