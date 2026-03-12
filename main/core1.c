#include <stdint.h>
#include <string.h>
#include <math.h>
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "core1.h"
#include "globales.h"
#include "hal/gpio_types.h"
#include "tipos.h"



#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_LED_STRIP_GPIO_NUM      14

#define EXAMPLE_LED_NUMBERS         NUM_PIXELES

#define EXAMPLE_FRAME_DURATION_MS   1
#define EXAMPLE_ANGLE_INC_FRAME     0.01
#define EXAMPLE_ANGLE_INC_LED       0.1

static const char *TAG = "example";

static uint8_t led_strip_pixels[EXAMPLE_LED_NUMBERS * 3];



volatile SemaphoreHandle_t pixeles_mutex;


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

esp_timer_handle_t opto_timer;
volatile uint8_t interrupt_pulses = 0;
volatile enum INTERRUPT_STATE interrupt_state = NOTHING;
volatile uint32_t last_interrupt = 0;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t now = pdTICKS_TO_MS(xTaskGetTickCountFromISR());
    if (now - last_interrupt < 50) 
        return; 
    last_interrupt = now;

    esp_timer_stop(opto_timer);

    if(!gpio_get_level(OPTOCOUPLER_GPIO) && interrupt_pulses == 0)
        interrupt_state = STARTED;

    interrupt_pulses++;
    esp_timer_start_once(opto_timer, 1000000);
}


void opto_timer_callback(void* arg) 
{
    interrupt_state = READABLE;
}



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


    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << OPTOCOUPLER_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf);

    //gpio_set_direction(OPTOCOUPLER_GPIO, GPIO_MODE_INPUT);
    //gpio_set_pull_mode(OPTOCOUPLER_GPIO, GPIO_PULLUP_ONLY);
    //gpio_set_intr_type(OPTOCOUPLER_GPIO, GPIO_INTR_ANYEDGE);
    
    gpio_install_isr_service(0);
    gpio_isr_handler_add(OPTOCOUPLER_GPIO, gpio_isr_handler, (void*) OPTOCOUPLER_GPIO);


    esp_timer_create_args_t timer_args = {
        .callback = opto_timer_callback,
        .name = "opto_timer"
    };
    esp_timer_create(&timer_args, &opto_timer);


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
                    t = (timestamp - pixel->tiempo + pixel->offset) % (pixel->params.pulso.t_encendido + pixel->params.pulso.t_apagado);

                    if (t < pixel->params.pulso.t_encendido)
                    {
                        fase = (float)t / pixel->params.pulso.t_encendido;
                        angulo = -M_PI_2 + fase * M_PI;
                    }
                    else
                    {
                        fase = (float)(t - pixel->params.pulso.t_encendido) / pixel->params.pulso.t_apagado;
                        angulo = M_PI_2 + fase * M_PI;
                    }

                    pixel->color.value = (int32_t)pixel->params.respiracion.brillo_min + (sinf(angulo) + 1.0f) * 0.5f * ((int32_t)pixel->extra - (int32_t)pixel->params.respiracion.brillo_min);
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


        // Flush RGB values to LEDs
        ESP_ERROR_CHECK(rmt_transmit(led_chan, simple_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
        timestamp = pdTICKS_TO_MS(xTaskGetTickCount());

        while (xQueueReceive(*pixel_queue, &pixel_nuevo, 0) == pdTRUE)
        {
            pixel_nuevo.pixel.tiempo = timestamp;
            pixeles[pixel_nuevo.num] = pixel_nuevo.pixel;
        }

        ESP_LOGI(TAG, "pulses: %d", interrupt_pulses);

        if(interrupt_state == READABLE)
        {
            if(interrupt_pulses == 2)
                gpio_set_level(2, 1);
            else
                gpio_set_level(2, 0);

            interrupt_state = NOTHING;
            interrupt_pulses = 0;
        }

        //vTaskDelay(1);
    }
}
