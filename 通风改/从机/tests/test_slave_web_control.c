#include <assert.h>
#include <stdio.h>
#include "slave_web_control.h"
#include "slave_master_status.h"
static void online(uint32_t now)
{ uint8_t p[26];memset(p,255,sizeof(p));p[0]=1;p[1]=0;p[2]=128;assert(SlaveMasterStatus_Accept(1,p,26,now)); }
int main(void)
{
    uint8_t p[11]={0x10,4,100,1,2,3,4,5,6,7,8},copy[11],r[9],state;uint16_t flow;
    SlaveWebControl_Init();SlaveMasterStatus_Init();
    assert(SlaveWebControl_Submit(p,0,&state)==503);online(0);
    assert(SlaveWebControl_Submit(p,0,&state)==202&&state==WEB_QUEUED);
    assert(SlaveWebControl_Submit(p,0,&state)==202);
    p[2]=50;assert(SlaveWebControl_Submit(p,0,&state)==409);p[2]=100;
    assert(SlaveWebControl_Prepare(copy,&flow,50)&&!memcmp(p,copy,11));
    SlaveWebControl_Sent(1,50);assert(!SlaveWebControl_Prepare(copy,&flow,60));
    assert(!strcmp(SlaveWebControl_PhaseName(WEB_WAITING),"awaiting_result"));
    memcpy(r,p+3,8);r[8]=WEB_OK;
    SlaveWebControl_Accept((uint16_t)(flow+1),r,9,60);assert(SlaveWebControl_Status(p+3,60)==WEB_WAITING);
    SlaveWebControl_Accept(flow,r,8,60);assert(SlaveWebControl_Status(p+3,60)==WEB_WAITING);
    SlaveWebControl_Accept(flow,r,9,60);assert(SlaveWebControl_Status(p+3,60)==WEB_OK);
    assert(SlaveWebControl_Submit(p,100,&state)==200&&state==WEB_OK);
    assert(SlaveWebControl_Status(p+3,30060)==WEB_MISSING);
    assert(SlaveWebControl_Reason(p+3,30060)==WEB_REASON_MISSING);
    assert(SlaveWebControlDiag.submitted==1U&&SlaveWebControlDiag.sent==1U&&SlaveWebControlDiag.received==1U&&SlaveWebControlDiag.unmatched==2U);
    SlaveWebControl_Init();SlaveMasterStatus_Init();online(0xFFFFFF00U);
    assert(SlaveWebControl_Submit(p,0xFFFFFF00U,&state)==202);
    assert(!SlaveWebControl_Prepare(copy,&flow,744U));assert(SlaveWebControl_Status(p+3,744U)==WEB_UNKNOWN);
    assert(SlaveWebControl_Reason(p+3,744U)==WEB_REASON_QUEUE_TIMEOUT);
    p[3]++;SlaveMasterStatus_Init();online(1000);
    assert(SlaveWebControl_Submit(p,1000,&state)==202);assert(SlaveWebControl_Prepare(copy,&flow,1050));SlaveWebControl_Sent(1,1050);
    assert(SlaveWebControl_Status(p+3,8999)==WEB_WAITING);assert(SlaveWebControl_Status(p+3,9000)==WEB_UNKNOWN);
    assert(SlaveWebControl_Reason(p+3,9000)==WEB_REASON_RESULT_TIMEOUT);
    memcpy(r,p+3,8U);r[8]=WEB_OK;SlaveWebControl_Accept(flow,r,9U,9001U);
    assert(SlaveWebControl_Status(p+3,9001U)==WEB_UNKNOWN);
    p[1]=5;assert(SlaveWebControl_Submit(p,9000,&state)==400);
    p[1]=1;p[3]++;SlaveMasterStatus_Init();online(10000U);
    assert(SlaveWebControl_Submit(p,10000U,&state)==202U);SlaveWebControl_Sent(0U,10001U);
    assert(SlaveWebControl_Reason(p+3,10001U)==WEB_REASON_TX_FAILED);
    p[3]++;assert(SlaveWebControl_Submit(p,10002U,&state)==202U);assert(SlaveWebControl_Prepare(copy,&flow,10003U));
    SlaveWebControl_Sent(1U,10003U);memcpy(r,p+3,8U);r[8]=WEB_FAILED;SlaveWebControl_Accept(flow,r,9U,10004U);
    assert(SlaveWebControl_Reason(p+3,10004U)==WEB_REASON_REJECTED);
    puts("PASS: slave web command admission, duplicates, conflicts, cached results, queue/overall timeout and tick wrap");return 0;
}
