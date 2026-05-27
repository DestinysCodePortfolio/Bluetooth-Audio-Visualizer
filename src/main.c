#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"

extern void a2dp_sink_setup(void);

int main(void) {
    stdio_init_all();

    if (cyw43_arch_init()) {
        printf("CYW43 init failed\n");
        return -1;
    }

    a2dp_sink_setup();

    // Hand control to BTstack — runs forever
    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();

    return 0;
}