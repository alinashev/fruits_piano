#include "fruits.h"
#include "buzzer.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>

#include "driver/gpio.h"

#define CONFIG_TOUCH_SUPPRESS_DEPRECATE_WARN 1
#include "driver/touch_pad.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "fruit_piano";

// Buzzer is connected to GPIO5.
#define BUZZER_GPIO GPIO_NUM_5

#define ACTIVE_DELTA_THRESHOLD  500U
#define DELTA_FULL_SCALE        10000U
#define EMA_ALPHA               0.30f

#define TOUCH_PERIOD_MS         20U
#define BASELINE_WAIT_MS        150U
#define BASELINE_SAMPLES        32U
#define BUZZER_NOTE_MS          35U

typedef struct {
    touch_pad_t pad;
    const char *name;
    uint32_t baseline;
    float ema;
    uint32_t freq_hz;
} fruit_entry_t;

// XIAO ESP32-S3 mapping used here:
// apricot: GPIO2 / D1 / TOUCH_PAD_NUM2 / note C4
// ginger:  GPIO6 / D5 / TOUCH_PAD_NUM6 / note E4
// GPIO5 is reserved for the buzzer.
static fruit_entry_t s_fruits[] = {
    { TOUCH_PAD_NUM2, "apricot", 0, 0.0f, 262 },
    { TOUCH_PAD_NUM6, "ginger",  0, 0.0f, 330 },
};

static atomic_bool s_touching = false;
static atomic_uint s_pot = 0;
static atomic_int s_active_index = -1;

static uint32_t clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static uint16_t delta_to_pot(uint32_t delta)
{
    uint32_t pot = delta * 4095UL / DELTA_FULL_SCALE;
    return (uint16_t)clamp_u32(pot, 0, 4095);
}

int fruits_count(void)
{
    return (int)(sizeof(s_fruits) / sizeof(s_fruits[0]));
}

const char *fruits_name(int i)
{
    if (i < 0 || i >= fruits_count()) {
        return "none";
    }
    return s_fruits[i].name;
}

bool fruits_is_touching(void)
{
    return atomic_load(&s_touching);
}

uint16_t fruits_read_pot(void)
{
    return (uint16_t)atomic_load(&s_pot);
}

int fruits_active_index(void)
{
    return atomic_load(&s_active_index);
}

static esp_err_t read_pad_raw(touch_pad_t pad, uint32_t *out)
{
    uint32_t raw = 0;
    esp_err_t err = touch_pad_read_raw_data(pad, &raw);
    if (err != ESP_OK) {
        return err;
    }
    *out = raw;
    return ESP_OK;
}

static esp_err_t calibrate_baseline(void)
{
    ESP_LOGW(TAG, "Do not touch apricot/ginger during calibration");
    vTaskDelay(pdMS_TO_TICKS(BASELINE_WAIT_MS));

    for (int i = 0; i < fruits_count(); i++) {
        uint64_t sum = 0;

        for (int sample = 0; sample < BASELINE_SAMPLES; sample++) {
            uint32_t raw = 0;
            esp_err_t err = read_pad_raw(s_fruits[i].pad, &raw);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "touch read failed on pad %d", (int)s_fruits[i].pad);
                return err;
            }

            sum += raw;
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        s_fruits[i].baseline = (uint32_t)(sum / BASELINE_SAMPLES);
        s_fruits[i].ema = (float)s_fruits[i].baseline;

        ESP_LOGI(TAG, "%s pad %d baseline = %" PRIu32,
                 s_fruits[i].name,
                 (int)s_fruits[i].pad,
                 s_fruits[i].baseline);
    }

    return ESP_OK;
}

static void fruits_task(void *arg)
{
    (void)arg;
    int last_active = -1;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(TOUCH_PERIOD_MS));

        uint32_t best_delta = 0;
        int best_index = -1;

        for (int i = 0; i < fruits_count(); i++) {
            uint32_t raw = 0;
            if (read_pad_raw(s_fruits[i].pad, &raw) != ESP_OK) {
                continue;
            }

            s_fruits[i].ema =
                s_fruits[i].ema * (1.0f - EMA_ALPHA) +
                (float)raw * EMA_ALPHA;

            int32_t delta_signed = (int32_t)s_fruits[i].ema - (int32_t)s_fruits[i].baseline;
            uint32_t delta = delta_signed > 0 ? (uint32_t)delta_signed : 0;

            if (delta > best_delta) {
                best_delta = delta;
                best_index = i;
            }
        }

        if (best_index >= 0 && best_delta > ACTIVE_DELTA_THRESHOLD) {
            uint16_t pot = delta_to_pot(best_delta);

            atomic_store(&s_touching, true);
            atomic_store(&s_pot, pot);
            atomic_store(&s_active_index, best_index);

            if (best_index != last_active) {
                ESP_LOGI(TAG, "active: %s, delta=%" PRIu32 ", pot=%u, freq=%" PRIu32 " Hz",
                         s_fruits[best_index].name,
                         best_delta,
                         (unsigned)pot,
                         s_fruits[best_index].freq_hz);
                last_active = best_index;
            }

            buzzer_play_tone(s_fruits[best_index].freq_hz, BUZZER_NOTE_MS);
        } else {
            atomic_store(&s_touching, false);
            atomic_store(&s_pot, 0);
            atomic_store(&s_active_index, -1);
            last_active = -1;
        }
    }
}

esp_err_t fruits_init(void)
{
    ESP_LOGI(TAG, "init fruit piano");

    buzzer_init(BUZZER_GPIO);

    ESP_ERROR_CHECK(touch_pad_init());

    for (int i = 0; i < fruits_count(); i++) {
        ESP_ERROR_CHECK(touch_pad_config(s_fruits[i].pad));
    }

    ESP_ERROR_CHECK(touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER));
    ESP_ERROR_CHECK(touch_pad_fsm_start());

    esp_err_t err = calibrate_baseline();
    if (err != ESP_OK) {
        return err;
    }

    BaseType_t task_ok = xTaskCreate(
        fruits_task,
        "fruits_task",
        4096,
        NULL,
        5,
        NULL
    );

    return task_ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
