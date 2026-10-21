// One-shot startup diagnostic: no UIKit, game access, or background service.
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

int main(int argc,char **argv) {
    if (argc!=3 || strcmp(argv[1],"--check") ||
        (strcmp(argv[2],"mobile") && strcmp(argv[2],"root"))) return 64;
    const char *folder="/var/mobile/Library/Application Support/org.newstateport.External";
    char path[256];
    int length=snprintf(path,sizeof(path),"%s/spawn-check-%s.json",folder,argv[2]);
    if (length<0 || length>=(int)sizeof(path)) return 65;
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0644);
    if (fd<0) return 66;
    FILE *file=fdopen(fd,"w");
    if (!file) {close(fd);return 67;}
    int written=fprintf(file,"{\"probe\":\"%s\",\"pid\":%d,\"uid\":%u,\"euid\":%u}\n",
        argv[2],getpid(),getuid(),geteuid());
    int closed=fclose(file);
    if (written<0 || closed) return 68;
    return !strcmp(argv[2],"root") && geteuid()!=0 ? 69 : 0;
}
