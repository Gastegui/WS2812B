#include <stdint.h>
#include <string.h>
#include <math.h>
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

#include "core1.h"
#include "globales.h"
#include "tipos.h"



#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_LED_STRIP_GPIO_NUM      14

static const char *TAG = "example";

static uint8_t led_strip_pixels[NUM_PIXELES * 3];

static const rmt_symbol_word_t ws2812_zero = {
    .level0 = 1,
    .duration0 = 0.3 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T0H=0.3us
    .level1 = 0,
    .duration1 = 0.9 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T0L=0.9us
};

static const rmt_symbol_word_t ws2812_one = {
    .level0 = 1,
    .duration0 = 0.9 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T1H=0.9us
    .level1 = 0,
    .duration1 = 0.3 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T1L=0.3us
};

//reset defaults to 50uS
static const rmt_symbol_word_t ws2812_reset = {
    .level0 = 0,
    .duration0 = RMT_LED_STRIP_RESOLUTION_HZ / 1000000 * 50 / 2,
    .level1 = 0,
    .duration1 = RMT_LED_STRIP_RESOLUTION_HZ / 1000000 * 50 / 2,
};

void hsv_to_rgb(struct COLOR hsv, uint8_t* r, uint8_t* g, uint8_t* b)
{
    if(hsv.saturation == 0) {
        *r = *g = *b = hsv.value;
        return;
    }

    uint8_t region    = hsv.hue / 43;
    uint8_t remainder = (hsv.hue - (region * 43)) * 6;

    uint8_t p = (hsv.value * (255 - hsv.saturation)) >> 8;
    uint8_t q = (hsv.value * (255 - ((hsv.saturation * remainder) >> 8))) >> 8;
    uint8_t t = (hsv.value * (255 - ((hsv.saturation * (255 - remainder)) >> 8))) >> 8;

    switch(region) {
        case 0: *r = hsv.value; *g = t;         *b = p;         break;
        case 1: *r = q;         *g = hsv.value; *b = p;         break;
        case 2: *r = p;         *g = hsv.value; *b = t;         break;
        case 3: *r = p;         *g = q;         *b = hsv.value; break;
        case 4: *r = t;         *g = p;         *b = hsv.value; break;
        default:*r = hsv.value; *g = p;         *b = q;         break;
    }
}

static size_t encoder_callback(const void *data, size_t data_size,
                               size_t symbols_written, size_t symbols_free,
                               rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    // We need a minimum of 8 symbol spaces to encode a byte. We only
    // need one to encode a reset, but it's simpler to simply demand that
    // there are 8 symbol spaces free to write anything.
    if (symbols_free < 8) {
        return 0;
    }

    // We can calculate where in the data we are from the symbol pos.
    // Alternatively, we could use some counter referenced by the arg
    // parameter to keep track of this.
    size_t data_pos = symbols_written / 8;
    uint8_t *data_bytes = (uint8_t*)data;
    if (data_pos < data_size) {
        // Encode a byte
        size_t symbol_pos = 0;
        for (int bitmask = 0x80; bitmask != 0; bitmask >>= 1) {
            if (data_bytes[data_pos]&bitmask) {
                symbols[symbol_pos++] = ws2812_one;
            } else {
                symbols[symbol_pos++] = ws2812_zero;
            }
        }
        // We're done; we should have written 8 symbols.
        return symbol_pos;
    } else {
        //All bytes already are encoded.
        //Encode the reset, and we're done.
        symbols[0] = ws2812_reset;
        *done = 1; //Indicate end of the transaction.
        return 1; //we only wrote one symbol
    }
}

