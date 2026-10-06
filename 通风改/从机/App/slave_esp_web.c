#include "slave_web_control.h"
#include "slave_esp_web.h"
#include "esp_at_uart.h"
#include "slave_bme280.h"
#include "slave_mq2.h"
#include "slave_hcsr04.h"
#include "slave_master_status.h"
#include "slave_servo_test.h"
#include "slave_web_page.h"
#include "esp_ap_config.local.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* Include the terminating NUL in this compile-time passphrase check. */
typedef char EspApPasswordLengthCheck[
    (sizeof(SLAVE_ESP_AP_PASSWORD) >= 9U &&
     sizeof(SLAVE_ESP_AP_PASSWORD) <= 64U) ? 1 : -1];

#define CONNECTIONS 5U
#define CHUNK_SIZE 512U
#define TX_TIMEOUT 250U
#define AT_TIMEOUT 2000U
#define RETRY_MS 5000U
#define REQUEST_MAX 2048U
#define LINE_SIZE 128U
#define HEADER_SKIP 32U
#define CONTROL_ID_VALID 64U
#define CONTROL_HTTP 128U

enum { REQUEST_NONE, REQUEST_PAGE, REQUEST_JSON, REQUEST_ICON,
       REQUEST_CONTROL_POST, REQUEST_CONTROL_GET, REQUEST_NOT_FOUND, REQUEST_METHOD, REQUEST_BAD, REQUEST_CLOSE };
enum { EVENT_OK = 1, EVENT_ERROR = 2, EVENT_PROMPT = 4,
       EVENT_SENT = 8, EVENT_READY = 16 };
typedef struct {
    uint32_t started, tail;
    uint16_t total, line_length;
    uint8_t live, first_line, kind, pending;
    uint8_t control_id[8], flags, body_length, control_state;
    uint16_t http_status;
    char line[LINE_SIZE];
} Connection;
typedef struct { const char *command, *fallback, *old; uint8_t optional; } InitCommand;
static const InitCommand g_init[] = {
    /* Some stock ESP-01S AT firmware reports ERROR for an empty CRLF command.
       Probe without a leading blank line so its ERROR cannot mask AT's OK. */
    {"AT\r\n", 0, 0, 0}, {"ATE0\r\n", 0, 0, 0},
    {"AT+GMR\r\n", 0, 0, 1}, {"AT+SYSSTORE=0\r\n", 0, 0, 1},
    {"AT+CWMODE_CUR=2\r\n", "AT+CWMODE=2\r\n", 0, 0},
    {"AT+CIPSERVER=0\r\n", 0, 0, 1}, {"AT+CIPMODE=0\r\n", 0, 0, 0},
    {"AT+CWSAP_CUR=\"" SLAVE_ESP_SSID "\",\"" SLAVE_ESP_AP_PASSWORD "\",6,3\r\n",
     "AT+CWSAP=\"" SLAVE_ESP_SSID "\",\"" SLAVE_ESP_AP_PASSWORD "\",6,3\r\n", 0, 0},
    {"AT+CIPAP_CUR=\"" SLAVE_ESP_IP "\",\"" SLAVE_ESP_IP "\",\"255.255.255.0\"\r\n",
     "AT+CIPAP=\"" SLAVE_ESP_IP "\",\"" SLAVE_ESP_IP "\",\"255.255.255.0\"\r\n", 0, 0},
    /* NONOS: mode,enable; new ESP-AT: operate,mode bitmask. */
    {"AT+CWDHCP_CUR=0,1\r\n", "AT+CWDHCP=1,2\r\n", "AT+CWDHCP=0,1\r\n", 0},
    {"AT+CIPMUX=1\r\n", 0, 0, 0}, {"AT+CIPDINFO=0\r\n", 0, 0, 1},
    {"AT+CIPRECVMODE=0\r\n", 0, 0, 1},
    {"AT+CIPSERVER=1,80\r\n", 0, 0, 0}, {"AT+CIPSTO=5\r\n", 0, 0, 1}
};
static Connection g_connections[CONNECTIONS];
static char g_line[LINE_SIZE], g_header[192], g_json[1280];
static uint8_t g_tx[CHUNK_SIZE];
static const char *g_body;
static uint16_t g_line_length, g_header_length, g_body_length, g_offset, g_chunk;
static uint32_t g_deadline, g_ipd_left, g_ipd_started, g_now;
static uint32_t g_seen_overflow, g_seen_error, g_response_started;
static uint8_t g_events, g_step, g_variant, g_active, g_next, g_ipd_id, g_lost;
volatile SlaveEspWebDiagnostics SlaveEspWebDiag;

