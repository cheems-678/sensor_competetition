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
    union
    {
        LoRaMessage lora_message;
    } data;
} MasterEvent;

#endif /* MASTER_MESSAGES_H */
