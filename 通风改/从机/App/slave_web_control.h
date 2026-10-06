#ifndef SLAVE_WEB_CONTROL_H
#define SLAVE_WEB_CONTROL_H
#include "../../web_control_wire.h"
enum { WEB_REASON_NONE, WEB_REASON_INVALID, WEB_REASON_CONFLICT, WEB_REASON_LOCAL_BUSY,
       WEB_REASON_OFFLINE, WEB_REASON_QUEUE_TIMEOUT, WEB_REASON_TX_FAILED,
       WEB_REASON_RESULT_TIMEOUT, WEB_REASON_REJECTED, WEB_REASON_MASTER_BUSY,
       WEB_REASON_REMOTE_UNKNOWN, WEB_REASON_MISSING };
typedef struct {
    uint32_t submitted, sent, received, queue_timeout, tx_failed, result_timeout, unmatched;
} SlaveWebControlDiagnostics;
extern SlaveWebControlDiagnostics SlaveWebControlDiag;
uint8_t SlaveWebControl_Reason(const uint8_t *id, uint32_t now);
const char *SlaveWebControl_ReasonName(uint8_t reason);
const char *SlaveWebControl_PhaseName(uint8_t state);
void SlaveWebControl_Init(void);
/* Returns HTTP status; record state distinguishes acceptance from execution. */
uint16_t SlaveWebControl_Submit(const uint8_t *payload, uint32_t now, uint8_t *state);
uint8_t SlaveWebControl_Status(const uint8_t *id, uint32_t now);
uint8_t SlaveWebControl_Prepare(uint8_t *payload, uint16_t *flow, uint32_t now);
void SlaveWebControl_Sent(uint8_t success, uint32_t now);
void SlaveWebControl_Accept(uint16_t flow, const uint8_t *payload, uint8_t size, uint32_t now);
void SlaveWebControl_Process(uint32_t now);
#endif
