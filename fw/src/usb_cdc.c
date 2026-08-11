#include "../inc/usb_cdc.h"
#include <stdio.h>

void usb_cdc_init(void)
{
    /* TODO: initialize USB CDC descriptors and endpoints */
    (void)printf("usb_cdc_init\n");
}

int usb_cdc_send(const uint8_t *buf, unsigned len)
{
    /* TODO: queue data for USB transmission; return bytes accepted */
    (void)buf;
    (void)len;
    return 0;
}
