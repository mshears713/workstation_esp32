#include "newton_store.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
/* New dedicated namespace only. Never erase/init NVS or touch device settings. */
bool newton_store_load(newton_saved_t *out)
{
    nvs_handle_t handle;
    esp_err_t err=nvs_open("newton_scene",NVS_READONLY,&handle);
    if(err!=ESP_OK)return false;
    newton_saved_t data={0};size_t size=sizeof(data);
    err=nvs_get_blob(handle,"fruit_v1",&data,&size);nvs_close(handle);
    if(err!=ESP_OK || size!=sizeof(data) || data.version!=1)return false;
    for(unsigned i=0;i<NEWTON_FRUITS;i++)if(data.regrow_left_ms[i]>NEWTON_REGROW_MS)return false;
    *out=data;return true;
}
bool newton_store_save(const newton_saved_t *state)
{
    if(!state || state->version!=1)return false;
    for(unsigned i=0;i<NEWTON_FRUITS;i++)if(state->regrow_left_ms[i]>NEWTON_REGROW_MS)return false;
    newton_saved_t old;
    if(newton_store_load(&old) && !memcmp(&old,state,sizeof(old)))return true;
    nvs_handle_t handle;esp_err_t err=nvs_open("newton_scene",NVS_READWRITE,&handle);
    if(err==ESP_OK) {
        err=nvs_set_blob(handle,"fruit_v1",state,sizeof(*state));
        if(err==ESP_OK)err=nvs_commit(handle);
        nvs_close(handle);
    }
    if(err!=ESP_OK)ESP_LOGW("newton_scene","Fruit save failed: %s",esp_err_to_name(err));
    return err==ESP_OK;
}
