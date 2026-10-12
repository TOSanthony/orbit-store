/* Paired desktop registration. This stores no download URL and makes no network request. */
#include "zero_companion.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>

static void path(char *out, size_t cap, bool temporary) {
    snprintf(out, cap, "%s/zero-companion%s.json", orbit.state_dir, temporary ? ".tmp" : "");
}
static cJSON *load(void) {
    char file[640], data[2048]; path(file,sizeof file,false);
    int fd=open(file,O_RDONLY|O_NOFOLLOW); struct stat s;
    if(fd<0)return cJSON_CreateObject();
    ssize_t n = fstat(fd,&s) || !S_ISREG(s.st_mode) || s.st_size<0 || (uint64_t)s.st_size>=sizeof data ? -1 : read(fd,data,sizeof data-1);
    close(fd);
    if(n<=0)return cJSON_CreateObject();
    data[n]=0; cJSON *value=cJSON_Parse(data);
    if(!cJSON_IsObject(value)){cJSON_Delete(value);return cJSON_CreateObject();}
    return value;
}
static const char *text(const cJSON *v,const char *key) {
    const cJSON *item=cJSON_GetObjectItemCaseSensitive(v,key);
    return cJSON_IsString(item)?item->valuestring:"";
}
static bool private_address(const char *ip) {
    struct in_addr a;if(inet_pton(AF_INET,ip,&a)!=1)return false;
    unsigned n=ntohl(a.s_addr);
    return n>>24==10 || n>>20==0xac1 || n>>16==0xc0a8
#ifdef ORBIT_TEST
        || n>>24==127
#endif
        ;
}
cJSON *zero_companion_status(bool secret) {
    cJSON *value=load();
    cJSON_AddBoolToObject(value,"configured",*text(value,"address") && strlen(text(value,"token"))==64);
    if(!secret)cJSON_DeleteItemFromObjectCaseSensitive(value,"token");
    return value;
}
int zero_companion_update(const cJSON *input,const char *peer,bool local,char *error,size_t cap) {
    if(!cJSON_IsObject(input)){copy_text(error,cap,"Invalid companion settings.");return 400;}
    cJSON *value=load();const char *action=text(input,"action");
    if(!strcmp(action,"register")) {
        const char *token=text(input,"token"); const cJSON *port=cJSON_GetObjectItemCaseSensitive(input,"port");
        if(!private_address(peer)||strlen(token)!=64||strspn(token,"0123456789abcdef")!=64||
           !cJSON_IsNumber(port)||port->valuedouble!=port->valueint||port->valueint<1024||port->valueint>65535) {
            cJSON_Delete(value);copy_text(error,cap,"Invalid local companion address.");return 400;
        }
        bool enabled=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(value,"enabled"));
        /* A different computer requires a fresh console opt-in. */
        if(strcmp(text(value,"address"),peer))enabled=false;
        cJSON_Delete(value);value=cJSON_CreateObject();
        cJSON_AddStringToObject(value,"address",peer);cJSON_AddNumberToObject(value,"port",port->valueint);
        cJSON_AddStringToObject(value,"token",token);cJSON_AddBoolToObject(value,"enabled",enabled);
    } else if(!strcmp(action,"enable") && local &&
              cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(input,"enabled"))) {
        if(!*text(value,"address")){cJSON_Delete(value);copy_text(error,cap,"Connect and pair Orbit Zero on your computer first.");return 409;}
        cJSON_DeleteItemFromObjectCaseSensitive(value,"enabled");
        cJSON_AddBoolToObject(value,"enabled",cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(input,"enabled")));
    } else {cJSON_Delete(value);copy_text(error,cap,"Change Orbit Zero mode in the native app.");return 403;}
    char final[640],temporary[640];path(final,sizeof final,false);path(temporary,sizeof temporary,true);
    char *data=cJSON_PrintUnformatted(value);cJSON_Delete(value);
    if(!data){copy_text(error,cap,"Could not save companion settings.");return 503;}
    int fd=open(temporary,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);bool ok=false;
    if(fd>=0){size_t size=strlen(data);ok=write(fd,data,size)==(ssize_t)size && !fsync(fd);if(close(fd))ok=false;}
    if(ok)ok=!rename(temporary,final);
    if(ok){int dir=open(orbit.state_dir,O_RDONLY|O_DIRECTORY);ok=dir>=0 && !fsync(dir);if(dir>=0)close(dir);}
    free(data);if(!ok){unlink(temporary);copy_text(error,cap,"Could not save companion settings.");return 503;}
    return 200;
}
