#include "slave_web_control.h"
#include "slave_master_status.h"
static WebRecord g_active, g_cache[4];
static uint16_t g_flow;
static uint8_t g_next;
static uint8_t g_reason, g_cache_reason[4];
SlaveWebControlDiagnostics SlaveWebControlDiag;
static void Finish(uint8_t state, uint8_t reason, uint32_t now)
{ g_active.state=state; g_active.tick=now; g_cache[g_next]=g_active; g_cache_reason[g_next]=reason;g_next=(uint8_t)((g_next+1U)%4U); g_active.command=0U; }
void SlaveWebControl_Init(void)
{ memset(&g_active,0,sizeof(g_active)); memset(g_cache,0,sizeof(g_cache));memset(g_cache_reason,0,sizeof(g_cache_reason));memset(&SlaveWebControlDiag,0,sizeof(SlaveWebControlDiag));g_reason=0U;g_flow=0U;g_next=0U; }
void SlaveWebControl_Process(uint32_t now)
{
    if(!g_active.command)return;
    if(g_active.state==WEB_QUEUED && (uint32_t)(now-g_active.tick)>=WEB_QUEUE_MS)
    {SlaveWebControlDiag.queue_timeout++;Finish(WEB_UNKNOWN,WEB_REASON_QUEUE_TIMEOUT,now);}
    else if((uint32_t)(now-g_active.tick)>=WEB_TIMEOUT_MS)
    {SlaveWebControlDiag.result_timeout++;Finish(WEB_UNKNOWN,WEB_REASON_RESULT_TIMEOUT,now);}
}
uint16_t SlaveWebControl_Submit(const uint8_t *p,uint32_t now,uint8_t *state)
{
    WebRecord *r; SlaveMasterStatus master;
    *state=WEB_UNKNOWN; if(!Web_Valid(p,WEB_REQUEST_SIZE)) return 400U;
    SlaveWebControl_Process(now); r=Web_Find(g_cache,p+3,now);
    if(g_active.command && !memcmp(g_active.id,p+3,8U)) r=&g_active;
    if(r){if(!Web_Same(r,p))return 409U;*state=r->state;return r->state>=WEB_QUEUED?202U:200U;}
    if(g_active.command){*state=WEB_BUSY;return 409U;}
    SlaveMasterStatus_Get(now,&master); if(!master.online)return 503U;
    memcpy(g_active.id,p+3,8U);g_active.command=p[0];g_active.channel=p[1];g_active.value=p[2];g_active.state=WEB_QUEUED;g_active.tick=now;
    ++g_flow;g_reason=WEB_REASON_NONE;SlaveWebControlDiag.submitted++;*state=WEB_QUEUED;return 202U;
}
uint8_t SlaveWebControl_Status(const uint8_t *id,uint32_t now)
{ WebRecord *r;SlaveWebControl_Process(now);if(g_active.command&&!memcmp(id,g_active.id,8U))return g_active.state;r=Web_Find(g_cache,id,now);return r?r->state:WEB_MISSING; }
uint8_t SlaveWebControl_Prepare(uint8_t *p,uint16_t *flow,uint32_t now)
{ SlaveWebControl_Process(now);if(!g_active.command||g_active.state!=WEB_QUEUED)return 0U;p[0]=g_active.command;p[1]=g_active.channel;p[2]=g_active.value;memcpy(p+3,g_active.id,8U);*flow=g_flow;return 1U; }
void SlaveWebControl_Sent(uint8_t success,uint32_t now)
{ if(!g_active.command||g_active.state!=WEB_QUEUED)return;if(success){g_active.state=WEB_WAITING;SlaveWebControlDiag.sent++;}else{SlaveWebControlDiag.tx_failed++;Finish(WEB_UNKNOWN,WEB_REASON_TX_FAILED,now);} }
void SlaveWebControl_Accept(uint16_t flow,const uint8_t *p,uint8_t size,uint32_t now)
{
    uint8_t reason;
    SlaveWebControl_Process(now);
    if(!g_active.command||g_active.state!=WEB_WAITING||flow!=g_flow||!p||size!=WEB_RESULT_SIZE||p[8]>WEB_BUSY||memcmp(p,g_active.id,8U))
    {SlaveWebControlDiag.unmatched++;return;}
    reason=(p[8]==WEB_OK)?WEB_REASON_NONE:(p[8]==WEB_FAILED)?WEB_REASON_REJECTED:(p[8]==WEB_BUSY)?WEB_REASON_MASTER_BUSY:WEB_REASON_REMOTE_UNKNOWN;
    SlaveWebControlDiag.received++;Finish(p[8],reason,now);
}
uint8_t SlaveWebControl_Reason(const uint8_t *id,uint32_t now)
{
    WebRecord *r;SlaveWebControl_Process(now);
    if(g_active.command&&!memcmp(g_active.id,id,8U))return g_reason;
    r=Web_Find(g_cache,id,now);return r?g_cache_reason[r-g_cache]:WEB_REASON_MISSING;
}
const char *SlaveWebControl_ReasonName(uint8_t reason)
{
    static const char *const names[]={"none","invalid_request","id_conflict","local_busy","master_offline","queue_timeout","radio_tx_failed","result_timeout","device_rejected","master_busy","remote_unknown","unknown_request"};
    return reason<sizeof(names)/sizeof(names[0])?names[reason]:"remote_unknown";
}
const char *SlaveWebControl_PhaseName(uint8_t state)
{return state==WEB_QUEUED?"radio_queued":state==WEB_WAITING?"awaiting_result":"finished";}
