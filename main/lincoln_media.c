/* Additive, hash-pinned media provisioning on the existing exclusive SD owner.
 * No format, replacement of an existing file, directory traversal, or SD scan. */
#include "lincoln_media.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef HOST_LINCOLN_MEDIA
#include <openssl/sha.h>
#define sha_ctx SHA256_CTX
#define sha_init(c) SHA256_Init(c)
#define sha_add(c,b,n) SHA256_Update(c,b,n)
#define sha_end(c,b) SHA256_Final(b,c)
extern bool media_fetch(const char *name,FILE *file,size_t size,media_wanted_t wanted);
#else
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "device_config.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define sha_ctx mbedtls_sha256_context
#define sha_init(c) do{mbedtls_sha256_init(c);mbedtls_sha256_starts(c,0);}while(0)
#define sha_add(c,b,n) mbedtls_sha256_update(c,b,n)
#define sha_end(c,b) do{mbedtls_sha256_finish(c,b);mbedtls_sha256_free(c);}while(0)
extern const uint8_t media_ca[] asm("_binary_clawbox_ota_ca_pem_start");
static bool media_fetch(const char *name,FILE *file,size_t expected,media_wanted_t wanted)
{
    char url[256];int n=snprintf(url,sizeof(url),"%s/firmware/lincoln-scottw-two-20261030/%s",device_config_ota_base_url(),name);
    if(n<0||(size_t)n>=sizeof(url))return false;
    esp_http_client_config_t cfg={.url=url,.cert_pem=(const char *)media_ca,.timeout_ms=5000,.buffer_size=2048,.keep_alive_enable=false};
    esp_http_client_handle_t client=esp_http_client_init(&cfg);if(!client)return false;
    bool ok=false;size_t total=0;unsigned chunks=0;
    char *buf=malloc(2048);if(!buf)goto done;
    if(!wanted()||esp_http_client_open(client,0)!=ESP_OK)goto done;
    int64_t length=esp_http_client_fetch_headers(client);
    if(esp_http_client_get_status_code(client)!=200||length!=(int64_t)expected)goto done;
    int64_t deadline=esp_timer_get_time()+60000000;
    while(total<expected&&wanted()&&esp_timer_get_time()<deadline){
        size_t need=expected-total;if(need>2048)need=2048;
        int got=esp_http_client_read(client,buf,(int)need);if(got<=0)break;
        if(fwrite(buf,1,(size_t)got,file)!=(size_t)got)break;
        total+=(size_t)got;if(++chunks%16==0)vTaskDelay(1);
    }
    ok=total==expected&&wanted();
done:
    free(buf);esp_http_client_close(client);esp_http_client_cleanup(client);
    ESP_LOGI("lincoln_media","asset %s: %u/%u bytes %s",name,(unsigned)total,(unsigned)expected,ok?"received":"failed/cancelled");
    return ok;
}
#endif
typedef struct {const char *name;size_t bytes;const char *sha256;} media_asset_t;
#include "lincoln_media_assets.inc"
static bool mkdir_safe(const char *path)
{
    if(mkdir(path,0775)==0)return true;
    struct stat st;return errno==EEXIST&&stat(path,&st)==0&&S_ISDIR(st.st_mode);
}
static bool verify(const char *path,const media_asset_t *asset,media_wanted_t wanted)
{
    struct stat st;if(stat(path,&st)!=0||!S_ISREG(st.st_mode)||(size_t)st.st_size!=asset->bytes)return false;
    FILE *f=fopen(path,"rb");if(!f)return false;
    unsigned char buffer[1024],digest[32];sha_ctx hash;sha_init(&hash);size_t total=0;
    while(wanted()){
        size_t n=fread(buffer,1,sizeof(buffer),f);if(!n)break;sha_add(&hash,buffer,n);total+=n;
#ifndef HOST_LINCOLN_MEDIA
        if(total%32768==0)vTaskDelay(1); /* Cached-media verification must also yield to idle/network tasks. */
#endif
    }
    bool read_ok=!ferror(f)&&wanted();fclose(f);sha_end(&hash,digest);
    char hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);
    return read_ok&&total==asset->bytes&&!strcmp(hex,asset->sha256);
}
bool lincoln_media_ensure(media_wanted_t wanted,media_status_t status)
{
    if(!wanted||!status||!wanted())return false;
#ifndef HOST_LINCOLN_MEDIA
    if(!media_voice_approved){status("Lincoln voice awaiting approval");return false;}
#else
    (void)media_voice_approved; /* Rejected takes may test file integrity, never playback approval. */
#endif
    const char *directories[]={BSP_SD_MOUNT_POINT "/dottie",BSP_SD_MOUNT_POINT "/dottie/audio",BSP_SD_MOUNT_POINT "/dottie/audio/lincoln",LINCOLN_MEDIA_ROOT};
    for(unsigned i=0;i<4;i++)if(!mkdir_safe(directories[i])){status("Cannot create Lincoln audio folder");return false;}
    for(unsigned i=0;i<sizeof(assets)/sizeof(assets[0]);i++){
        if(!wanted())return false;
        char path[224],partial[232],message[64];struct stat st;
        snprintf(path,sizeof(path),"%s/%s",LINCOLN_MEDIA_ROOT,assets[i].name);
        snprintf(partial,sizeof(partial),"%s.part",path);
        snprintf(message,sizeof(message),"Checking Lincoln audio %u / %u",i+1,(unsigned)(sizeof(assets)/sizeof(assets[0])));status(message);
        if(stat(path,&st)==0){
            if(verify(path,&assets[i],wanted))continue;
            status("Lincoln file conflict - existing kept");return false;
        }
        if(errno!=ENOENT){status("Lincoln SD access error");return false;}
        FILE *f=fopen(partial,"wbx"); /* An unrelated/existing partial is never overwritten. */
        if(!f){status("Lincoln partial exists / SD unwritable");return false;}
        snprintf(message,sizeof(message),"Downloading Lincoln audio %u / %u",i+1,(unsigned)(sizeof(assets)/sizeof(assets[0])));status(message);
        bool ok=media_fetch(assets[i].name,f,assets[i].bytes,wanted);
        if(fflush(f)!=0)ok=false;
        if(fclose(f)!=0)ok=false;
        if(!ok||!verify(partial,&assets[i],wanted)){
            unlink(partial);status(wanted()?"Lincoln download failed - retry":"Lincoln download cancelled");return false;
        }
        /* FAT rename refuses an existing target; an explicit check also protects host tests. */
        if(stat(path,&st)==0||rename(partial,path)!=0){unlink(partial);status("Lincoln publish conflict - existing kept");return false;}
    }
    status("Lincoln audio verified");return true;
}
