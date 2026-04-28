#include "button.h"
#include "can_handler.h"
#include "stats.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define SW0_NODE DT_ALIAS(sw0)
#define BUTTON_DEBOUNCE_MS 500

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
static struct gpio_callback button_cb_data;

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    static int64_t last_press_time = 0;
    int64_t now = k_uptime_get();

    if (now - last_press_time < BUTTON_DEBOUNCE_MS) {
        return;
    }
    last_press_time = now;

    can_send_enabled = !can_send_enabled;

    if (can_send_enabled){
        printk("[%u ms] *** CAN SENDING RESUMED ***\n", k_uptime_get_32());
    } else {
        printk("[%u ms] *** CAN SENDING PAUSED ***\n", k_uptime_get_32());
        stats_print(tx_sequence_num);
    }
}

int button_init(void)
{
    int ret;

    if (!gpio_is_ready_dt(&button)){
        printk("Error: button device not ready\n");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret != 0){
        printk("Error %d: failed to configure button pin\n", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if(ret != 0){
        printk("Error %d: failed to configure button interrupt\n", ret);
        return ret;
    }

    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
    return 0;
}