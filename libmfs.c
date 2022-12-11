#include "mfs.h"
#include "udp.h"
#include "message.h"
#include <sys/select.h>
#include <sys/time.h>

#define BUFFER_SIZE (4096)
int SD = -1;
char *HOSTNAME;
int PORT;

message_t *sendMsg;
message_t *respMsg;
struct sockaddr_in sendAddr;

int exchangeMessage(message_t *sendMsg, message_t *respMsg) {
    struct sockaddr_in readAddr;
    fd_set readfds;
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    int retryLimit = 5;
    int retryCurr = 0;
    do {
        FD_ZERO(&readfds);
        FD_SET(SD, &readfds);
        UDP_Write(SD, &sendAddr, (char *)sendMsg, sizeof(message_t));
        int selectRes = select(SD+1,&readfds,NULL,NULL,&timeout);

        if(selectRes > 0) {
            if(UDP_Read(SD, &readAddr, (char *)respMsg, sizeof(message_t)) > 0) {
                return 0;
            }
            else {
                retryCurr++;
            }
        }
    }
    while(retryCurr<retryLimit);

    return -1;
}
/**
 * takes a host name and port number and uses those to 
 * find the server exporting the file system.
*/
int MFS_Init(char *hostname, int port) {
    free(sendMsg);
    free(respMsg);

    sendMsg = (message_t*)malloc(sizeof(message_t));
    respMsg = (message_t*)malloc(sizeof(message_t));
    
    SD = UDP_Open(0);
    if(SD < 0) {
        return -1;
    }
    int rc = UDP_FillSockAddr(&sendAddr, hostname, port);
    if(rc < 0) {
        return -1;
    }

    sendMsg->mtype = 1;
    if(exchangeMessage(sendMsg, respMsg) == 0) {
        return respMsg->rc;
    }
    return -1;
}

/**
 * takes the parent inode number (which should be the inode number of a directory) 
 * and looks up the entry name in it. The inode number of name is returned. 
 * Success: return inode number of name; 
 * failure: return -1. 
 * Failure modes: invalid pinum, name does not exist in pinum.
*/
int MFS_Lookup(int pinum, char *name) {
    if(SD < 0 || strlen(name) > 28) {
        return -1;
    }

    sendMsg->inum = pinum;
    strcpy((char*)sendMsg->name, name);
    sendMsg->mtype = 2;
    if(exchangeMessage(sendMsg, respMsg) == 0) {
        return respMsg->rc;
    }
    return -1;
}

/**
 * returns some information about the file specified by inum. 
 * Upon success, return 0, otherwise -1. 
 * The exact info returned is defined by MFS_Stat_t. 
 * Failure modes: inum does not exist. 
 * File and directory sizes are described below.
*/
int MFS_Stat(int inum, MFS_Stat_t *m) {
    if(SD < 0){
        return -1;
    }

    sendMsg->inum = inum;
    sendMsg->mtype = 3;

    if(exchangeMessage(sendMsg, respMsg) == 0) {
        m->size = respMsg->nbytes;
        m->type = respMsg->type;
        return respMsg->rc;
    }
    return -1;
}

/**
 * writes a buffer of size nbytes (max size: 4096 bytes) at the byte offset specified by offset. 
 * Returns 0 on success, -1 on failure. 
 * Failure modes: invalid inum, invalid nbytes, invalid offset, not a regular file (because you can't write to directories).
*/
int MFS_Write(int inum, char *buffer, int offset, int nbytes) {
    if(SD < 0) {
        return -1;
    }
    free(sendMsg);
    free(respMsg);

    sendMsg->inum = inum;
    memcpy((char *)sendMsg->buffer, buffer, BUFFER_SIZE);
    sendMsg->nbytes = nbytes;
    sendMsg->offset = offset;
    sendMsg->mtype = 4;

    if(exchangeMessage(sendMsg, respMsg) == 0) {
        return respMsg->rc;
    }
    return -1;
}

/**
 * reads nbytes of data (max size 4096 bytes) specified by the byte offset offset into the buffer from file specified by inum. 
 * The routine should work for either a file or directory; 
 * directories should return data in the format specified by MFS_DirEnt_t. 
 * Success: 0, failure: -1. Failure modes: invalid inum, invalid offset, invalid nbytes.
*/
int MFS_Read(int inum, char *buffer, int offset, int nbytes) {
    if(SD < 0) {
        return -1;
    }
    free(sendMsg);
    free(respMsg);

    sendMsg->inum = inum;
    sendMsg->nbytes = nbytes;
    sendMsg->offset = offset;
    sendMsg->mtype = 5;

    if(exchangeMessage(sendMsg, respMsg) == 0) {
        memcpy(buffer, respMsg->buffer, MFS_BLOCK_SIZE);
        return respMsg->rc;
    }
    return -1;
}

/**
 * makes a file (type == MFS_REGULAR_FILE) or directory (type == MFS_DIRECTORY) in the parent directory specified by pinum of name name. 
 * Returns 0 on success, -1 on failure. Failure modes: pinum does not exist, or name is too long. If name already exists, return success.
*/
int MFS_Creat(int pinum, int type, char *name) {
    if(SD < 0) {
        return -1;
    }

    sendMsg->inum = pinum;
    sendMsg->type = type;
    strcpy((char*)sendMsg->name, name);
    sendMsg->mtype = 6;

    if(exchangeMessage(sendMsg, respMsg) == 0) {
        return respMsg->rc;
    }
    return -1;
}

/**
 * removes the file or directory name from the directory specified by pinum. 
 * 0 on success, -1 on failure. Failure modes: pinum does not exist, directory is NOT empty. 
 * Note that the name not existing is NOT a failure by our definition (think about why this might be).
*/
int MFS_Unlink(int pinum, char *name) {
    if(SD < 0) {
        return -1;
    }

    sendMsg->inum = pinum;
    strcpy((char*)sendMsg->name, name);
    sendMsg->mtype = 7;

    if(exchangeMessage(sendMsg, respMsg) == 0) {
        return respMsg->rc;
    }
    return -1;
}

/**
 * just tells the server to force all of its data structures to disk and shutdown by calling exit(0). 
 * This interface will mostly be used for testing purposes.
*/
int MFS_Shutdown() {
    if(SD < 0) {
        return -1;
    }

    sendMsg->mtype = 8;
    if(exchangeMessage(sendMsg, respMsg) == 0) {
        return respMsg->rc;
    }
    return -1;
}