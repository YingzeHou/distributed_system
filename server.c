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
    inode_t inodes[UFS_BLOCK_SIZE / sizeof(inode_t)];
} inode_block;

// inode_block inode_map;

typedef struct {
	dir_ent_t entries[128];
} dir_block_t;

// dir_block_t dir_block

int mfs_lookup(int pinum, char* name); // Yingze
inode_t *mfs_stat(int inum); // Yingze
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
    signal(SIGINT, intHandler);
    int port = atoi(argv[1]);
    char* fsimg = argv[2];

    fd = open(fsimg, O_RDWR,S_IRWXU|S_IRUSR);
    assert(fd >= 0);

    struct stat fileStat;
    if(fstat(fd,&fileStat) < 0) 
    {
        //error("Cannot open file");
        exit(1);
    }
    sd = UDP_Open(port);
    assert(sd > -1);

    fileAddr = mmap(NULL, fileStat.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    metaAddr = (super_t *) fileAddr;

    while(1) {
        struct sockaddr_in socketAddr;
        printf("server:: waiting...\n");
        message_t *receivedMsg = malloc(sizeof(message_t));
        message_t *replyMsg = malloc(sizeof(message_t));
        int rc = UDP_Read(sd, &socketAddr, (char *)receivedMsg, sizeof(message_t));
        printf("server:: read message [size:%d mtype:%d name:%s inode:%d]\n", rc, receivedMsg->mtype, (char *)receivedMsg->name, receivedMsg->inum);
        if(rc > 0) {
            switch (receivedMsg -> mtype)
            {
            case 1: // MFS_INIT
                printf("Server Init\n");
                replyMsg->rc = 0;
                UDP_Write(sd, &socketAddr, (char *)replyMsg, sizeof(message_t));
                break;
            
            case 2: // MFS_LOOKUP   
                replyMsg->rc = mfs_lookup(receivedMsg->inum, receivedMsg->name);
                UDP_Write(sd, &socketAddr, (char *)replyMsg, sizeof(message_t));
                break;

            case 3: // MFS_STAT
                printf("Server Init\n");
                replyMsg->rc = -1;
                inode_t *inode = mfs_stat(receivedMsg->inum);
                if(inode!=NULL) {
                    replyMsg->rc = 0;
                    replyMsg->nbytes = inode->size;
                    replyMsg->type = inode->type;
                    printf("SIZE: %d; TYPE: %d\n", replyMsg->nbytes, replyMsg->type);
                }
                UDP_Write(sd, &socketAddr, (char *)replyMsg, sizeof(message_t));
                break;

            case 4: // MFS_WRITE
                replyMsg->rc = mfs_write(receivedMsg->inum, receivedMsg->buffer, receivedMsg->offset, receivedMsg->nbytes);
                UDP_Write(sd, &socketAddr, (char *)replyMsg, sizeof(message_t));
                break;
            
            case 5: // MFS_READ
                printf("Server Read\n");
                char *buffer = malloc(BLOCK_SIZE);
                replyMsg->rc = mfs_read(receivedMsg->inum, buffer, receivedMsg->offset, receivedMsg->nbytes);
                memcpy((char*)replyMsg->buffer, buffer, sizeof(buffer));
                UDP_Write(sd, &socketAddr, (char *)replyMsg, sizeof(message_t));
                break;

            case 6: // MFS_CREAT
                printf("Server Create\n");
                replyMsg->rc = mfs_create(receivedMsg->inum, receivedMsg->type, receivedMsg->name);
                UDP_Write(sd, &socketAddr, (char *)replyMsg, sizeof(message_t));
                break;

            case 7: // MFS_UNLINK
                UDP_Write(sd, &socketAddr, (char *)replyMsg, sizeof(message_t));
                break;

            case 8: // MFS_SHUTDOWN
                close(fd);
                receivedMsg->rc = 0;
                UDP_Write(sd, &socketAddr, (char *)replyMsg, sizeof(message_t));
                exit(0);
                break;

            default:
                break;
            }
        }

    }
}

inode_t *get_inode(int pinum) {
    if(get_bit(get_addr(metaAddr->inode_bitmap_addr), pinum) == 0) {
        return NULL;
    }
    int inode_offset = pinum * sizeof(inode_t) + metaAddr->inode_region_addr*BLOCK_SIZE;
    int rc = lseek(fd, inode_offset, SEEK_SET);
    if(rc < 0) {
        return NULL;
    }
    inode_t *inode = malloc(sizeof(inode_t));
    rc = read(fd, inode, sizeof(inode_t));

    // printf("INODE: %d\n", inode->direct[0]);
    if(rc < 0) {
        return NULL;
    }
    return inode;
}

int mfs_lookup(int pinum, char* name) {
    if(pinum<0 || pinum>=metaAddr->num_inodes) { // invalid pinum
        return -1;
    }

    inode_t *inode = get_inode(pinum);

    if(inode == NULL || inode->type!=MFS_DIRECTORY) {
        return -1;
    }

    dir_ent_t *entries = malloc(sizeof(dir_ent_t) * 128);
    for(int i = 0; i<DIRECT_PTRS; i++) {
        int block_ptr = inode->direct[i];
        // printf("DIRECT: %d\n", inode->direct[i]);
        int rc = lseek(fd, block_ptr*BLOCK_SIZE, SEEK_SET);
        if(rc < 0) {
            return -1;
        }

        rc = read(fd, entries, sizeof(dir_ent_t)*128);
        if(rc < 0) {
            return -1;
        }

        for(int j = 0; j<128; j++) {
            dir_ent_t entry = entries[j];
            // printf("ENTRY: %d\n", entry.inum);
            if(entry.inum == -1) {
                continue;
            }
            if(strcmp(entry.name, name) == 0){
                return entry.inum;
            }
        }
    }
    return -1;
}

