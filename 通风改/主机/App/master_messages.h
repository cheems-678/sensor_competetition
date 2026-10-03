#ifndef MASTER_MESSAGES_H
#define MASTER_MESSAGES_H

#include "lora_protocol.h"

typedef enum
{
    MASTER_EVENT_NONE = 0,
    MASTER_EVENT_LORA_MESSAGE
} MasterEventType;

typedef struct
{
    MasterEventType type;
    uint32_t received_tick; /* Complete LoRa frame reception, local metadata only. */
    union
    {
        LoRaMessage lora_message;
    } data;
} MasterEvent;

#endif /* MASTER_MESSAGES_H */