static uint8_t Due(uint32_t now, uint32_t deadline)
{ return (uint8_t)((int32_t)(now - deadline) >= 0); }
static void State(uint32_t state, uint32_t delay)
{
    SlaveEspWebDiag.state = state;
    g_deadline = g_now + delay;
}
static uint8_t JsonAppend(unsigned *length, const char *format, ...)
{
    int added;
    va_list args;
    if (*length >= sizeof(g_json)) { return 0U; }
    va_start(args, format);
    added = vsnprintf(g_json + *length, sizeof(g_json) - *length, format, args);
    va_end(args);
    if (added < 0 || (unsigned)added >= sizeof(g_json) - *length) { return 0U; }
    *length += (unsigned)added;
    return 1U;
}
static uint8_t JsonNumber(unsigned *length, uint8_t valid, uint32_t number)
{ return JsonAppend(length, valid != 0U ? "%lu" : "null", (unsigned long)number); }
static uint8_t JsonMonitor(unsigned *length)
{
    SlaveMasterStatus master;
    uint8_t i;
    SlaveMasterStatus_Get(g_now, &master);
    if (!JsonAppend(length, ",\"master\":{\"online\":%s,\"rx_age_ms\":", master.online ? "true" : "false") ||
        !JsonNumber(length, master.online, master.rx_age_ms) ||
        !JsonAppend(length, ",\"uptime_ms\":") || !JsonNumber(length, master.online, master.uptime_ms) ||
        !JsonAppend(length, ",\"sample_seq\":") || !JsonNumber(length, master.online, master.sample_seq) ||
        !JsonAppend(length, ",\"valid\":%s,\"age_ms\":", master.bme_valid ? "true" : "false") ||
        !JsonNumber(length, master.bme_valid, master.bme_age_ms) ||
        !JsonAppend(length, ",\"temperature_x10\":") ||
        !JsonAppend(length, master.bme_valid ? "%d" : "null", (int)master.temperature_x10) ||
        !JsonAppend(length, ",\"humidity_x10\":") || !JsonNumber(length, master.bme_valid, master.humidity_x10) ||
        !JsonAppend(length, ",\"pressure_pa\":") || !JsonNumber(length, master.bme_valid, master.pressure_pa) ||
        !JsonAppend(length, ",\"rain\":") || !JsonNumber(length, master.rain != 0xFFU, master.rain) ||
        !JsonAppend(length, ",\"dark\":") || !JsonNumber(length, master.dark != 0xFFU, master.dark) ||
        !JsonAppend(length, ",\"light_on\":") || !JsonNumber(length, master.light_on != 0xFFU, master.light_on) ||
        !JsonAppend(length, ",\"fan_pwm\":[")) { return 0U; }
    for (i = 0U; i < 4U; i++)
    {
        if ((i != 0U && !JsonAppend(length, ",")) ||
            !JsonNumber(length, master.fan_pwm[i] != 0xFFU, master.fan_pwm[i])) { return 0U; }
    }
    if (!JsonAppend(length, "]},\"windows\":[")) { return 0U; }
    for (i = 1U; i <= SLAVE_SERVO_COUNT; i++)
    {
        const volatile SlaveServoTestDiagnostics *window = SlaveServoTest_GetDiagnostics(i);
        if (window == 0 || !JsonAppend(length,
            "%s{\"state\":%lu,\"pulse_us\":%lu,\"error\":%lu}", i == 1U ? "" : ",",
            (unsigned long)window->state, (unsigned long)window->pulse_us,
            (unsigned long)window->last_error)) { return 0U; }
    }
    return JsonAppend(length, "]}");
}
static void ClearClients(void)
{
    memset(g_connections, 0, sizeof(g_connections));
    SlaveEspWebDiag.active_connections = 0U;
    g_active = CONNECTIONS;
    g_lost = 0U;
}
static void ClearParser(void)
{
    g_line_length = 0U;
    g_ipd_left = 0U;
    g_events = 0U;
    EspAtUart_ClearRx();
}
static void Fail(uint32_t error)
{
    SlaveEspWebDiag.last_error = error;
    SlaveEspWebDiag.ready = 0U;
    if (error == SLAVE_ESP_ERROR_TIMEOUT) { SlaveEspWebDiag.timeout_count++; }
    EspAtUart_AbortTx();
    ClearClients();
    ClearParser();
    /* If AT was detected, pad at least the entire last announced chunk.
       In raw mode this finishes its remaining bytes; in command mode spaces
       form a harmless invalid line. Only then send CRLF and AT+RST.
       Never issue a new command into an ambiguous CIPSEND payload. */
    if (SlaveEspWebDiag.at_detected != 0U)
    {
        SlaveEspWebDiag.recovery_count++;
        State(SLAVE_ESP_RECOVER_START, 500U);
    }
    else { State(SLAVE_ESP_BACKOFF, RETRY_MS); }
}
static void Backoff(uint32_t error)
{
    SlaveEspWebDiag.last_error = error;
    SlaveEspWebDiag.ready = 0U;
    if (error == SLAVE_ESP_ERROR_TIMEOUT) { SlaveEspWebDiag.timeout_count++; }
    EspAtUart_AbortTx();
    ClearClients();
    ClearParser();
    State(SLAVE_ESP_BACKOFF, RETRY_MS);
}
static uint8_t Start(const uint8_t *data, uint16_t size, uint32_t state)
{
    g_events = 0U;
    if (EspAtUart_StartTx(data, size) == 0U) { Fail(SLAVE_ESP_ERROR_UART); return 0U; }
    State(state, TX_TIMEOUT);
    return 1U;
}
static uint8_t TxDone(void)
{
    uint8_t result = EspAtUart_PollTx();
    if (result == 1U) { return 1U; }
    if (result == 2U || Due(g_now, g_deadline))
    { Fail(result == 2U ? SLAVE_ESP_ERROR_UART : SLAVE_ESP_ERROR_TIMEOUT); }
    return 0U;
}
static void SetPending(Connection *c, uint8_t kind)
{
    c->kind = kind;
    c->pending = 1U;
    SlaveEspWebDiag.request_count++;
    if (kind >= REQUEST_NOT_FOUND) { SlaveEspWebDiag.rejected_count++; }
}
static uint8_t Hex(const char *s,uint8_t *id)
{ uint8_t i,v;for(i=0;i<16U;i++){char c=s[i];if(c>='0'&&c<='9')v=(uint8_t)(c-'0');else if(c>='a'&&c<='f')v=(uint8_t)(c-'a'+10);else if(c>='A'&&c<='F')v=(uint8_t)(c-'A'+10);else return 0U;if(!(i&1U))id[i/2U]=(uint8_t)(v<<4U);else id[i/2U]|=v;}return 1U; }
static const char *SkipSpace(const char *p){while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;return p;}
static uint8_t StringToken(const char **p,char *out,uint8_t cap)
{ uint8_t n=0U;*p=SkipSpace(*p);if(*(*p)++!='"')return 0U;while(**p&&**p!='"'){if(n+1U>=cap||**p=='\\')return 0U;out[n++]=*(*p)++;}if(*(*p)++!='"')return 0U;out[n]=0;return 1U; }
static uint8_t ControlJson(Connection *c,uint8_t *payload)
{
    const char *p=SkipSpace(c->line);uint8_t seen=0U,bit;char key[16],value[20];
    memset(payload,0,11U);if(*p++!='{')return 0U;
    for(;;)
    {
        uint16_t number=0U;
        if(!StringToken(&p,key,sizeof(key))){return 0U;}
        p=SkipSpace(p);if(*p++!=':')return 0U;
        if(!strcmp(key,"request_id")){bit=1U;if(!StringToken(&p,value,sizeof(value))||strlen(value)!=16U||!Hex(value,payload+3))return 0U;}
        else if(!strcmp(key,"type")){bit=2U;if(!StringToken(&p,value,sizeof(value)))return 0U;if(!strcmp(value,"fan"))payload[0]=0x10U;else if(!strcmp(value,"window"))payload[0]=0x11U;else return 0U;}
        else if(!strcmp(key,"channel")||!strcmp(key,"value"))
        {bit=(uint8_t)(!strcmp(key,"channel")?4U:8U);p=SkipSpace(p);if(*p<'0'||*p>'9')return 0U;if(*p=='0'&&p[1]>='0'&&p[1]<='9')return 0U;
         while(*p>='0'&&*p<='9'){number=(uint16_t)(number*10U+(uint16_t)(*p++-'0'));if(number>255U)return 0U;}payload[bit==4U?1U:2U]=(uint8_t)number;}
        else return 0U;
        if(seen&bit){return 0U;}
        seen|=bit;p=SkipSpace(p);if(*p=='}'){p=SkipSpace(p+1);return seen==15U&&!*p&&Web_Valid(payload,11U);}if(*p++!=',')return 0U;
    }
}
static uint8_t HeaderName(const char *s,const char *name)
{while(*name){char a=*s++,b=*name++;if(a>='A'&&a<='Z')a=(char)(a+32);if(a!=b)return 0U;}return 1U;}
static uint8_t CriticalHeader(const char *s)
{
    return HeaderName(s,"content-length:")||HeaderName(s,"content-type:")||
           HeaderName(s,"origin:")||HeaderName(s,"host:")||HeaderName(s,"transfer-encoding:");
}
static uint8_t Route(const char *line)
{
    if (strcmp(line, "GET / HTTP/1.1") == 0 || strcmp(line, "GET / HTTP/1.0") == 0)
    { return REQUEST_PAGE; }
    if (strcmp(line, "GET /api/telemetry HTTP/1.1") == 0 ||
        strcmp(line, "GET /api/telemetry HTTP/1.0") == 0) { return REQUEST_JSON; }
    if (strcmp(line, "GET /favicon.ico HTTP/1.1") == 0 ||
        strcmp(line, "GET /favicon.ico HTTP/1.0") == 0) { return REQUEST_ICON; }
    if(!strcmp(line,"POST /api/control HTTP/1.1"))return REQUEST_CONTROL_POST;
    if(!strncmp(line,"GET /api/control?id=",20U))return REQUEST_CONTROL_GET;
    if (strncmp(line, "GET ", 4U) == 0) { return REQUEST_NOT_FOUND; }
    return REQUEST_METHOD;
}
static uint8_t Number(const char **cursor,uint32_t limit,uint32_t *value);
static void HttpByte(uint8_t id,uint8_t byte)
{
    Connection *c; if(id>=CONNECTIONS||!SlaveEspWebDiag.ready)return;c=g_connections+id;
    if(!c->live){c->live=1U;c->started=g_now;SlaveEspWebDiag.active_connections++;}
    if(c->pending||id==g_active)return;
    if(++c->total>REQUEST_MAX||!byte){SetPending(c,REQUEST_BAD);return;}
    if(c->flags&HEADER_SKIP)
    {
        if(byte=='\n')
        {
            if(c->tail!='\r'){SetPending(c,REQUEST_BAD);return;}
            c->flags&=(uint8_t)~HEADER_SKIP;c->line_length=0U;
        }
        c->tail=byte;return;
    }
    if(c->first_line&&c->kind!=REQUEST_CONTROL_POST){c->tail=(c->tail<<8U)|byte;if(c->tail==0x0D0A0D0AU)SetPending(c,c->kind);return;}
    if(c->flags&16U)
    {
        if(c->line_length>=c->body_length){SetPending(c,REQUEST_BAD);return;}
        c->line[c->line_length++]=(char)byte;
        if(c->line_length==c->body_length)
        {
            uint8_t payload[11];c->line[c->line_length]=0;
            if(!ControlJson(c,payload)){SetPending(c,REQUEST_BAD);return;}
            memcpy(c->control_id,payload+3,8U);c->flags|=CONTROL_ID_VALID;c->http_status=SlaveWebControl_Submit(payload,g_now,&c->control_state);SetPending(c,REQUEST_CONTROL_POST);
        }return;
    }
    if(byte!='\n')
    {
        if(c->line_length+1U>=LINE_SIZE)
        {
            c->line[c->line_length]=0;
            /* Only discard unrelated header values, never a request line or a checked field. */
            if(!c->first_line||CriticalHeader(c->line)||!memchr(c->line,':',c->line_length))
            {SetPending(c,REQUEST_BAD);return;}
            c->flags|=HEADER_SKIP;c->tail=byte;return;
        }
        c->line[c->line_length++]=(char)byte;return;
    }
    if(!c->line_length||c->line[c->line_length-1U]!='\r'){SetPending(c,REQUEST_BAD);return;}
    c->line[--c->line_length]=0;
    if(!c->first_line)
    {
        c->kind=Route(c->line);c->first_line=1U;c->tail=0x0D0AU;
        if(c->kind==REQUEST_CONTROL_POST||c->kind==REQUEST_CONTROL_GET)c->flags|=CONTROL_HTTP;
        if(c->kind==REQUEST_CONTROL_GET && (strlen(c->line)!=45U || !Hex(c->line+20,c->control_id)||strcmp(c->line+36," HTTP/1.1"))){SetPending(c,REQUEST_BAD);return;}
        if(c->kind==REQUEST_CONTROL_GET)c->flags|=CONTROL_ID_VALID;
    }
    else if(!c->line_length)
    {
        if(c->kind==REQUEST_CONTROL_POST)
        {if((c->flags&15U)!=15U||!c->body_length){SetPending(c,REQUEST_BAD);return;}c->flags|=16U;}
        else SetPending(c,c->kind);
    }
    else if(c->kind==REQUEST_CONTROL_POST)
    {
        if(HeaderName(c->line,"content-length:"))
        {const char *q=SkipSpace(c->line+15);uint32_t size;if((c->flags&1U)||!Number(&q,96U,&size)||*SkipSpace(q)||!size){SetPending(c,REQUEST_BAD);return;}c->body_length=(uint8_t)size;c->flags|=1U;}
        else if(HeaderName(c->line,"content-type:")){if((c->flags&2U)||strcmp(SkipSpace(c->line+13),"application/json")){SetPending(c,REQUEST_BAD);return;}c->flags|=2U;}
        else if(HeaderName(c->line,"origin:")){if((c->flags&4U)||strcmp(SkipSpace(c->line+7),"http://" SLAVE_ESP_IP)){SetPending(c,REQUEST_BAD);return;}c->flags|=4U;}
        else if(HeaderName(c->line,"host:")){if((c->flags&8U)||(strcmp(SkipSpace(c->line+5),SLAVE_ESP_IP)&&strcmp(SkipSpace(c->line+5),SLAVE_ESP_IP ":80"))){SetPending(c,REQUEST_BAD);return;}c->flags|=8U;}
        else if(HeaderName(c->line,"transfer-encoding:")){SetPending(c,REQUEST_BAD);return;}
    }
    c->line_length=0U;
}
static void AtLine(void)
{
    unsigned id;
    g_line[g_line_length] = '\0';
    if (strcmp(g_line, "OK") == 0) { g_events |= EVENT_OK; }
    else if (strcmp(g_line, "ERROR") == 0 || strcmp(g_line, "FAIL") == 0 ||
             strcmp(g_line, "SEND FAIL") == 0 || strncmp(g_line, "busy", 4U) == 0)
    { g_events |= EVENT_ERROR; }
    else if (strcmp(g_line, "SEND OK") == 0) { g_events |= EVENT_SENT; }
    else if (strcmp(g_line, "ready") == 0) { g_events |= EVENT_READY; }
    else if (g_step == 2U && strncmp(g_line, "AT version:", 11U) == 0)
    {
        size_t len = strlen(g_line);
        if (len >= sizeof(SlaveEspWebDiag.at_version)) { len = sizeof(SlaveEspWebDiag.at_version) - 1U; }
        memcpy((void *)SlaveEspWebDiag.at_version, g_line, len);
        SlaveEspWebDiag.at_version[len] = '\0';
    }
    else if (g_line[0] >= '0' && g_line[0] <= '4' && g_line[1] == ',')
    {
        id = (unsigned)(g_line[0] - '0');
        if (strcmp(&g_line[2], "CLOSED") == 0)
        {
            if (g_connections[id].live != 0U) { SlaveEspWebDiag.active_connections--; }
            memset(&g_connections[id], 0, sizeof(Connection));
            SlaveEspWebDiag.disconnect_count++;
            if (id == g_active) { g_lost = 1U; }
        }
        else if (strcmp(&g_line[2], "CONNECT") == 0 && SlaveEspWebDiag.ready != 0U)
        {
            if (id == g_active) { g_lost = 2U; }
            if (g_connections[id].live == 0U) { SlaveEspWebDiag.active_connections++; }
            memset(&g_connections[id], 0, sizeof(Connection));
            g_connections[id].live = 1U;
            g_connections[id].started = g_now;
        }
    }
}
static uint8_t Number(const char **cursor, uint32_t limit, uint32_t *value)
{
    const char *p = *cursor;
    uint32_t result = 0U;
    if (*p < '0' || *p > '9') { return 0U; }
    do
    {
        uint32_t digit = (uint32_t)(*p++ - '0');
        if (result > (limit - digit) / 10U) { return 0U; }
        result = result * 10U + digit;
    } while (*p >= '0' && *p <= '9');
    *cursor = p; *value = result;
    return 1U;
}
static void ParseByte(uint8_t byte)
{
    if (g_ipd_left != 0U)
    {
        HttpByte(g_ipd_id, byte);
        g_ipd_left--;
        return;
    }
    if (byte == '>' && g_line_length == 0U)
    { g_events |= EVENT_PROMPT; return; }
    if (byte == ' ' && g_line_length == 0U) { return; } /* ESP prompt's space */
    if (byte == ':' && g_line_length >= 5U && memcmp(g_line, "+IPD,", 5U) == 0)
    {
        uint32_t id = 0U, size = 0U;
        const char *cursor;
        g_line[g_line_length] = '\0';
        cursor = &g_line[5];
        if (Number(&cursor, 255U, &id) == 0U || *cursor++ != ',' ||
            Number(&cursor, 65535U, &size) == 0U ||
            (*cursor != '\0' && *cursor != ',') || size == 0U)
        { Fail(SLAVE_ESP_ERROR_IPD); return; }
        g_ipd_id = (id < CONNECTIONS) ? (uint8_t)id : CONNECTIONS;
        g_ipd_left = size;
        g_ipd_started = g_now;
        g_line_length = 0U;
        return;
    }
    if (byte == '\n')
    {
        if (g_line_length != 0U && g_line[g_line_length - 1U] == '\r') { g_line_length--; }
        AtLine();
        g_line_length = 0U;
    }
    else if (g_line_length + 1U < LINE_SIZE) { g_line[g_line_length++] = (char)byte; }
    else
    {
        /* Losing an IPD prefix means payload could masquerade as AT replies. */
        Fail(SLAVE_ESP_ERROR_IPD);
    }
}
static void BuildResponse(uint8_t kind)
{
    const char *status = "200 OK", *type = "text/plain; charset=utf-8";
    int length;
    g_offset = 0U;
    if (kind == REQUEST_PAGE)
    { g_body = SlaveWebPage; g_body_length = (uint16_t)(sizeof(SlaveWebPage) - 1U); type = "text/html; charset=utf-8"; }
    else if(kind==REQUEST_CONTROL_POST||kind==REQUEST_CONTROL_GET||
            (kind==REQUEST_BAD&&(g_connections[g_active].flags&CONTROL_HTTP)))
    {
        Connection *c=g_connections+g_active;char id[19]="null";uint8_t i,state,reason;
        const char *phase;
        const char *names[]={"success","failed","unknown","busy","queued","waiting","unknown"};
        if(c->flags&CONTROL_ID_VALID)
        {id[0]='"';for(i=0U;i<8U;i++){static const char hex[]="0123456789abcdef";id[1U+2U*i]=hex[c->control_id[i]>>4U];id[2U+2U*i]=hex[c->control_id[i]&15U];}id[17]='"';id[18]=0;}
        state=SlaveWebControl_Status(c->control_id,g_now);
        if(kind==REQUEST_CONTROL_POST&&c->http_status>=400U)state=c->control_state;
        reason=SlaveWebControl_Reason(c->control_id,g_now);phase=SlaveWebControl_PhaseName(state);
        if(kind==REQUEST_BAD){status="400 Bad Request";state=WEB_FAILED;reason=WEB_REASON_INVALID;phase="rejected";}
        if(kind==REQUEST_CONTROL_POST){if(c->http_status==202U)status="202 Accepted";else if(c->http_status==409U)status="409 Conflict";else if(c->http_status==503U)status="503 Service Unavailable";}
        if(kind==REQUEST_CONTROL_POST&&c->http_status>=400U)
        {phase="rejected";reason=c->http_status==503U?WEB_REASON_OFFLINE:state==WEB_BUSY?WEB_REASON_LOCAL_BUSY:WEB_REASON_CONFLICT;}
        if(kind==REQUEST_CONTROL_GET&&state==WEB_MISSING)status="404 Not Found";
        length=snprintf(g_json,sizeof(g_json),"{\"request_id\":%s,\"state\":\"%s\",\"phase\":\"%s\",\"reason\":\"%s\"}",id,names[state],phase,SlaveWebControl_ReasonName(reason));
        if(length<0||(unsigned)length>=sizeof(g_json)){Fail(SLAVE_ESP_ERROR_IPD);return;}
        g_body=g_json;g_body_length=(uint16_t)length;type="application/json; charset=utf-8";
    }
    else if (kind == REQUEST_JSON)
    {
        SlaveBme280Sample sample;
        uint8_t valid = SlaveBme280_GetSample(g_now, &sample);
        uint32_t age = g_now - SlaveBme280Diag.sample_tick;
        type = "application/json; charset=utf-8";
        if (valid != 0U)
        {
            length = snprintf(g_json, sizeof(g_json),
                "{\"device\":\"slave\",\"group\":1,\"uptime_ms\":%lu,\"sample_seq\":%lu,"
                "\"age_ms\":%lu,\"valid\":true,\"temperature_x10\":%d,\"humidity_x10\":%u,\"pressure_pa\":%lu}",
                (unsigned long)g_now, (unsigned long)SlaveBme280Diag.sample_success_count,
                (unsigned long)age, (int)sample.temperature_x10, (unsigned)sample.humidity_x10,
                (unsigned long)sample.pressure_pa);
        }
        else
        {
            length = snprintf(g_json, sizeof(g_json),
                "{\"device\":\"slave\",\"group\":1,\"uptime_ms\":%lu,\"sample_seq\":%lu,"
                "\"age_ms\":null,\"valid\":false,\"temperature_x10\":null,\"humidity_x10\":null,\"pressure_pa\":null}",
                (unsigned long)g_now, (unsigned long)SlaveBme280Diag.sample_success_count);
        }
        if (length < 0 || (unsigned)length >= sizeof(g_json)) { Fail(SLAVE_ESP_ERROR_IPD); return; }
        {
            SlaveMq2Sample mq2;
            uint8_t mq_valid = SlaveMq2_GetSample(g_now, &mq2);
            unsigned offset = (unsigned)length - 1U;
            int added;
            if (mq_valid != 0U)
            {
                added = snprintf(g_json + offset, sizeof(g_json) - offset,
                    ",\"mq2\":{\"valid\":true,\"sample_seq\":%lu,\"age_ms\":%lu,"
                    "\"raw\":%u,\"pa7_mv\":%lu,\"ao_mv\":%lu}}",
                    (unsigned long)mq2.sequence, (unsigned long)(g_now - mq2.tick),
                    (unsigned)mq2.raw, (unsigned long)mq2.pa7_mv, (unsigned long)mq2.ao_mv);
            }
            else
            {
                added = snprintf(g_json + offset, sizeof(g_json) - offset,
                    ",\"mq2\":{\"valid\":false,\"sample_seq\":%lu,\"age_ms\":null,"
                    "\"raw\":null,\"pa7_mv\":null,\"ao_mv\":null}}",
                    (unsigned long)SlaveMq2Diag.sample_count);
            }
            if (added < 0 || (unsigned)added >= sizeof(g_json) - offset)
            { Fail(SLAVE_ESP_ERROR_IPD); return; }
            length = (int)offset + added;
        }
        {
            unsigned offset = (unsigned)length - 1U;
            SlaveHcsr04Sample ultrasonic;
            uint8_t good = SlaveHcsr04_GetSample(g_now, &ultrasonic);
            if (good != 0U)
            {
                if (JsonAppend(&offset,
                    ",\"ultrasonic\":{\"valid\":true,\"distance_mm\":%u,\"raw_mm\":%u,"
                    "\"pulse_us\":%u,\"filter_count\":%u,\"sample_seq\":%lu,\"age_ms\":%lu,\"error\":0}",
                    (unsigned)ultrasonic.distance_mm, (unsigned)ultrasonic.raw_mm,
                    (unsigned)ultrasonic.pulse_us, (unsigned)ultrasonic.filter_count,
                    (unsigned long)ultrasonic.sequence, (unsigned long)(g_now - ultrasonic.tick)) == 0U)
                { Fail(SLAVE_ESP_ERROR_IPD); return; }
            }
            else if (JsonAppend(&offset,
                ",\"ultrasonic\":{\"valid\":false,\"distance_mm\":null,\"raw_mm\":null,"
                "\"pulse_us\":null,\"filter_count\":0,\"sample_seq\":%lu,\"age_ms\":null,\"error\":%lu}",
                (unsigned long)SlaveHcsr04Diag.sample_count,
                (unsigned long)SlaveHcsr04Diag.last_error) == 0U)
            { Fail(SLAVE_ESP_ERROR_IPD); return; }
            if (JsonMonitor(&offset) == 0U) { Fail(SLAVE_ESP_ERROR_IPD); return; }
            length = (int)offset;
        }
        g_body = g_json; g_body_length = (uint16_t)length;
    }
    else
    {
        g_body = "Not found\n";
        status = "404 Not Found";
        if (kind == REQUEST_ICON) { status = "204 No Content"; g_body = ""; }
        if (kind == REQUEST_METHOD) { status = "405 Method Not Allowed"; g_body = "Read only\n"; }
        if (kind == REQUEST_BAD) { status = "400 Bad Request"; g_body = "Bad request\n"; }
        g_body_length = (uint16_t)strlen(g_body);
    }
    length = snprintf(g_header, sizeof(g_header),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n%s\r\n", status, type, (unsigned)g_body_length,kind==REQUEST_PAGE?"Content-Encoding: gzip\r\n":"");
    if (length < 0 || (unsigned)length >= sizeof(g_header)) { Fail(SLAVE_ESP_ERROR_IPD); return; }
    g_header_length = (uint16_t)length;
    g_response_started = g_now;
    State(SLAVE_ESP_SEND_START, 0U);
}
static void FillChunk(void)
{
    uint16_t i, total = (uint16_t)(g_header_length + g_body_length);
    g_chunk = (uint16_t)(total - g_offset);
    if (g_chunk > CHUNK_SIZE) { g_chunk = CHUNK_SIZE; }
    for (i = 0U; i < g_chunk; i++)
    {
        uint16_t offset = (uint16_t)(g_offset + i);
        g_tx[i] = (uint8_t)((offset < g_header_length) ? g_header[offset] : g_body[offset - g_header_length]);
    }
}
void SlaveEspWeb_Init(uint32_t now_ms)
{
    memset((void *)&SlaveEspWebDiag, 0, sizeof(SlaveEspWebDiag));
    EspAtUart_Init();
    g_now = now_ms;
    g_step = g_variant = g_next = 0U;
    g_seen_error = EspAtUartDiag.error_count;
    g_seen_overflow = EspAtUartDiag.overflow_count;
    ClearClients(); ClearParser();
    State(SLAVE_ESP_BOOT, 1200U);
}
void SlaveEspWeb_Process(uint32_t now_ms)
{
    uint8_t byte, id;
    uint16_t budget = 256U;
    uint32_t state;
    g_now = now_ms;
    EspAtUart_Service();
    if (g_seen_error != EspAtUartDiag.error_count || g_seen_overflow != EspAtUartDiag.overflow_count)
    {
        uint32_t error = (g_seen_overflow != EspAtUartDiag.overflow_count) ? SLAVE_ESP_ERROR_OVERFLOW : SLAVE_ESP_ERROR_UART;
        g_seen_error = EspAtUartDiag.error_count;
        g_seen_overflow = EspAtUartDiag.overflow_count;
        Fail(error);
        return;
    }
    while (budget-- != 0U && EspAtUart_Read(&byte) != 0U) { ParseByte(byte); }
    state = SlaveEspWebDiag.state;
    if ((g_events & EVENT_READY) != 0U)
    {
        EspAtUart_AbortTx(); ClearClients(); ClearParser();
        SlaveEspWebDiag.ready = 0U;
        g_step = g_variant = 0U;
        State(SLAVE_ESP_BOOT, 1200U);
        return;
    }
    if (g_ipd_left != 0U && (uint32_t)(g_now - g_ipd_started) >= AT_TIMEOUT)
    { Fail(SLAVE_ESP_ERROR_IPD); return; }
    /* Browsers may close as soon as Content-Length bytes arrive, before the
       module emits SEND OK. Finish the last TX before accepting that close. */
    if (g_lost == 1U && state == SLAVE_ESP_SEND_WAIT &&
        (g_events & EVENT_SENT) != 0U && g_offset + g_chunk >= g_header_length + g_body_length)
    {
        SlaveEspWebDiag.response_count++;
        g_active = CONNECTIONS; g_lost = 0U;
        State(SLAVE_ESP_READY, 0U);
        return;
    }
    if (g_lost != 0U && state == SLAVE_ESP_CLOSE_START)
    {
        g_active = CONNECTIONS; g_lost = 0U;
        State(SLAVE_ESP_READY, 0U);
        return;
    }
    if (state >= SLAVE_ESP_SEND_START && state <= SLAVE_ESP_SEND_WAIT &&
        ((g_lost != 0U && !(g_lost == 1U && state == SLAVE_ESP_RAW_TX)) ||
         (uint32_t)(g_now - g_response_started) >= 20000U))
    { Fail(g_lost != 0U ? SLAVE_ESP_ERROR_AT : SLAVE_ESP_ERROR_TIMEOUT); return; }
    switch (state)
    {
    case SLAVE_ESP_BOOT:
    case SLAVE_ESP_BACKOFF:
        if (Due(g_now, g_deadline))
        {
            g_step = g_variant = 0U;
            ClearParser();
            SlaveEspWebDiag.init_count++;
            State(SLAVE_ESP_INIT_START, 0U);
        }
        break;
    case SLAVE_ESP_INIT_START:
        if (g_step >= sizeof(g_init) / sizeof(g_init[0]))
        {
            SlaveEspWebDiag.ready = 1U;
            SlaveEspWebDiag.last_error = SLAVE_ESP_ERROR_NONE;
            State(SLAVE_ESP_READY, 0U);
        }
        else
        {
            const char *command = (g_variant == 0U) ? g_init[g_step].command :
                ((g_variant == 1U) ? g_init[g_step].fallback : g_init[g_step].old);
            SlaveEspWebDiag.init_step = g_step;
            (void)Start((const uint8_t *)command, (uint16_t)strlen(command), SLAVE_ESP_INIT_TX);
        }
        break;
    case SLAVE_ESP_INIT_TX:
        if (TxDone()) { State(SLAVE_ESP_INIT_WAIT, AT_TIMEOUT); }
        break;
    case SLAVE_ESP_INIT_WAIT:
        if ((g_events & EVENT_ERROR) != 0U)
        {
            if (g_variant == 0U && g_init[g_step].fallback != 0) { g_variant = 1U; }
            else if (g_variant == 1U && g_init[g_step].old != 0) { g_variant = 2U; }
            else if (g_init[g_step].optional != 0U) { g_step++; g_variant = 0U; }
            else { Backoff(SLAVE_ESP_ERROR_AT); break; }
            State(SLAVE_ESP_INIT_START, 0U);
        }
        else if ((g_events & EVENT_OK) != 0U)
        {
            if (g_step == 0U) { SlaveEspWebDiag.at_detected = 1U; }
            g_step++; g_variant = 0U;
            State(SLAVE_ESP_INIT_START, 0U);
        }
        else if (Due(g_now, g_deadline)) { Backoff(SLAVE_ESP_ERROR_TIMEOUT); }
        break;
    case SLAVE_ESP_READY:
        for (id = 0U; id < CONNECTIONS; id++)
        {
            uint8_t candidate = (uint8_t)((g_next + id) % CONNECTIONS);
            Connection *c = &g_connections[candidate];
            if (c->live == 0U) { continue; }
            if (c->pending == 0U && (uint32_t)(g_now - c->started) >= AT_TIMEOUT)
            { c->pending = 1U; c->kind = REQUEST_CLOSE; }
            if (c->pending != 0U && g_ipd_left == 0U)
            {
                g_active = candidate; g_lost = 0U;
                g_next = (uint8_t)((candidate + 1U) % CONNECTIONS);
                if (c->kind == REQUEST_CLOSE) { State(SLAVE_ESP_CLOSE_START, 0U); }
                else { BuildResponse(c->kind); }
                break;
            }
        }
        break;
    case SLAVE_ESP_SEND_START:
        FillChunk();
        {
            int len = snprintf((char *)g_tx, sizeof(g_tx), "AT+CIPSEND=%u,%u\r\n", (unsigned)g_active, (unsigned)g_chunk);
            (void)Start(g_tx, (uint16_t)len, SLAVE_ESP_SEND_TX);
        }
        break;
    case SLAVE_ESP_SEND_TX:
        if (TxDone()) { State(SLAVE_ESP_PROMPT, AT_TIMEOUT); }
        break;
    case SLAVE_ESP_PROMPT:
        if ((g_events & EVENT_ERROR) != 0U) { Fail(SLAVE_ESP_ERROR_AT); }
        else if ((g_events & EVENT_PROMPT) != 0U) { State(SLAVE_ESP_RAW_START, 0U); }
        else if (Due(g_now, g_deadline)) { Fail(SLAVE_ESP_ERROR_TIMEOUT); }
        break;
    case SLAVE_ESP_RAW_START:
        FillChunk();
        (void)Start(g_tx, g_chunk, SLAVE_ESP_RAW_TX);
        break;
    case SLAVE_ESP_RAW_TX:
        if (TxDone()) { State(SLAVE_ESP_SEND_WAIT, AT_TIMEOUT); }
        break;
    case SLAVE_ESP_SEND_WAIT:
        if ((g_events & EVENT_ERROR) != 0U) { Fail(SLAVE_ESP_ERROR_AT); }
        else if ((g_events & EVENT_SENT) != 0U)
        {
            g_offset = (uint16_t)(g_offset + g_chunk);
            if (g_offset >= g_header_length + g_body_length)
            { SlaveEspWebDiag.response_count++; State(SLAVE_ESP_CLOSE_START, 0U); }
            else { State(SLAVE_ESP_SEND_START, 0U); }
        }
        else if (Due(g_now, g_deadline)) { Fail(SLAVE_ESP_ERROR_TIMEOUT); }
        break;
    case SLAVE_ESP_CLOSE_START:
        {
            int len = snprintf((char *)g_tx, sizeof(g_tx), "AT+CIPCLOSE=%u\r\n", (unsigned)g_active);
            (void)Start(g_tx, (uint16_t)len, SLAVE_ESP_CLOSE_TX);
        }
        break;
    case SLAVE_ESP_CLOSE_TX:
        if (TxDone()) { State(SLAVE_ESP_CLOSE_WAIT, AT_TIMEOUT); }
        break;
    case SLAVE_ESP_CLOSE_WAIT:
        if ((g_events & (EVENT_OK | EVENT_ERROR)) != 0U)
        {
            /* A CONNECT following CLOSED while close was in flight survives. */
            if (g_lost == 0U)
            {
                if (g_connections[g_active].live != 0U) { SlaveEspWebDiag.active_connections--; }
                memset(&g_connections[g_active], 0, sizeof(Connection));
            }
            g_active = CONNECTIONS; g_lost = 0U;
            State(SLAVE_ESP_READY, 0U);
        }
        else if (Due(g_now, g_deadline)) { Fail(SLAVE_ESP_ERROR_TIMEOUT); }
        break;
    case SLAVE_ESP_RECOVER_START:
        if (Due(g_now, g_deadline))
        {
            memset(g_tx, ' ', sizeof(g_tx));
            (void)Start(g_tx, sizeof(g_tx), SLAVE_ESP_RECOVER_TX);
        }
        break;
    case SLAVE_ESP_RECOVER_TX:
        if (TxDone()) { State(SLAVE_ESP_RECOVER_QUIET, 1000U); }
        break;
    case SLAVE_ESP_RECOVER_QUIET:
        if (Due(g_now, g_deadline)) { State(SLAVE_ESP_RESET_START, 0U); }
        break;
    case SLAVE_ESP_RESET_START:
        ClearParser();
        (void)Start((const uint8_t *)"\r\nAT+RST\r\n", 10U, SLAVE_ESP_RESET_TX);
        break;
    case SLAVE_ESP_RESET_TX:
        if (TxDone()) { State(SLAVE_ESP_RESET_WAIT, AT_TIMEOUT); }
        break;
    case SLAVE_ESP_RESET_WAIT:
        if (Due(g_now, g_deadline)) { Backoff(SLAVE_ESP_ERROR_TIMEOUT); }
        break;
    default: Fail(SLAVE_ESP_ERROR_AT); break;
    }
}
