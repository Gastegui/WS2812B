#ifndef GLOBALES_H
#define GLOBALES_H

#include "freertos/idf_additions.h"
#include "tipos.h"

#define NUM_PIXELES 300
#define MQTT_TIMEOUT 50000000


extern struct PIXEL pixeles[NUM_PIXELES];
extern volatile uint8_t rx_buffer[NUM_PIXELES];

#endif
