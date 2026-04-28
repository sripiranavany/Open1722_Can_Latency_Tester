#include <zephyr/kernel.h>
#include "timestamp.h"
#include "can_handler.h"
#include "button.h"

#define SLEEP_TIME_MS 100

int main(void){
    timestamp_init();
    can_handler_init();
    button_init();

    printk("==========================================\n");
    printk("    CAN Latency Tester - Initialized     \n");
    printk("==========================================\n");
    printk("  Send Interval: %d ms\n", SLEEP_TIME_MS);
    printk("==========================================\n");

    while(1){
        if(can_send_enabled){
            uint32_t ts = get_hw_timestamp_us();
            int ret = can_send_frame(ts,tx_sequence_num);
            if(ret != 0){
                printk("[TX] Send failed [%d] seq=%u\n", ret, tx_sequence_num);
            } else {
                printk("[TX] Seq=%u @ %u us\n", tx_sequence_num, ts);
            }
            tx_sequence_num++;
        }
        k_msleep(SLEEP_TIME_MS);
    }
    return 0;
}