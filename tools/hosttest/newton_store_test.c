#include "newton_store.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static newton_saved_t blob;
static int writes,commits,opens,closes;
static bool exists,fail_open,fail_commit;
int nvs_open(const char *name,int mode,nvs_handle_t *out)
{
    assert(!strcmp(name,"newton_scene"));assert(mode==NVS_READONLY || mode==NVS_READWRITE);
    if(fail_open)return -1;
    *out=1;opens++;return 0;
}
int nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size)
{
    assert(h==1 && !strcmp(key,"fruit_v1") && *size==sizeof(blob));
    if(!exists)return -1;
    memcpy(out,&blob,sizeof(blob));return 0;
}
int nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size)
{
    assert(h==1 && !strcmp(key,"fruit_v1") && size==sizeof(blob));
    memcpy(&blob,data,size);writes++;return 0;
}
int nvs_commit(nvs_handle_t h) { assert(h==1);commits++;if(fail_commit)return -1;exists=true;return 0; }
void nvs_close(nvs_handle_t h) { assert(h==1);closes++; }
void test_log(const char *tag,const char *fmt,...) { (void)tag;(void)fmt; }
const char *esp_err_to_name(int error) { (void)error;return "test failure"; }
int main(void)
{
    newton_saved_t s={.version=1,.regrow_left_ms={NEWTON_REGROW_MS}},loaded;
    assert(!newton_store_load(&loaded));assert(newton_store_save(&s));
    assert(newton_store_load(&loaded) && !memcmp(&s,&loaded,sizeof(s)));
    for(int i=0;i<500;i++)assert(newton_store_save(&s));
    assert(writes==1 && commits==1); /* no idle/reopen flash wear */
    s.regrow_left_ms[0]=NEWTON_GROW_ANIM_MS;assert(newton_store_save(&s));
    assert(writes==2);fail_commit=true;s.regrow_left_ms[0]=0;
    assert(!newton_store_save(&s));fail_open=true;assert(!newton_store_load(&loaded));
    assert(!newton_store_save(&s));fail_open=fail_commit=false;
    s.version=2;assert(!newton_store_save(&s));
    blob.version=2;assert(!newton_store_load(&loaded));
    blob.version=1;blob.regrow_left_ms[0]=UINT32_MAX;assert(!newton_store_load(&loaded));
    assert(opens==closes);
    puts("PASS Newton persistence: dedicated namespace/key, exact round-trip, unchanged save suppression, corrupt version/range, open/commit failure, all handles closed");
}
