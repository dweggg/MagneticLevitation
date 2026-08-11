#include "protocol.h"
#include <stdio.h>

void protocol_init(void)
{
    /* TODO: setup packet buffers and callbacks */
    (void)printf("protocol_init\n");
}

int protocol_process_byte(uint8_t b)
{
    /* TODO: parse incoming bytes; return 1 if packet complete */
    (void)b;
    return 0;
}
