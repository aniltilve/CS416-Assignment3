/*
  Simple File System

  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.

*/

#include "params.h"
#include "block.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"

///////////////////////////////////////////////////////////
// MACROS
///////////////////////////////////////////////////////////

//size of disk
//#define DISK 16777216 not necessary since we are using blocks as our granularity

#define NUM_BLKS 2048                            //number of blocks in our disk
#define INODE_NUM 32                             //number of inodes (can be altered)
#define INODE_SIZE 128                           //bytes
#define INODES_PER_BLK (BLOCK_SIZE / INODE_SIZE) //number of inodes per block
#define INODE_BLK_NUM 10                       //last index value for inodes (inode index limit)
#define FILE_SYS_ID 0x0495                       //unique filesystem identifier (check for this in init())

///////////////////////////////////////////////////////////
// STRUCTS
///////////////////////////////////////////////////////////

//inode structure
typedef struct Inode_t
{
    unsigned int mode,                      // file permissions (read, write, execute)
        usr_id,                             // user id
        grp_id,                             // group id
        acc_time,                           // access time
        chg_time,                           // change time
        mod_time,                           // modification time
        num_links,                          // number of hard links to file
        file_sz,                            // file size
        num_alloc_blks,                     // number of blocks allocated to the file
        disk_blks[INODE_BLK_NUM];         // disk pointers to file's data blocks
    unsigned char padding[INODE_SIZE - 74]; // used to make this struct's size a power of 2
} Inode;

//superblock structure
typedef struct SuperBlock_t
{
    unsigned int file_sys_id, //filesystem ID
        blocks,               //total number of blocks
        root,                 //inode number of root directory
        inode_start,          //starting index of inode
        inode_blocks,         //number of inode blocks
        inode_bmp_start_idx,   //starting index of inode bitmap
        inode_bitmap_blocks,  //number of inode bitmap blocks
        data_bmp_start_idx,           //starting index of data
        data_blocks;          //number of data blocks
} SuperBlock;

///////////////////////////////////////////////////////////
// HELPER FUNCTIONS
///////////////////////////////////////////////////////////

void read_sup_blk_and_inode(SuperBlock *sup_blk, char[] buf, Inode *inode)
{
    memset(buf, 0, BLOCK_SIZE);
    block_read(0, buf);
    memcpy((void *)sup_blk, (void *)buf, sizeof(SuperBlock));
    memset(buf, 0, BLOCK_SIZE);
    block_read(sup_blk.inode_start, buf);
    memcpy((void *)inode, (void *)buf, sizeof(Inode));
}

void write_sup_blk_and_inode(SuperBlock *sup_blk, char[] buf, Inode *inode)
{
    memset((void *)buf, 0, BLOCK_SIZE);
    memcpy((void *)buf, (void *)sup_blk, sizeof(SuperBlock));
    block_write(0, (void *)buf);
    memset((void *)buf, 0, BLOCK_SIZE);
    memcpy((void *)buf, (void *)inode, sizeof(Inode));
    block_write(sup_blk.inode_start, (void *)buf);
}