inode_t *mfs_stat(int inum) {
    if(inum<0 || inum>=metaAddr->num_inodes) { // invalid pinum
        return NULL;
    }

    inode_t *inode = get_inode(inum);
    if(inode == NULL) {
        return NULL;
    }
    return inode;
}

int mfs_write(int inum, char *buffer, int offset, int nbytes) {
    if(nbytes<0 || nbytes>=BLOCK_SIZE) {
        return -1;
    }
    if(inum<0 || inum>=metaAddr->num_inodes) { // invalid pinum
        return -1;
    }
    inode_t *inode = get_inode(inum);
    if(inode == NULL || inode->type == MFS_DIRECTORY){
        return -1;
    }
    if(inode->size < offset) {
        return -1;
    }

    int block_ptr = inode->direct[offset/BLOCK_SIZE];

    int rc = lseek(fd, block_ptr*BLOCK_SIZE, SEEK_SET);
    if(rc<0) {
        return -1;
    }
    write(fd, buffer, nbytes);
    return 0;
    
}

int mfs_read(int inum, char *buffer, int offset, int nbytes) {
    if(nbytes<0 || nbytes>=4096) {
        return -1;
    }
    inode_t *inode = get_inode(inum);
    if(inode == NULL) {
        return -1;
    }
    int block_ptr = inode->direct[offset/BLOCK_SIZE];
    int rc = lseek(fd, block_ptr*BLOCK_SIZE, SEEK_SET);
    if(rc<0) {
        return -1;
    }
    if(inode->type == MFS_REGULAR_FILE) {
        rc = read(fd, buffer, nbytes);
        if(rc<0) {
            return -1;
        }
    }
    else {
        rc = read(fd, (MFS_DirEnt_t*) buffer, nbytes);
        if(rc<0) {
            return -1;
        }    
    }
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
}

inode_t *allocate_inode(int inum) {
    int inode_offset = inum * sizeof(inode_t) + metaAddr->inode_region_addr*BLOCK_SIZE;
    lseek(fd, inode_offset, SEEK_SET);
    inode_t *new_inode = malloc(sizeof(inode_t));
    write(fd, new_inode, sizeof(new_inode));
    return new_inode;
}

int mfs_create(int pinum, int type, char *name) {
    if(strlen(name) > 28) {
        return -1;
    }
    inode_t *parent_inode = get_inode(pinum);
    lseek(fd, parent_inode->direct[0]*BLOCK_SIZE, SEEK_SET);
    dir_ent_t *parent_entry = malloc(BLOCK_SIZE);
    read(fd, parent_entry, BLOCK_SIZE);
    int freeinum = get_available_inum();
    for(int i = 0; i< BLOCK_SIZE/sizeof(dir_ent_t); i++) {
        if(parent_entry[i].inum == -1) {
            parent_entry[i].inum = freeinum;
            // printf("Index: %d, value: %d\n", i, freeinum);
            strcpy(parent_entry[i].name, name);
            set_bit(get_addr(metaAddr->inode_bitmap_addr), freeinum);
            parent_inode->size += sizeof(dir_ent_t);
            break;
        }
    }

    lseek(fd, parent_inode->direct[0]*BLOCK_SIZE, SEEK_SET);
    write(fd, parent_entry, BLOCK_SIZE);

    inode_t *new_inode = allocate_inode(freeinum);
    new_inode->type = type;

    if(type == MFS_DIRECTORY) {
        dir_ent_t *new_dir_entries = malloc(BLOCK_SIZE);
        new_inode->size = 2 * sizeof(dir_ent_t);
        strcpy(new_dir_entries[0].name, ".");
        new_dir_entries[0].inum = freeinum;
        strcpy(new_dir_entries[1].name, ".");
        new_dir_entries[1].inum = pinum;
        for(int i = 1; i<DIRECT_PTRS; i++) {
            new_inode->direct[i] = -1;
        }
        for(int i = 2; i<BLOCK_SIZE/sizeof(dir_ent_t); i++) {
            new_dir_entries[i].inum = -1;
        }

        int new_dir_addr = get_available_datablock_addr();
        new_inode->direct[0] = new_dir_addr;
        set_bit(get_addr(metaAddr->data_bitmap_addr), new_dir_addr-metaAddr->data_region_addr);

        lseek(fd, new_dir_addr*BLOCK_SIZE, SEEK_SET);
        write(fd, new_dir_entries, BLOCK_SIZE);
    }
    else {
        for(int i = 0; i<DIRECT_PTRS; i++) {
            new_inode->direct[i] = get_available_datablock_addr();
            set_bit(get_addr(metaAddr->data_bitmap_addr), new_inode->direct[i]-metaAddr->data_region_addr);

        }
    }

    fsync(fd);
    return 0;
}
