#define W4HOME_MODULE "vfd-desktop-clock"
#define W4HOME_ID_PREFIX "w4vfd-"
#include "w4home_client.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#ifndef W4HOME_SERVER_URL
#define W4HOME_SERVER_URL "https://w4.h.p5.pw"
#endif
#ifndef W4HOME_FIRMWARE
#define W4HOME_FIRMWARE "dev"
#endif
static portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
static bool pause_requested, active;
void w4home_pause(bool paused) { portENTER_CRITICAL(&guard);pause_requested=paused;portEXIT_CRITICAL(&guard); }
bool w4home_idle(void) { portENTER_CRITICAL(&guard);bool idle=!active;portEXIT_CRITICAL(&guard);return idle; }
static bool hex64(const char *s) {
    if (!s || strlen(s)!=64) return false;
    for (unsigned i=0;i<64;++i) if (!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return false;
    return true;
}
typedef struct { char data[512];size_t used;bool rejected;int64_t started; } response_t;
static esp_err_t response_event(esp_http_client_event_t *event) {
    response_t *r=(response_t *)event->user_data;
    if (esp_timer_get_time()-r->started>10000000) {r->rejected=true;return ESP_FAIL;}
    if(event->event_id==HTTP_EVENT_ON_DATA) {
        if(event->data_len<0||(size_t)event->data_len>=sizeof(r->data)-r->used){r->rejected=true;return ESP_FAIL;}
        memcpy(r->data+r->used,event->data,event->data_len);r->used+=event->data_len;r->data[r->used]=0;
    }
    return ESP_OK;
}
static int post(const char *url,const char *token,const char *body,response_t *response) {
    memset(response,0,sizeof(*response));response->started=esp_timer_get_time();
    esp_http_client_config_t config={0};config.url=url;config.method=HTTP_METHOD_POST;
    config.timeout_ms=2500;config.disable_auto_redirect=true;config.crt_bundle_attach=esp_crt_bundle_attach;
    config.event_handler=response_event;config.user_data=response;
    esp_http_client_handle_t client=esp_http_client_init(&config);if(!client)return -1;
    esp_http_client_set_header(client,"Content-Type","application/json");
    char auth[80];if(token[0]){snprintf(auth,sizeof(auth),"Bearer %s",token);esp_http_client_set_header(client,"Authorization",auth);}
    esp_http_client_set_post_field(client,body,strlen(body));
    esp_err_t err=esp_http_client_perform(client);
    int code=(err==ESP_OK&&!response->rejected)?esp_http_client_get_status_code(client):-1;
    esp_http_client_cleanup(client);return code;
}
static void worker(void *unused) {
    (void)unused;
    nvs_handle_t storage;char claim[65]={0},token[65]={0},id[40],url[256],body[1024];
    uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);
    snprintf(id,sizeof(id),W4HOME_ID_PREFIX "%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    if(nvs_open("w4home",NVS_READWRITE,&storage)!=ESP_OK){ESP_LOGE("w4home","Identity storage unavailable");vTaskDelete(NULL);return;}
    size_t length=sizeof(claim);esp_err_t err=nvs_get_str(storage,"claim",claim,&length);
    if(err==ESP_ERR_NVS_NOT_FOUND){uint8_t random[32];esp_fill_random(random,sizeof(random));for(unsigned i=0;i<32;++i)snprintf(claim+2*i,3,"%02x",random[i]);
        if(nvs_set_str(storage,"claim",claim)!=ESP_OK||nvs_commit(storage)!=ESP_OK)claim[0]=0;
    }
    if(!hex64(claim)){ESP_LOGE("w4home","Invalid identity; storage preserved");nvs_close(storage);vTaskDelete(NULL);return;}
    length=sizeof(token);err=nvs_get_str(storage,"token",token,&length);
    if(err!=ESP_OK&&err!=ESP_ERR_NVS_NOT_FOUND){nvs_close(storage);vTaskDelete(NULL);return;}
    if(token[0]&&!hex64(token)){nvs_close(storage);vTaskDelete(NULL);return;}
    uint32_t wait_seconds=0,backoff=30;
    for(;;){
        vTaskDelay(pdMS_TO_TICKS(1000));
        if(wait_seconds){--wait_seconds;continue;}
        wifi_ap_record_t ap;
        if(esp_wifi_sta_get_ap_info(&ap)!=ESP_OK)continue;
        if(strncmp(W4HOME_SERVER_URL,"https://",8)==0&&time(NULL)<1704067200)continue;
        portENTER_CRITICAL(&guard);bool run=!pause_requested;if(run)active=true;portEXIT_CRITICAL(&guard);
        if(!run)continue;
        response_t response;int code=-1;
        if(!token[0]){
            snprintf(url,sizeof(url),W4HOME_SERVER_URL "/api/" W4HOME_MODULE "/enroll");
            snprintf(body,sizeof(body),"{\"id\":\"%s\",\"claim\":\"%s\"}",id,claim);
            code=post(url,"",body,&response);
            if(code==200){
                const char *end=NULL;cJSON *json=cJSON_ParseWithOpts(response.data,&end,true);
                const cJSON *t=cJSON_GetObjectItemCaseSensitive(json,"token"),*i=cJSON_GetObjectItemCaseSensitive(json,"id");
                if(cJSON_IsString(t)&&hex64(t->valuestring)&&cJSON_IsString(i)&&strcmp(i->valuestring,id)==0&&
                   nvs_set_str(storage,"token",t->valuestring)==ESP_OK&&nvs_commit(storage)==ESP_OK)snprintf(token,sizeof(token),"%s",t->valuestring);
                else code=-1;
                cJSON_Delete(json);
            }
        }
        if(token[0]){
            char status[640]={0};w4home_status(status,sizeof(status));
            snprintf(body,sizeof(body),"{\"firmware\":\"" W4HOME_FIRMWARE "\",\"status\":%s}",status);
            snprintf(url,sizeof(url),W4HOME_SERVER_URL "/api/" W4HOME_MODULE "/devices/%s/sync",id);
            code=post(url,token,body,&response);
        }
        portENTER_CRITICAL(&guard);active=false;portEXIT_CRITICAL(&guard);
        if(code==204){wait_seconds=300;backoff=30;}else{wait_seconds=backoff;if(backoff<1800)backoff*=2;if(backoff>1800)backoff=1800;}
        ESP_LOGI("w4home","%s HTTP %d; retry in %us",id,code,(unsigned)wait_seconds);
    }
}
void w4home_start(void) {
    if(xTaskCreate(worker,"w4home",8192,NULL,1,NULL)!=pdPASS)ESP_LOGE("w4home","Worker unavailable");
}