///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void *sfs_init(struct fuse_conn_info *conn)
{
    fprintf(stderr, "in bb-init\n");
    log_msg("\nsfs_init()\n");

    log_conn(conn);
    log_fuse_context(fuse_get_context());

    char buf[BLOCK_SIZE];
    int ret; //bytes retrieved
    SuperBlock sup_blk;
    Inode inode;

    memset(buf, 0, BLOCK_SIZE);
    disk_open(SFS_DATA->diskfile);

    if ((ret = block_read(0, buf)) <= 0)
    {
        sup_blk.file_sys_id = FILE_SYS_ID;
        sup_blk.blocks = NUM_BLKS;
        sup_blk.root = 0;
        sup_blk.inode_start = (unsigned int)(sizeof(SuperBlock) / BLOCK_SIZE) + 1;
        sup_blk.inode_blocks = INODE_NUM / INODES_PER_BLK;
        sup_blk.inode_bmp_start_idx = sup_blk.inode_start + sup_blk.inode_blocks;
        sup_blk.inode_bitmap_blocks = 1;
        sup_blk.data_bmp_start_idx = sup_blk.inode_bmp_start_idx + sup_blk.inode_bitmap_blocks;
        sup_blk.data_blocks = 0;

        inode.mode = S_IFDIR | S_IRWXU | S_IRWXG;
        inode.usr_id = getuid();
        inode.grp_id = getgid();
        inode.acc_time = time(NULL);
        inode.chg_time = inode.acc_time;
        inode.mod_time = inode.acc_time;
        inode.num_links = 1;
        inode.file_sz = 0;
        inode.num_alloc_blks = 0;
        /*
        memset((void*)buf, 0, BLOCK_SIZE);
        memcpy((void*)buf, (void*)&sup_blk, sizeof(SuperBlock));
        block_write(0, (void*) buf);
        memset((void*)buf, 0, BLOCK_SIZE);
        memcpy((void*)buf, (void*)&inode, sizeof(Inode));
        block_write(sup_blk.inode_start, (void*)buf);*/
        write_sup_blk_and_inode(&sup_blk, buf, &inode);
    }
    else
    {
        memcpy((void *)&sup_blk, (void *)buf, sizeof(SuperBlock));
        if (sup_blk.file_sys_id != FILE_SYS_ID)
        {
            //not our filesystem, overwrite?
        }
    }

    return SFS_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void sfs_destroy(void *userdata)
{
    disk_close();
    log_msg("\nsfs_destroy(userdata=0x%08x)\n", userdata);
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int sfs_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n", path, statbuf);

    char buf[BLOCK_SIZE], *info;
    struct dirent *entry;
    SuperBlock sup_blk;
    Inode root, ino;
    int num_ent, idx;
    /*
	memset(buf, 0, BLOCK_SIZE);
	block_read(0, buf);
	memcpy((void*)&sup_blk, (void*)buf, sizeof(SuperBlock));
	memset(buf, 0, BLOCK_SIZE);
	block_read(sup_blk.inode_start, buf);
	memcpy((void*)&root, (void*)buf, sizeof(Inode));
	*/
    read_sup_blk_and_inode(&sup_blk, buf, &root);

    if (strcmp("/", path) == 0)
    {
        statbuf->st_mode = root.mode;
        statbuf->st_uid = root.usr_id;
        statbuf->st_gid = root.grp_id;
        statbuf->st_rdev = 0; //not part of our implementation
        statbuf->st_atime = root.acc_time;
        statbuf->st_ctime = root.chg_time;
        statbuf->st_mtime = root.inode_mtime;
        statbuf->st_nlink = root.num_links;
        statbuf->st_blocks = root.num_alloc_blks;
        statbuf->st_size = root.file_sz;
    }
    else
    {
        info = (char *)malloc(BLOCK_SIZE * root.num_alloc_blks);

        for (idx = 0; (unsigned int)idx != root.num_alloc_blks; idx++)
        {
            memset(buf, 0, BLOCK_SIZE);
            block_read(root.disk_blks[idx], buf);
            memcpy((void *)&info[BLOCK_SIZE * idx], (void *)buf, BLOCK_SIZE);
        }

        num_ent = root.file_sz / sizeof(struct dirent);
        entry = (struct dirent *)info;
        for (idx = 0; idx != num_ent; idx++)
        {
            if (strcmp(&path[1], entry[idx].d_name) == 0)
            {
                //get this inode
                memset(buf, 0, BLOCK_SIZE);
                block_read(sup_blk.inode_start + (unsigned int)((entry[idx].d_ino) / INODES_PER_BLK), buf);
                memcpy((void *)&ino, (void *)&((Inode *)buf)[(entry[idx].d_ino) % INODES_PER_BLK], sizeof(Inode));
                statbuf->st_mode = root.mode;
                statbuf->st_uid = root.usr_id;
                statbuf->st_gid = root.grp_id;
                statbuf->st_rdev = 0; //not part of our implementation
                statbuf->st_atime = root.acc_time;
                statbuf->st_ctime = root.chg_time;
                statbuf->st_mtime = root.inode_mtime;
                statbuf->st_nlink = root.num_links;
                statbuf->st_blocks = root.num_alloc_blks;
                statbuf->st_size = root.file_sz;
                break;
            }
        }

        if (idx == num_ent)
            statbuf->st_mode = S_IFREG | S_IRWXU | S_IRWXG;

        free(info);
    }

    return retstat;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int sfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
	    path, mode, fi);
	    
	char buf[BLOCK_SIZE], *data, *inodes_data;
    struct dirent *entry;
    SuperBlock sup_blk;
    Inode root, *inodes_table;
    int num_ent, idx, j, k, m, off, blk_idx;
    unsigned char *byte;
    read_sup_blk_and_inode(&sup_blk, buf, &root);
    
    //see if path exists and create if not
    num_ent = root.file_sz / sizeof(struct dirent); //number of enties
    entry = (struct dirent *)info;
    
    for (idx = 0; idx != num_ent; idx++)
    { //searches for inode
        if (strcmp(&path[1], entry[idx].d_name) == 0)
        {
            break;
        }
    }
    
    if (idx != num_ent)
    { //file exists
        printf("File path (%s) exists already\n", path);
        fi->fh = idx;
        return retstat;
    }
    else
    {
    	  //find an open block and inode
    	 /* 
    	  block_read(sup_blk.inode_bitmap_blocks,buf);//reads in data bitmap
    	  memcpy(&blockBitmap,buf,BLOCK_SIZE);//copies bitmap to variable
    	  for(idx =0; idx<NUM_BLKS ;idx++){//finds open block
    	  
    	  		if(blockBitmap[idx]=='1'){//found a free block
    	  			blockBitmap[idx]='0';
    	  		}
    	  		
    	  }
    	  block_read(sup_blk.inode_bmp_start_idx,buf);//reads in inode bitmap
    	  memcpy(&inodeBitmap,buf,BLOCK_SIZE);//copies bitmap to variable
    	  for(j =0; idx<NUM_BLKS ;j++){//finds open inode
    	  		if(inodeBitmap[j]=='1'){//found a free inode
    	  			inodeBitmap[j]='0';
    	  		}
    	  }
          */ 
          
        //create file
        if (idx == num_ent)
        {
            fi->fh = idx;
            // read all inodes
            inodes_data = malloc(BLOCK_SIZE * sup_blk.s_ino_blocks);
            for (j = 0; j != sup_blk.s_ino_blocks; ++j)
            {
                memset(buf, 0, BLOCK_SIZE);
                block_read(sup_blk.s_ino_start + j, buf);
                memcpy((void *)&inodes_data[BLOCK_SIZE * j], (void *)buf, BLOCK_SIZE);
            }
            // find first free inode
            inodes_table = (Inode *)inodes_data;
            for (j = 1; j != INODE_NUM; ++j)
            {
                if (inodes_table[j].num_links == 0)
                {
                    break;
                }
            }
            // if there is no more inode
            if (j == INODE_NUM)
            {
                retstat = -1;
            }
            else
            {
                // create this inode
                inodes_table[j].num_links = 1;
                inodes_table[j].mode = S_IFREG | S_IRWXU | S_IRWXG;
                inodes_table[j].usr_id = getuid();
                inodes_table[j].grp_id = getgid();
                inodes_table[j].acc_time = time(NULL);
                inodes_table[j].chg_time = inodes_table[j].acc_time;
                inodes_table[j].mod_time = inodes_table[j].acc_time;
                inodes_table[j].file_sz = 0;
                inodes_table[j].num_alloc_blks = 0;
                block_write(sup_blk.s_ino_start + (unsigned int)(j / INODES_PER_BLK), &inodes_data[BLOCK_SIZE * (unsigned int)(j / INODES_PER_BLK)]);
                // make a new entry
                entry = (struct dirent *)malloc(sizeof(struct dirent));
                entry->d_ino = sup_blk.s_root + j;
                if (strlen(&path[1]) > 256)
                    retstat = -1;
                else
                    memcpy(entry->d_name, &path[1], sizeof(path) - 1);
                // need a new block
                if (root.file_sz + sizeof(struct dirent) > BLOCK_SIZE * root.num_alloc_blks)
                {
                    // find first free data block
                    memset(buf, 0, BLOCK_SIZE);
                    block_read(sup_blk.s_bitmap_start, buf);
                    blk_idx = sup_blk.data_bmp_start_idx;
                    for (k = 0; k != BLOCK_SIZE; ++k)
                    {
                        byte = (unsigned char *)&buf[k];
                        for (m = 0; m != 8; ++m)
                        {
                            if (((*byte >> m) & 1) == 0)
                            {
                                *byte |= 1 << m;
                                break;
                            }
                            blk_idx++;
                        }
                        if (m != 8)
                            break;
                    }
                    root.disk_blks[root.num_alloc_blks] = blk_idx;
                    block_write(sup_blk.s_bitmap_start, buf);
                }

                // write this entry
                off = root.num_alloc_blks * BLOCK_SIZE - root.file_sz;
                if (off != 0)
                {
                    memcpy(&data[root.file_sz], entry, off);
                    memcpy(&buf, &data[BLOCK_SIZE * (root.num_alloc_blks - 1)], BLOCK_SIZE);
                    block_write(root.disk_blks[root.num_alloc_blks - 1], buf);
                }
                if (off < sizeof(struct dirent))
                {
                    memset(buf, 0, BLOCK_SIZE);
                    memcpy((void *)buf, &((unsigned char*)entry)[off], sizeof(struct dirent) - off);
                    block_write(root.disk_blks[root.num_alloc_blks], buf);
                }

                root.num_alloc_blks++;
                sup_blk.s_ino_blocks++;
                root.file_sz += sizeof(struct dirent);

                //write root and superblock back
                memset(buf, 0, BLOCK_SIZE);
                block_read(sup_blk.s_ino_start, buf);
                memcpy((void *)buf, (void *)&root, sizeof(Inode));
                block_write(sup_blk.s_ino_start, buf);
                memset(buf, 0, BLOCK_SIZE);
                memcpy((void *)buf, (void *)&sup_blk, sizeof(SuperBlock));
                block_write(0, buf);

                free(entry);
            }

            free(inodes_data);
        }

        free(data);

        //update superblock and inode
        retstat = sfs_open(path, fi); //open file
    }
    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", path);

    char buf[BLOCK_SIZE], *data, *inodes_data, blank[BLOCK_SIZE], blockBitmap;
    struct dirent *entry;
    SuperBlock sup_blk;
    Inode root, *inodes_table;
    int num_ent, i, j, k, m, off, blk_idx;
    unsigned char *byte;

    //read in super block and inode sirectory
    read_sup_blk_and_inode(&sup_blk, buf, &root);

    //read in inodes
    inodes_data = malloc(BLOCK_SIZE * sup_blk.s_ino_blocks);
    for (j = 0; j != sup_blk.s_ino_blocks; ++j)
    {
        memset(buf, 0, BLOCK_SIZE);
        block_read(sup_blk.s_ino_start + j, buf);
        memcpy((void *)&inodes_data[BLOCK_SIZE * j], (void *)buf, BLOCK_SIZE);
    }
    inodes_table = (Inode *)inodes_data;

    //see if path exists and destroy otherwise return error
    num_ent = root.file_sz / sizeof(struct dirent); //number of entries
    entry = (struct dirent *)info;
    for (i = 0; i != num_ent; i++)
    { //searches for inode
        if (strcmp(&path[1], entry[i].d_name) == 0)
        {
            break;
        }
    }
    if (i != num_ent)
    { //file exists
        retstat = 1;

        //read in specific inode
        memset(buf, 0, BLOCK_SIZE);
        block_read(sb.inode_start + (unsigned int)((entry.d_ino) / INODE_BLK_NUM), buf);
        memcpy((void *)&ino, (void *)((Inode *)buf)[entry.d_ino / INODE_BLK_NUM], sizeof(Inode));

        //destroy file
        inodes_table[j].num_links = 0; //sets number of file links to 0
        block_write(sup_blk.s_ino_start + (unsigned int)(j / INODES_PER_BLK), &inodes_data[BLOCK_SIZE * (unsigned int)(j / INODES_PER_BLK)]);

        //reads in bitmap to change it
        block_read(sup_blk.inode_bitmap_blocks, buf); //reads in data bitmap
        memcpy(&blockBitmap, buf, BLOCK_SIZE);        //copies bitmap to variable

        for (i = 0; i < INODE_BLK_NUM; i++)
        { //finds blocks and updates bitmap
            if (0 < ino.disk_blks[i] && ino.disk_blks[i] < NUM_BLKS)
            {
                blk_idx = ino.disk_blks[i];
                ino.disk_blks[i] = 2050;
                blockBitmap[i] = '1'; //I wasn't sure how to express the bitmap
            }
        }

        root.num_alloc_blks--;
        sup_blk.s_ino_blocks--;
        root.file_sz -= sizeof(struct dirent);

        //write root and superblock back
        memset(buf, 0, BLOCK_SIZE);
        block_read(sup_blk.s_ino_start, buf);
        memcpy((void *)buf, (void *)&root, sizeof(Inode));
        block_write(sup_blk.s_ino_start, buf);
        memset(buf, 0, BLOCK_SIZE);
        memcpy((void *)buf, (void *)&sup_blk, sizeof(SuperBlock));
        block_write(0, buf);

        //frees variables
        free(entry);
        free(inodes_data);
        free(data);
        return retstat;
    }
    else
    { //file does not exist
        printf("File path (%s) does not exist\n", path);
        return retstat;
    }
    return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int sfs_open(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n",
            path, fi);

    char buf[BLOCK_SIZE], *data, *inodes_data;
    struct dirent *entry;
    SuperBlock sup_blk;
    Inode root, *inodes_table;
    int num_ent, i, j, k, m, off, blk_idx;
    unsigned char *byte;
    
    /*
    memset(buf, 0, BLOCK_SIZE);
    block_read(0, buf);
    memcpy((void *)&sup_blk, (void *)buf, sizeof(SuperBlock));
    memset(buf, 0, BLOCK_SIZE);
    block_read(sup_blk.s_ino_start, buf);
    memcpy((void *)&root, (void *)buf, sizeof(Inode)); */
    read_sup_blk_and_inode(&sup_blk, buf, &root);

    // read all data of root directory
    data = malloc(BLOCK_SIZE * root.num_alloc_blks);
    for (i = 0; i != root.num_alloc_blks; ++i)
    {
        memset(buf, 0, BLOCK_SIZE);
        block_read(root.disk_blks[i], buf);
        memcpy((void *)&data[BLOCK_SIZE * i], (void *)buf, BLOCK_SIZE);
    }

    // find this file in current directory
    num_ent = root.file_sz / sizeof(struct dirent);
    entry = (struct dirent *)data;
    for (i = 0; i != num_ent; ++i)
    {
        if (strcmp(&path[1], entry[i].d_name) == 0)
        {
            break;
        }
    }

    free(data);
    return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int sfs_release(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_release(path=\"%s\", fi=0x%08x)\n",
	  path, fi);

    //remove any temporary data structures (file descriptor)    
    if(fi->fh > 0 ){
        fi->fh = -(fi->fh);
    }else if(fi->fh ==0){
        fi->fh = -1;
    }
    

    return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
int sfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
            path, buf, size, offset, fi);

    char buf[BLOCK_SIZE], *data, *inode_data;
    struct dirent *entry;
    SuperBlock super_block;
    struct Inode root, inode;
    int num_entries, i, j, num, off;

    read_sup_blk_and_inode(&super_block, buf, &root);

    data = malloc(BLOCK_SIZE * root.num_alloc_blks);

    for (i = 0; i != root.num_alloc_blks; ++i)
    {
        memset(buf, 0, BLOCK_SIZE);
        block_read(root.disk_blks[i], buf);
        memcpy((void *)&data[BLOCK_SIZE * i], (void *)buf, BLOCK_SIZE);
    }

    num_entries = root_inodes.file_sz / sizeof(struct dirent);
    entry = (struct dirent *)data;
    for (i = 0; i != num_entries; ++i)
    {
        if (strcmp(&path[1], entry[i].d_name) == 0)
            break;
    }

    if (i == num_entries)
    {
        memset(buf, 0, BLOCK_SIZE);
        block_read(super_block.inode_start + (unsigned int)(entry[i].d_ino / INODES_PER_BLOCK), buf);
        memcpy((void *)&inode, (void *)&((Inode *)buf)[entry[i].d_ino % INODES_PER_BLOCK], sizeof(Inode));

        if (size <= inode.file_sz)
        {
            num = (unsigned int)(size / BLOCK_SIZE);
            for (j = 0; j != num; ++j)
            {
                block_read(inode.disk_blks[j], buf);
                memcpy((void *)&buf[BLOCK_SIZE * j], buf, BLOCK_SIZE);
            }
            retstat size;
        }
        else
        {
            off = inode.file_sz % BLOCK_SIZE;
            num = inode.file_sz / BLOCK_SIZE;

            for (j = 0; j != num; ++j)
            {
                block_read(inode.disk_blks[j], buf);
                memcpy((void *)&buf[BLOCK_SIZE * j], buf, BLOCK_SIZE);
            }
            if (off != 0)
            {
                block_read(inode.disk_blks[j], buf);
                memcpy((void *)&buf[BLOCK_SIZE * j], buf, off);
            }
            retstat = inode.file_sz;
        }
    }
    free(data);
    return retstat;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int sfs_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
            path, buf, size, offset, fi);

    char buf[BLOCK_SIZE], *data, *inode_data;
    struct dirent *entry;
    SuperBlock super_block;
    Inode root_inode, inode;
    int num_entries, i, j, k, l, shift, num, off, block_idx;
    unsigned char *byte;

    read_sup_blk_and_inode(&super_block, buf, &root_inode);

    data = malloc(BLOCK_SIZE * root_inode.num_alloc_blks);

    for (i = 0; i != root_inode.num_alloc_blks; ++i)
    {
        memset(buf, 0, BLOCK_SIZE);
        block_read(root_inode.disk_blks[i], buf);
        memcpy((void *)&data[BLOCK_SIZE * i], (void *)buf, BLOCK_SIZE);
    }

    num_entries = root_inode.file_sz / sizeof(struct dirent);
    entry = (struct dirent *)data;

    for (i = 0; i != num_entries; ++i)
    {
        if (strcmp(&path[1], entry[i].d_name) == 0)
        {
            break;
        }
    }

    if (i != num_entries)
    {
        //get inode structure
        memset(buf, 0, BLOCK_SIZE);
        block_read(super_block.inode_start + (unsigned int)((Inode *)buf)[entry[i].d_ino % INODES_PER_BLK], sizeof(Inode));
        //fill that last using blocks
        off = inode.num_alloc_blks * BLOCK_SIZE - inode.file_sz;
        if (off != 0)
        {
            memcpy(&buf, &data[BLOCK_SIZE * (inode.num_alloc_blks - 1)], BLOCK_SIZE);
            memcpy(&buf[BLOCK_SIZE - off], buf, off);
            block_write(inode.disk_blks[inode.num_alloc_blks - 1], buf);
        }
        //how many new blocks needed
        num = (unsigned int)((size - off) / BLOCK_SIZE);
        num += ((size - off) % BLOCK_SIZE == 0 ? 0 : 1);
        //create num of new blocks

        for (j = 0; j != num; ++j)
        {
            //find a free data block
            memset(buf, 0, BLOCK_SIZE);
            block_read(super_block.inode_bmp_start_idx, buf);
            block_idx = super_block.data_bmp_start_idx;

            for (k = 0; k != BLOCK_SIZE; ++k)
            {
                byte = (unsigned char *)&buf[k];

                for (shift = 0; shift != 8; ++shift)
                {
                    if (((*byte >> shift) & 1) == 0)
                    {
                        *byte |= 1 << shift;
                        break;
                    }
                    block_idx++;
                }
                if (shift != 8)
                {
                    break;
                }
            }
            inode.disk_blks[inode.num_alloc_blks] = block_idx;
            block_write(super_block.inode_bmp_start_idx, buf);

            if (j != num - 1)
            {
                block_write(inode.disk_blks[inode.num_alloc_blks], &buf[off]);
                off += BLOCK_SIZE;
            }
            else
            {
                memset(buf, 0, BLOCK_SIZE);
                memcpy(buf, &buf[off], size - off);
                block_write(inode.disk_blks[inode.num_alloc_blks], buf);
                off = size;
            }
            inode.num_alloc_blks++;
            super_block.inode_blocks++;
        }
        inode.file_sz += size;

        memset(buf, 0, BLOCK_SIZE);
        block_read(super_block.inode_start + (unsigned int)(entry[i].d_ino / INODES_PER_BLK), buf);

        memcpy((void *)&((Inode *)buf)[entry[i].d_ino % INODES_PER_BLK], (void *)&ino, sizeof(Inode));
        block_write(super_block.inode_start + (unsigned int)(entry[i].d_ino / INODES_PER_BLK), buf);

        memset(buf, 0, BLOCK_SIZE);
        memcpy((void *)buf, (void *)&super_block, sizeof(SuperBlock));
        block_write(0, buf);
    }
    free(data);
    return retstat;
}

