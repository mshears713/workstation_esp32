#include "lincoln_media.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char *source;
static bool running=true,fetch_fail,corrupt;
static unsigned fetches;
static char message[96];
static bool wanted(void){return running;}
static void status(const char *s){snprintf(message,sizeof(message),"%s",s);}
bool media_fetch(const char *name,FILE *out,size_t size,media_wanted_t keep)
{
    fetches++;if(fetch_fail)return false;
    char path[512];snprintf(path,sizeof(path),"%s/%s",source,name);FILE *f=fopen(path,"rb");assert(f);
    unsigned char b[1024];size_t total=0,n;
    while((n=fread(b,1,sizeof(b),f))&&keep()){
        if(corrupt&&total==0)b[0]^=1;
        assert(fwrite(b,1,n,out)==n);total+=n;
    }
    fclose(f);return total==size;
}
int main(int argc,char **argv)
{
    assert(argc==2);source=argv[1];
    running=false;assert(!lincoln_media_ensure(wanted,status)&&fetches==0);running=true;
    fetch_fail=true;assert(!lincoln_media_ensure(wanted,status));fetch_fail=false;
    assert(access(LINCOLN_MEDIA_ROOT "/lincoln_1.pcm.part",F_OK)!=0);
    corrupt=true;assert(!lincoln_media_ensure(wanted,status));corrupt=false;
    assert(access(LINCOLN_MEDIA_ROOT "/lincoln_1.pcm",F_OK)!=0);
    assert(lincoln_media_ensure(wanted,status));unsigned initial=fetches;
    assert(lincoln_media_ensure(wanted,status)&&fetches==initial); /* hash-check existing; never redownload */
    FILE *f=fopen(LINCOLN_MEDIA_ROOT "/lincoln_2.pcm","r+b");assert(f);int first=fgetc(f);assert(first!=EOF);rewind(f);fputc(first^1,f);fclose(f);
    assert(!lincoln_media_ensure(wanted,status)&&fetches==initial&&strstr(message,"existing kept"));
    f=fopen(LINCOLN_MEDIA_ROOT "/lincoln_2.pcm","rb");assert(fgetc(f)==(first^1));fclose(f);
    puts("PASS media provisioning: cancellation, fetch failure, hash mismatch cleanup, all seven selected assets, idempotent cache, conflicting existing file preserved");
}
