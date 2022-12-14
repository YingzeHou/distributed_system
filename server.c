#include "udp.h"
#include "ufs.h"
#include "mfs.h"
#include "message.h"
#include <sys/mman.h>
#include <stdio.h>
#include <sys/stat.h>

#define BLOCK_SIZE (4096)
int sd, fd;
int *fileAddr;
super_t *metaAddr;
int num_inode, num_data;

typedef struct {
	inode_t inodes[BLOCK_SIZE / sizeof(inode_t)];
} inode_block;

inode_t dummyInode;
inode_block inode_map;

typedef struct {
	dir_ent_t entries[128];
} dir_block_t;

dir_block_t dir_map;

int mfs_lookup(int pinum, char* name); // Yingze
inode_t mfs_stat(int inum); // Yingze
int mfs_write(int inum, char *buffer, int offset, int nbytes); // Yingze
int mfs_read(int inum, char *buffer, int offset, int nbytes); // Yingze
int mfs_create(int pinum, int type, char *name); // XinSu
int mfs_unlink(int pinum, char *name); // XinSu

void intHandler(int dummy) {
    UDP_Close(sd);
    exit(130);
}

void *get_addr(int addr) {
    return (char *)metaAddr + addr*BLOCK_SIZE;
}

unsigned int get_bit(unsigned int *bitmap, int position) {
   int index = position / 32;
   int offset = 31 - (position % 32);
//    printf("RC: %d\n", (bitmap[index] >> offset) & 0x1);
   return (bitmap[index] >> offset) & 0x1;
}

void set_bit(unsigned int *bitmap, int position) {
   int index = position / 32;
   int offset = 31 - (position % 32);
   bitmap[index] |= 0x1 << offset;
}

