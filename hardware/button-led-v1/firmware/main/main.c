#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_GPIO GPIO_NUM_34
#define LED_GPIO GPIO_NUM_33

void app_main(void)
{
    const gpio_config_t output = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&output));
    ESP_ERROR_CHECK(gpio_set_level(LED_GPIO, 0));
    const gpio_config_t input = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&input));
    int candidate = gpio_get_level(BUTTON_GPIO);
    int stable = candidate;
    unsigned stable_samples = 0;
    int led_on = 0;
    ESP_LOGI("button_led", "GPIO34 button -> GPIO33 LED; 30ms debounce");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        const int level = gpio_get_level(BUTTON_GPIO);
        if (level != candidate) {
            candidate = level;
            stable_samples = 0;
        } else if (stable_samples < 3) {
            ++stable_samples;
        }
        if (stable_samples == 3 && candidate != stable) {
            stable = candidate;
            if (stable == 0) {
                led_on = !led_on;
                ESP_ERROR_CHECK(gpio_set_level(LED_GPIO, led_on));
                ESP_LOGI("button_led", "LED %s", led_on ? "ON" : "OFF");
            }
        }
    }
}