/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
            path, mode);

    return retstat;
}

/** Remove a directory */
int sfs_rmdir(const char *path)
{
    int retstat = 0;
    log_msg("sfs_rmdir(path=\"%s\")\n",
            path);

    return retstat;
}

/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int sfs_opendir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    char buf[BLOCK_SIZE], *data, *inodes_data;
    struct dirent *entry;
    SuperBlock sup_blk;
    Inode root, *inodes_table;
    int num_ent, i;
    
    read_sup_blk_and_inode(&sup_blk, buf, &root);

    // read all data of root directory
    data = malloc(BLOCK_SIZE * root.num_alloc_blks);
    for (i = 0; i != root.num_alloc_blks; ++i)
    {
        memset(buf, 0, BLOCK_SIZE);
        block_read(root.disk_blks[i], buf);
        memcpy((void *)&data[BLOCK_SIZE * i], (void *)buf, BLOCK_SIZE);
    }

    // find this dir
    num_ent = root.file_sz / sizeof(struct dirent);
    entry = (struct dirent *)data;
    for (i = 0; i != num_ent; ++i)
    {
        if (strcmp(&path[1], entry[i].d_name) == 0)
        {
            log_msg("\nDEBUG: FOUND DIR: sfs_opendir(path=\"%s\", fi=0x%08x)\n", path, fi);
            break;
        }
    }

    free(data);
    return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                struct fuse_file_info *fi)
{
    int retstat = 0;

    SuperBlock sup_blk;
    Inode ino;
    struct dirent *entry;
    char buf[BLOCK_SIZE], *info;
    int num_entries, idx;

    log_msg("\nreaddir begins\n");
    memset(buf, 0, BLOCK_SIZE);
    block_read(0, buf);
    memcpy((void *)&sup_blk, (void *)buf, sizeof(SuperBlock));

    block_read(sup_blk.inode_start, buf);
    memcpy((void *)&ino, (void *)buf, sizeof(Inode));

    info = malloc(ino.num_alloc_blks * BLOCK_SIZE);
    memset(info, 0, ino.num_alloc_blks * BLOCK_SIZE);
    for (idx = 0; idx < ino.num_alloc_blks; idx++)
    {
        block_read(ino.addrs[i], buf);
        memcpy((void *)&info[BLOCK_SIZE * idx], (void *)buf, BLOCK_SIZE);
    }
    num_entries = ino.file_sz / sizeof(struct dirent);
    entry = (struct dirent *)info;

    for (idx = 0; idx < num_entries; idx++)
    {
        if (filler(buf, entry[idx].d_name, NULL, 0) != 0 || idx + 1 == num_entries)
            break;
    }

    free(info);
    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    
    return retstat;
}