void usage() {
    fprintf(stderr, "usage: server [portnum] [file-system-image]\n");
    exit(1);
}
int main(int argc, char *argv[]) {
    if(argc != 3) {
        usage();
        exit(1);
    }

    dummyInode.type = -1;
    signal(SIGINT, intHandler);
    int port = atoi(argv[1]);
    char* fsimg = argv[2];

    fd = open(fsimg, O_RDWR,S_IRWXU|S_IRUSR);
    assert(fd >= 0);

    struct stat fileStat;
    if(fstat(fd,&fileStat) < 0) 
    {
        exit(1);
    }
    sd = UDP_Open(port);
    assert(sd > -1);

    fileAddr = mmap(NULL, fileStat.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
    metaAddr = (super_t *) fileAddr;

    while(1) {
        struct sockaddr_in socketAddr;
        printf("server:: waiting...\n");
        message_t receivedMsg;
        int rc = UDP_Read(sd, &socketAddr, (char *)&receivedMsg, sizeof(message_t));
        printf("server:: read message [size:%d mtype:%d name:%s inode:%d]\n", rc, receivedMsg.mtype, (char *)receivedMsg.name, receivedMsg.inum);
        if(rc > 0) {
            message_t replyMsg;
            switch (receivedMsg.mtype)
            {
            case 1: // MFS_INIT
                printf("Server Init\n");
                replyMsg.rc = 0;
                UDP_Write(sd, &socketAddr, (char *)&replyMsg, sizeof(message_t));
                break;
            
            case 2: // MFS_LOOKUP   
                replyMsg.rc = mfs_lookup(receivedMsg.inum, receivedMsg.name);
                UDP_Write(sd, &socketAddr, (char *)&replyMsg, sizeof(message_t));
                break;

            case 3: // MFS_STAT
                printf("Server Stat\n");
                replyMsg.rc = -1;
                inode_t inode = mfs_stat(receivedMsg.inum);
                // if(inode.type!=-1) {
                    replyMsg.rc = 0;
                    replyMsg.nbytes = inode.size;
                    replyMsg.type = inode.type;
                    printf("SIZE: %d; TYPE: %d\n", replyMsg.nbytes, replyMsg.type);
                // }
                UDP_Write(sd, &socketAddr, (char *)&replyMsg, sizeof(message_t));
                break;

            case 4: // MFS_WRITE
                printf("Server Write\n");
                replyMsg.rc = mfs_write(receivedMsg.inum, receivedMsg.buffer, receivedMsg.offset, receivedMsg.nbytes);
                UDP_Write(sd, &socketAddr, (char *)&replyMsg, sizeof(message_t));
                break;
            
            case 5: // MFS_READ
                printf("Server Read\n");
                char *buffer = (char *)malloc(BLOCK_SIZE);
                replyMsg.rc = mfs_read(receivedMsg.inum, buffer, receivedMsg.offset, receivedMsg.nbytes);
                memcpy(replyMsg.buffer, buffer, BLOCK_SIZE);
                UDP_Write(sd, &socketAddr, (char *)&replyMsg, sizeof(message_t));
                free(buffer);
                break;

            case 6: // MFS_CREAT
                printf("Server Create\n");
                replyMsg.rc = mfs_create(receivedMsg.inum, receivedMsg.type, receivedMsg.name);
                UDP_Write(sd, &socketAddr, (char *)&replyMsg, sizeof(message_t));
                break;

            case 7: // MFS_UNLINK
                UDP_Write(sd, &socketAddr, (char *)&replyMsg, sizeof(message_t));
                break;

            case 8: // MFS_SHUTDOWN
                replyMsg.rc = 0;
                UDP_Write(sd, &socketAddr, (char *)&replyMsg, sizeof(message_t));
                exit(0);
                break;

            default:
                break;
            }
            // free(receivedMsg);
            // free(replyMsg);
        }


    }
    free(fileAddr);
    // fileAddr = 0;
    // free(metaAddr);
    close(fd);
}

inode_t get_inode(int pinum) {
    if(get_bit(get_addr(metaAddr->inode_bitmap_addr), pinum) == 0) {
        return dummyInode;
    }
    int inode_offset = pinum * sizeof(inode_t) + metaAddr->inode_region_addr*BLOCK_SIZE;
    int rc = lseek(fd, inode_offset, SEEK_SET);
    if(rc < 0) {
        return dummyInode;
    }
    inode_t inode = inode_map.inodes[pinum];
    // inode_t inode = (inode_t *)malloc(sizeof(inode_t));
    rc = read(fd, &inode, sizeof(inode_t));

    printf("INODE inum: %d; size: %d; type: %d\n", pinum, inode.size, inode.type);
    if(rc < 0) {
        return dummyInode;
    }

    return inode;
}

int mfs_lookup(int pinum, char* name) {
    if(pinum<0 || pinum>=metaAddr->num_inodes) { // invalid pinum
        return -1;
    }

    inode_t inode = get_inode(pinum);

    if(inode.type!=MFS_DIRECTORY) {
        // free(inode);
        return -1;
    }

    // dir_ent_t *entries = (dir_ent_t *)malloc(sizeof(dir_ent_t) * 128);
    
    for(int i = 0; i<DIRECT_PTRS; i++) {
        int block_ptr = inode.direct[i];
        // printf("DIRECT: %d\n", inode->direct[i]);
        lseek(fd, block_ptr*BLOCK_SIZE, SEEK_SET);

        read(fd, &dir_map.entries, sizeof(dir_ent_t)*128);

        for(int j = 0; j<128; j++) {
            dir_ent_t entry = dir_map.entries[j];
            // printf("ENTRY: %d\n", entry.inum);
            if(entry.inum == -1) {
                continue;
            }
            if(strcmp(entry.name, name) == 0){
                // free(entries);
                // free(inode);
                return entry.inum;
            }
        }
    }
    // free(entries);
    // free(inode);
    return -1;
}

inode_t mfs_stat(int inum) {
    if(inum<0 || inum>=metaAddr->num_inodes) { // invalid pinum
        return dummyInode;
    }

    inode_t inode = get_inode(inum);
    if(inode.type == -1) {
        return dummyInode;
    }
    return inode;
}

int mfs_write(int inum, char *buffer, int offset, int nbytes) {
    if(nbytes<0 || nbytes>BLOCK_SIZE) {
        printf("BYTES TOO LARGE");
        return -1;
    }
    if(offset/BLOCK_SIZE >= 30) {
        return -1;
    }

    if(inum<0 || inum>=metaAddr->num_inodes) { // invalid pinum
        printf("INVALID PINUM");
        return -1;
    }
    inode_t inode = get_inode(inum);
    if(inode.type == MFS_DIRECTORY){
        // free(inode);
        printf("WRONG TYPE");
        return -1;
    }
    // if(inode.size < offset) {
    //     // free(inode);
    //     return -1;
    // }

    int block_ptr = inode.direct[offset/BLOCK_SIZE];

    lseek(fd, block_ptr*BLOCK_SIZE, SEEK_SET);
    write(fd, buffer, nbytes);

    printf("BEFORE WRITE SIZE: %d\n", inode.size);
    inode.size += nbytes;
    printf("AFTER WRITE SIZE: %d\n", inode.size);
    lseek(fd, inum*sizeof(inode_t)+metaAddr->inode_region_addr*BLOCK_SIZE, SEEK_SET);
    write(fd, &inode, sizeof(inode_t));

    // free(inode);
    return 0;
}

int mfs_read(int inum, char *buffer, int offset, int nbytes) {
    if(nbytes<0 || nbytes>4096) {
        return -1;
    }

    if(offset/BLOCK_SIZE >= 30) {
        return -1;
    }
    inode_t inode = get_inode(inum);
    // if(inode == NULL) {
    //     free(inode);
    //     return -1;
    // }
    int block_ptr = inode.direct[offset/BLOCK_SIZE];
    lseek(fd, block_ptr*BLOCK_SIZE, SEEK_SET);
    if(inode.type == MFS_REGULAR_FILE) {
        read(fd, buffer, nbytes);
        printf("BUFFER CONTENT: %s\n", buffer);
    }
    else {
        read(fd, (MFS_DirEnt_t*) buffer, nbytes);
    }
    // free(inode);
    return 0;
}

int get_available_inum() {
    for(int i = 0; i<metaAddr->num_inodes; i++) {
        int bit = get_bit(get_addr(metaAddr->inode_bitmap_addr), i);
        if(bit==0) {
            return i;
        }
    }
    return -1;
}

int get_available_datablock_addr() {
    for(int i = 0; i<metaAddr->num_data; i++) {
        int bit = get_bit(get_addr(metaAddr->data_bitmap_addr), i);
        if(bit == 0) {
            return i+metaAddr->data_region_addr;
        }
    }
    return -1;
}

int mfs_create(int pinum, int type, char *name) {
    if(strlen(name) > 28) {
        return -1;
    }
    inode_t parent_inode = get_inode(pinum);
    if(parent_inode.type != MFS_DIRECTORY) {
        return -1;
    }
    printf("Parent Direct: %d\n", parent_inode.direct[0]);
    lseek(fd, parent_inode.direct[0]*BLOCK_SIZE, SEEK_SET);
    // dir_ent_t *parent_entry = (dir_ent_t *)malloc(BLOCK_SIZE);

    read(fd, &dir_map.entries, BLOCK_SIZE);
    int freeinum = get_available_inum();
    printf("Free inum: %d\n", freeinum);
    for(int i = 0; i< BLOCK_SIZE/sizeof(dir_ent_t); i++) {
        printf("Inum: %d\n", dir_map.entries[i].inum);
        if(dir_map.entries[i].inum == -1) {
            dir_map.entries[i].inum = freeinum;
            printf("Index: %d, value: %d\n", i, freeinum);
            strcpy(dir_map.entries[i].name, name);
            set_bit(get_addr(metaAddr->inode_bitmap_addr), freeinum);
            parent_inode.size += sizeof(dir_ent_t);
            break;
        }
    }

    lseek(fd, parent_inode.direct[0]*BLOCK_SIZE, SEEK_SET);
    write(fd, &dir_map.entries, BLOCK_SIZE);

    // inode_t *new_inode = (inode_t *)malloc(sizeof(inode_t));
    // new_inode->type = type;
    inode_map.inodes[freeinum].type = type;

    if(type == MFS_DIRECTORY) {
        // dir_ent_t *new_dir_entries = malloc(BLOCK_SIZE);

        inode_map.inodes[freeinum].size = 2 * sizeof(dir_ent_t);
        strcpy(dir_map.entries[0].name, ".");
        dir_map.entries[0].inum = freeinum;
        strcpy(dir_map.entries[1].name, "..");
        dir_map.entries[1].inum = pinum;
        for(int i = 1; i<DIRECT_PTRS; i++) {
            inode_map.inodes[freeinum].direct[i] = -1;
        }
        for(int i = 2; i<BLOCK_SIZE/sizeof(dir_ent_t); i++) {
            dir_map.entries[i].inum = -1;
        }

        int new_dir_addr = get_available_datablock_addr();
        inode_map.inodes[freeinum].direct[0] = new_dir_addr;
        set_bit(get_addr(metaAddr->data_bitmap_addr), new_dir_addr-metaAddr->data_region_addr);

        lseek(fd, new_dir_addr*BLOCK_SIZE, SEEK_SET);
        write(fd, &dir_map.entries, BLOCK_SIZE);

        // free(new_dir_entries);
    }
    else {
        for(int i = 0; i<DIRECT_PTRS; i++) {
            inode_map.inodes[freeinum].direct[i] = get_available_datablock_addr();
            // printf("new addr: %d\n", new_inode->direct[i]);
            set_bit(get_addr(metaAddr->data_bitmap_addr), inode_map.inodes[freeinum].direct[i]-metaAddr->data_region_addr);
        }
    }
    lseek(fd, metaAddr->inode_bitmap_addr*BLOCK_SIZE, SEEK_SET);
    write(fd, get_addr(metaAddr->inode_bitmap_addr), BLOCK_SIZE);

    lseek(fd, metaAddr->data_bitmap_addr*BLOCK_SIZE, SEEK_SET);
    write(fd, get_addr(metaAddr->data_bitmap_addr), BLOCK_SIZE);

    lseek(fd, freeinum*sizeof(inode_t)+metaAddr->inode_region_addr*BLOCK_SIZE, SEEK_SET);
    write(fd, &inode_map.inodes[freeinum], sizeof(inode_t));

    // free(parent_inode);
    // free(parent_entry);
    // free(new_inode);
    fsync(fd);
    return 0;
}
