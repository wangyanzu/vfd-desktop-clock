#define W4HOME_MODULE "vfd-desktop-clock"
#define W4HOME_MODEL "vfd-desktop-clock-esp32c3"
#include "w4home_client.h"
#include "clock/config.h"
#include "esp_flash.h"
#include "w4home/idf_status_client.h"
#ifndef W4HOME_FIRMWARE
#define W4HOME_FIRMWARE "dev"
#endif
static const char model_binding[]="W4HOME-MODEL:v1:vfd-desktop-clock/vfd-desktop-clock-esp32c3:END";
static bool enrollment(char *body, size_t capacity, const char *id, const char *claim) {
    using namespace clockapp;
    uint32_t flash_bytes = 0;
    if (esp_flash_get_size(NULL, &flash_bytes) != ESP_OK) return false;
    cJSON *root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "id", id);
    cJSON_AddStringToObject(root, "typeId", W4HOME_MODULE);
    cJSON_AddStringToObject(root, "model", W4HOME_MODEL);
    cJSON_AddStringToObject(root, "hardwareVersion", "unspecified");
    cJSON_AddStringToObject(root, "protocolVersion", "1");
    cJSON_AddStringToObject(root, "transport", "http");
    cJSON_AddStringToObject(root, "claim", claim);
    cJSON *report = cJSON_AddObjectToObject(root, "hardwareReport");
    char hostname[32];
    snprintf(hostname, sizeof(hostname), "w4vfd-%s", id);
    cJSON_AddStringToObject(report, "hostname", hostname);
    cJSON_AddStringToObject(report, "firmwareVersion", W4HOME_FIRMWARE);
    cJSON_AddNumberToObject(report, "firmwareBuild", 0);
    cJSON_AddStringToObject(report, "resourceVersion", "1");
    cJSON_AddArrayToObject(report, "capabilities");
    cJSON *hardware = cJSON_AddObjectToObject(report, "hardware");
    cJSON_AddStringToObject(hardware, "typeId", W4HOME_MODULE);
    cJSON_AddStringToObject(hardware, "model", W4HOME_MODEL);
    cJSON_AddStringToObject(hardware, "boardRevision", "unspecified");
    cJSON_AddNumberToObject(hardware, "flashBytes", flash_bytes);
    cJSON_AddStringToObject(hardware, "partitionLayout", "min-spiffs");
    cJSON_AddStringToObject(hardware, "pinMappingVersion", "vfd-pins-v1");
    cJSON_AddStringToObject(hardware, "imageFormat", "esp-app");
    cJSON *parts = cJSON_AddArrayToObject(hardware, "components");
    const auto component = [parts](const char *role, const char *model) {
        cJSON *part = cJSON_CreateObject();
        cJSON_AddStringToObject(part, "role", role);
        cJSON_AddStringToObject(part, "model", model);
        cJSON_AddItemToArray(parts, part);
    };
    component("mcu", "esp32-c3");
    component("display", "m202md28aa");
    if (config::hasAht) component("sensor", "aht10");
    if (config::hasButton) component("button", "momentary");
    cJSON *pins = cJSON_AddObjectToObject(hardware, "pins");
    cJSON_AddNumberToObject(pins, "vfd_tx", config::vfdTx);
    if (config::hasAht) {
        cJSON_AddNumberToObject(pins, "i2c_sda", config::sda);
        cJSON_AddNumberToObject(pins, "i2c_scl", config::scl);
    }
    if (config::hasButton) cJSON_AddNumberToObject(pins, "button", config::button);
    bool ok = cJSON_PrintPreallocated(root, body, capacity, false);
    cJSON_Delete(root);
    return ok;
}

static cJSON *build(const w4identity_record_t *r){char body[4096];return enrollment(body,sizeof(body),r->id,r->claim)?cJSON_Parse(body):NULL;}
void w4home_start(void){asm volatile("" : : "r"(model_binding) : "memory");w4status_start(W4HOME_MODEL,W4HOME_FIRMWARE,build,w4home_status);}
void w4home_pause(bool paused){w4status_pause(paused);}
bool w4home_idle(void){return w4status_idle();}
bool w4home_service_request(const char *server,bool cancel){return w4status_service(server,cancel);}
void w4home_resume(void){w4status_resume();}
void w4home_local_state(char *server,size_t capacity,bool *ready,bool *recovery){auto v=w4status_snapshot();snprintf(server,capacity,"%s",v.server[0]?v.server:W4HOME_DEFAULT_ORIGIN);*ready=v.phase==5;*recovery=v.blocked||v.phase==7;}
void w4home_diagnostics(char *out,size_t capacity){auto v=w4status_snapshot();snprintf(out,capacity,"{\"phase\":%u,\"http\":%d,\"lastSuccessUptime\":%lu}",v.phase,v.http,(unsigned long)v.last_success);}