void core1_main(void* args)
{
    QueueHandle_t* pixel_queue = (QueueHandle_t*) args;

    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_channel_handle_t led_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64, // increase the block size can make the LED less flickering
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4, // set the number of transactions that can be pending in the background
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(TAG, "Create simple callback-based encoder");
    rmt_encoder_handle_t simple_encoder = NULL;
    const rmt_simple_encoder_config_t simple_encoder_cfg = {
        .callback = encoder_callback
        //Note we don't set min_chunk_size here as the default of 64 is good enough.
    };
    ESP_ERROR_CHECK(rmt_new_simple_encoder(&simple_encoder_cfg, &simple_encoder));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    ESP_LOGI(TAG, "Start LED rainbow chase");
    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // no transfer loop
    };
    uint8_t r;
    uint8_t g;
    uint8_t b;
    TIEMPO t;
    TIEMPO timestamp;
    struct PIXEL* pixel;
    float angulo;
    float fase;
    struct PIXEL_QUEUE pixel_nuevo;

    for(int led = 0; led < NUM_PIXELES; led++)
    {
        pixeles[led].color = (struct COLOR){40, 150, 100};
        pixeles[led].modo = 3;
        pixeles[led].tiempo = 0;
        pixeles[led].offset = led*50;
        pixeles[led].extra = 30;
        pixeles[led].params.respiracion.t_apagar = 2000;
        pixeles[led].params.respiracion.t_encender = 3000;
        pixeles[led].params.respiracion.brillo_min = 10;
        pixeles[led].params.respiracion.t_encendido = 0;
        pixeles[led].params.respiracion.t_apagado = 0;
    }

    while (1) 
    {
        for(int led = 0; led < NUM_PIXELES; led++)
        {
            t = 0;
            timestamp = pdTICKS_TO_MS(xTaskGetTickCount());
            pixel = &pixeles[led];
            switch(pixel->modo)
            {
                case ARCOIRIS:
                    t = (timestamp - pixel->tiempo) % pixel->params.arcoiris.t_tiempo_ciclo;
                    pixel->color.hue = pixel->offset + (255*t) / pixel->params.arcoiris.t_tiempo_ciclo;
                    break;
                case PULSO: //pixel->extra = brillo
                    t = (timestamp - pixel->tiempo + pixel->offset) % (pixel->params.pulso.t_encendido + pixel->params.pulso.t_apagado);
                    if(t >= pixel->params.pulso.t_encendido)
                        pixel->color.value = 0;
                    else
                        pixel->color.value = pixeles->extra;
                    break;
                case RESPIRACION: //pixel->extra = brillo_max
                    t = (timestamp - pixel->tiempo + pixel->offset) % (pixel->params.respiracion.t_encender + pixel->params.respiracion.t_encendido + pixel->params.respiracion.t_apagar + pixel->params.respiracion.t_apagado);

                    if(t < pixel->params.respiracion.t_encender)
                    {
                        fase = (float)t / pixel->params.respiracion.t_encender;
                        angulo = -M_PI_2 + fase * M_PI;
                        pixel->color.value = (int32_t)pixel->params.respiracion.brillo_min + (sinf(angulo) + 1.0f) * 0.5f * ((int32_t)pixel->extra - (int32_t)pixel->params.respiracion.brillo_min);
                    }
                    else if(t < pixel->params.respiracion.t_encender + pixel->params.respiracion.t_encendido)
                    {
                        pixel->color.value = pixel->extra;
                    }
                    else if(t < pixel->params.respiracion.t_encender + pixel->params.respiracion.t_encendido + pixel->params.respiracion.t_apagar)
                    {
                        fase = (float)(t - pixel->params.respiracion.t_encender - pixel->params.respiracion.t_encendido) / pixel->params.respiracion.t_apagar;
                        angulo = M_PI_2 + fase * M_PI;
                        pixel->color.value = (int32_t)pixel->params.respiracion.brillo_min + (sinf(angulo) + 1.0f) * 0.5f * ((int32_t)pixel->extra - (int32_t)pixel->params.respiracion.brillo_min);
                    }
                    else 
                    {
                        pixel->color.value = pixel->params.respiracion.brillo_min;
                    }

                    break;
                case FADE: //pixel->extra = ultimo color, pixel->tiempo = tiempo del ultimo color
                    t = (timestamp - pixel->tiempo) % (pixel->params.fade.t_fade);

                    break;
                case ESTATICO:
                case APAGADO:
                default:
                    break;
            }

            hsv_to_rgb(pixeles[led].color, &r, &g, &b);
            led_strip_pixels[led*3+0] = g;
            led_strip_pixels[led*3+1] = r;
            led_strip_pixels[led*3+2] = b;
        }


        ESP_ERROR_CHECK(rmt_transmit(led_chan, simple_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
        timestamp = pdTICKS_TO_MS(xTaskGetTickCount());

        while (xQueueReceive(*pixel_queue, &pixel_nuevo, 0) == pdTRUE)
        {
            pixel_nuevo.pixel.tiempo = timestamp;
            pixeles[pixel_nuevo.num] = pixel_nuevo.pixel;
        }

        //vTaskDelay(1);
    }
}