struct fuse_operations sfs_oper = {
    .init = sfs_init,
    .destroy = sfs_destroy,

    .getattr = sfs_getattr,
    .create = sfs_create,
    .unlink = sfs_unlink,
    .open = sfs_open,
    .release = sfs_release,
    .read = sfs_read,
    .write = sfs_write,

    .rmdir = sfs_rmdir,
    .mkdir = sfs_mkdir,

    .opendir = sfs_opendir,
    .readdir = sfs_readdir,
    .releasedir = sfs_releasedir};

void sfs_usage()
{
    fprintf(stderr, "usage:  sfs [FUSE and mount options] diskFile mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct sfs_state *sfs_data;

    // sanity checking on the command line
    if ((argc < 3) || (argv[argc - 2][0] == '-') || (argv[argc - 1][0] == '-'))
        sfs_usage();

    sfs_data = malloc(sizeof(struct sfs_state));
    if (sfs_data == NULL)
    {
        perror("main calloc");
        abort();
    }

    // Pull the diskfile and save it in internal data
    sfs_data->diskfile = argv[argc - 2];
    argv[argc - 2] = argv[argc - 1];
    argv[argc - 1] = NULL;
    argc--;

    sfs_data->logfile = log_open();

    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
    fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

    return fuse_stat;
}
