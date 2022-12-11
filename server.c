#include "udp.h"
#include "ufs.h"
#include "message.h"
#include <sys/mman.h>
#include <stdio.h>

#define BUFFER_SIZE (4096)
int port, sd;
struct sockaddr_in socketAddr;
super_t *metaAddr;

void usage() {
    fprintf(stderr, "usage: server [portnum] [file-system-image]\n");
    exit(1);
}
int main(int argc, char *argv[]) {
    if(argc != 3) {
        usage();
    }
    port = atoi(argv[1]);
    char* fsimg = argv[2];

    int fd = open(fsimg, O_RDWR, S_IRWXU);
    assert(fd >= 0);

    sd = UDP_Open(port);
    assert(sd > -1);

    metaAddr = mmap(NULL, sizeof(super_t), PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);

    while(1) {
        message_t *receivedMsg;
        printf("server:: waiting...\n");
        int rc = UDP_Read(sd, &socketAddr, (char *)receivedMsg, sizeof(message_t));
        if(rc > 0) {
            switch (receivedMsg -> mtype)
            {
            case 1: // MFS_INIT
                /* code */
                break;
            
            case 2: // MFS_LOOKUP
                break;

            case 3: // MFS_STAT
                break;

            case 4: // MFS_WRITE
                /* code */
                break;
            
            case 5: // MFS_READ
                break;

            case 6: // MFS_CREAT
                break;

            case 7: // MFS_UNLINK
                break;

            case 8: // MFS_SHUTDOWN
                break;
            default:
                break;
            }
        }

    }



}