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

#define NUM_BLKS 2048  //number of blocks in our disk
#define INODE_NUM 32    //number of inodes (can be altered)
#define INODE_SIZE 128  //bytes
#define INODES_PER_BLK (BLOCK_SIZE/INODE_SIZE) //number of inodes per block
#define INODE_BLOCK_NUM 10 //last index value for inodes (inode index limit)
#define FILE_SYS_ID 0x0495 //unique filesystem identifier (check for this in init())

///////////////////////////////////////////////////////////
// STRUCTS
///////////////////////////////////////////////////////////

//inode structure
typedef struct Inode_t
{
	unsigned int mode,   //type of file (read, write, execute)
		usr_id,    //user id
		grp_id,     //group id
		acc_time,  //access
		chg_time,  //changed
		mod_time,  //modify
		num_links,   //links to file
		file_sz,    //size of file
		num_alloc_blks,  //blocks number
		addrs[INODE_BLOCK_NUM];   //Physical block addresses of inodes (0-9)
	unsigned char padding[INODE_SIZE-74];            //used to make a power of 2
} Inode;

//superblock structure
typdef struct SuperBlock_t 
{
	unsigned int fsid,                  //filesystem ID
		blocks,               //total number of blocks
		root,                //inode number of root directory
		inode_start,           //starting index of inode
		inode_blocks,          //number of inode blocks
		inode_bitmap_start,   //starting index of inode bitmap
		inode_bitmap_blocks,   //number of inode bitmap blocks
		data_start,           //starting index of data
		data_blocks;           //number of data blocks
} SuperBlock;

///////////////////////////////////////////////////////////
// HELPER FUNCTIONS
///////////////////////////////////////////////////////////

void read_sup_blk_and_root_ino(SuperBlock* sup_blk, char[] buf, Inode* root_ino)
{
	memset(buf, 0, BLOCK_SIZE);
	block_read(0, buf);
	memcpy((void*)&sup_blk, (void*)buf, sizeof(SuperBlock));
	memset(buf, 0, BLOCK_SIZE);
	block_read(sup_blk.inode_start, buf);
	memcpy((void*)&root_ino, (void*)buf, sizeof(Inode));
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
    Inode ino;

    memset(buf, 0, BLOCK_SIZE);
    disk_open(SFS_DATA->diskfile);

    if((ret=block_read(0, buf)) <= 0){
        sup_blk.fsid = FILE_SYS_ID;
        sup_blk.blocks = NUM_BLKS;
        sup_blk.root = 0;
        sup_blk.inode_start = (unsigned int)(sizeof(SuperBlock)/BLOCK_SIZE)+1;
        sup_blk.inode_blocks = INODE_NUM/INODES_PER_BLK;
        sup_blk.inode_bitmap_start = sup_blk.inode_start + sup_blk.inode_blocks;
        sup_blk.inode_bitmap_blocks = 1;
        sup_blk.data_start = sup_blk.inode_bitmap_start + sup_blk.inode_bitmap_blocks;
        sup_blk.data_blocks = 0;

        ino.mode = S_IFDIR | S_IRWXU | S_IRWXG;
        ino.usr_id = getuid();
        ino.grp_id = getgid();
        ino.acc_time = time(NULL);
        ino.chg_time = ino.acc_time;
        ino.inode_mtime = ino.acc_time;
        ino.num_links = 1;
        ino.file_sz = 0;
        ino.num_alloc_blks = 0;

        memset((void*)buf, 0, BLOCK_SIZE);
        memcpy((void*)buf, (void*)&sup_blk, sizeof(SuperBlock));
        block_write(0, (void*) buf);
        memset((void*)buf, 0, BLOCK_SIZE);
        memcpy((void*)buf, (void*)&ino, sizeof(Inode));
        block_write(sup_blk.inode_start, (void*)buf);
    }else{
        memcpy((void*)&sup_blk, (void*)buf, sizeof(SuperBlock));
        if(sup_blk.fsid != FILE_SYS_ID){
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

	char buf[BLOCK_SIZE], * info;
	struct dirent *entry;
	SuperBlock sup_blk;
	Inode root_ino, ino;
	int num_ent, idx;
/*
	memset(buf, 0, BLOCK_SIZE);
	block_read(0, buf);
	memcpy((void*)&sup_blk, (void*)buf, sizeof(SuperBlock));
	memset(buf, 0, BLOCK_SIZE);
	block_read(sup_blk.inode_start, buf);
	memcpy((void*)&root_ino, (void*)buf, sizeof(Inode));
	*/
	read_sup_blk_and_root_ino(&sup_blk, buf, &root_ino);

	if(strcmp("/", path) == 0)
	{
		statbuf->st_mode = root_ino.mode;
		statbuf->st_uid = root_ino.usr_id;
		statbuf->st_gid = root_ino.grp_id;
		statbuf->st_rdev = 0;	//not part of our implementation
		statbuf->st_atime = root_ino.acc_time;
		statbuf->st_ctime = root_ino.chg_time;
		statbuf->st_mtime = root_ino.inode_mtime;
		statbuf->st_nlink = root_ino.num_links;
		statbuf->st_blocks = root_ino.num_alloc_blks;
		statbuf->st_size = root_ino.file_sz;
	}
	else
	{
		info = (char*) malloc(BLOCK_SIZE * root_ino.num_alloc_blks);

		for(idx = 0; (unsigned int)idx != root_ino.num_alloc_blks; idx++)
		{
			memset(buf, 0, BLOCK_SIZE);
			block_read(root_ino.addrs[idx], buf);
			memcpy((void*)&info[BLOCK_SIZE * idx], (void*)buf, BLOCK_SIZE);
		}

		num_ent = root_ino.file_sz/sizeof(struct dirent);
		entry = (struct dirent*) info;
		for(idx = 0; idx != num_ent; idx++)
		{
			if(strcmp(&path[1], entry[idx].d_name) == 0)
			{
				//get this inode
				memset(buf, 0, BLOCK_SIZE);
				block_read(sup_blk.inode_start+(unsigned int)((entry[idx].d_ino)/INODES_PER_BLK), buf);
				memcpy((void*)&ino, (void*)&((Inode *)buf)[(entry[idx].d_ino)%INODES_PER_BLK], sizeof(Inode));
				statbuf->st_mode = root_ino.mode;
				statbuf->st_uid = root_ino.usr_id;
				statbuf->st_gid = root_ino.grp_id;
				statbuf->st_rdev = 0;	//not part of our implementation
				statbuf->st_atime = root_ino.acc_time;
				statbuf->st_ctime = root_ino.chg_time;
				statbuf->st_mtime = root_ino.inode_mtime;
				statbuf->st_nlink = root_ino.num_links;
				statbuf->st_blocks = root_ino.num_alloc_blks;
				statbuf->st_size = root_ino.file_sz;
				break;
			}
		}
		
		if(idx == num_ent)
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
    
    
    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", path);

    
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
    Inode root_ino, *inodes_table;
    int num_ent, i, j, k, m, off, blk_idx;
    u8 *byte;

    memset(buf, 0, BLOCK_SIZE);
    block_read(0, buf);
    memcpy((void *)&sup_blk, (void *)buf, sizeof(SuperBlock));
    memset(buf, 0, BLOCK_SIZE);
    block_read(sup_blk.s_ino_start, buf);
    memcpy((void *)&root_ino, (void *)buf, sizeof(Inode));

    // read all data of root directory
    data = malloc(BLOCK_SIZE * root_ino.num_alloc_blks);
    for (i=0; i != root_ino.num_alloc_blks; ++i) {
        memset(buf, 0, BLOCK_SIZE);
        block_read(root_ino.addrs[i], buf);
        memcpy((void *)&data[BLOCK_SIZE*i], (void *)buf, BLOCK_SIZE);
    }

    // find this file in current directory
    num_ent = root_ino.i_size/sizeof(struct dirent);
    entry = (struct dirent *) data;
    for (i=0; i != num_ent; ++i) {
        if (strcmp(&path[1], entry[i].d_name) == 0) {
            break;
        }
    }

    // this file doesn't exist, create
    if (i == num_ent) {
        // read all inodes
        inodes_data = malloc(BLOCK_SIZE * sup_blk.s_ino_blocks);
        for (j=0; j != sup_blk.s_ino_blocks; ++j) {
            memset(buf, 0, BLOCK_SIZE);
            block_read(sup_blk.s_ino_start+j, buf);
            memcpy((void *)&inodes_data[BLOCK_SIZE*j], (void *)buf, BLOCK_SIZE);
        }
        // find first free inode
        inodes_table = (Inode *)inodes_data;
        for (j=1; j != INODE_NUM; ++j) {
            if(inodes_table[j].num_links == 0) {
                break;
            }
        }
        // if there is no more inode
        if (j == INODE_NUM) {
            retstat = -1;
        }
        else {
            // create this inode
            inodes_table[j].num_links = 1;
            inodes_table[j].mode = S_IFREG | S_IRWXU | S_IRWXG;
            inodes_table[j].usr_id = getuid();
            inodes_table[j].grp_id = getgid();
            inodes_table[j].acc_time = time(NULL);
            inodes_table[j].chg_time = inodes_table[j].acc_time;
            inodes_table[j].i_mtime = inodes_table[j].acc_time;
            inodes_table[j].file_sz = 0;
            inodes_table[j].num_alloc_blks = 0;
            block_write(sup_blk.s_ino_start+(unsigned int)(j/INODES_PER_BLK), &inodes_data[BLOCK_SIZE*(unsigned int)(j/INODES_PER_BLK)]);
            // make a new entry
            entry = (struct dirent *) malloc(sizeof(struct dirent));
            entry->d_ino = sup_blk.s_root+j;
            if (strlen(&path[1]) > 256) retstat = -1;
            else memcpy(entry->d_name, &path[1], sizeof(path)-1);
            // need a new block
            if (root_ino.file_sz + sizeof(struct dirent) > BLOCK_SIZE*root_ino.num_alloc_blks) {
                // find first free data block
                memset(buf, 0, BLOCK_SIZE);
                block_read(sup_blk.s_bitmap_start, buf);
                blk_idx = sup_blk.s_data_start;
                for (k=0; k!=BLOCK_SIZE; ++k) {
                    byte = (u8 *) &buf[k];
                    for (m=0; m!=8;++m) {
                        if ( ((*byte >> m) & 1) == 0 ) 
			{
                            *byte |= 1 << m;
                            break;
                        }
                        blk_idx++;
                    }
                    if (m != 8) break;
                }
                root_ino.addrs[root_ino.num_alloc_blks] = blk_idx;
                block_write(sup_blk.s_bitmap_start, buf);
            }

            // write this entry
            off = root_ino.num_alloc_blks * BLOCK_SIZE - root_ino.file_sz;
            if (off != 0) 
	    {
                memcpy(&data[root_ino.file_sz], entry, off);
                memcpy(&buf, &data[BLOCK_SIZE*(root_ino.num_alloc_blks-1)], BLOCK_SIZE);
                block_write(root_ino.addrs[root_ino.num_alloc_blks-1], buf);
            }
            if (off < sizeof(struct dirent)) 
	    {
                memset(buf, 0, BLOCK_SIZE);
                memcpy((void *)buf, &((u8*)entry)[off], sizeof(struct dirent)-off);
                block_write(root_ino.addrs[root_ino.num_alloc_blks], buf);
            }

            root_ino.num_alloc_blks++;
            sup_blk.s_ino_blocks++;
            root_ino.file_sz += sizeof(struct dirent);

            //write root_ino and superblock back
            memset(buf, 0, BLOCK_SIZE);
            block_read(sup_blk.s_ino_start, buf);
            memcpy((void *)buf, (void *)&root_ino, sizeof(Inode));
            block_write(sup_blk.s_ino_start, buf);
            memset(buf, 0, BLOCK_SIZE);
            memcpy((void *)buf, (void *)&sup_blk, sizeof(SuperBlock));
            block_write(0, buf);

            free(entry);
        }

        free(inodes_data);
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
    log_msg("\nsfs_opendir(path=\"%s\", fi=0x%08x)\n",
	  path, fi);
    
    
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
   
    struct superBlock sup_blk;
    Inode ino;
    struct dirent* entry;
    char buf[BLOCK_SIZE], *info;
    int num_entries, idx;

    log_msg("\nreaddir begins\n");
    memset(buf, 0, BLOCK_SIZE);
    block_read(0, buf);
    memcpy((void*)&sup_blk, (void*)buf, sizeof(struct superBlock));

    block_read(sup_blk.inode_start, buf);
    memcpy((void*)&ino, (void*)buf, sizeof(Inode));

    info = malloc(ino.num_alloc_blks * BLOCK_SIZE);
    memset(info, 0, ino.num_alloc_blks * BLOCK_SIZE);
    for(idx = 0; idx < ino.num_alloc_blks; idx++)
    {
        block_read(ino.addrs[i], buf);
        memcpy((void*)&info[BLOCK_SIZE * idx], (void*)buf, BLOCK_SIZE);
    }
    num_entries = ino.file_sz / sizeof(struct dirent);
    entry = (struct dirent*) info;


    for(idx = 0; idx < num_entries; idx++)
    {
        if(filler(buf, entry[idx].d_name, NULL, 0)!=0 || idx + 1 == num_entries)
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
  .releasedir = sfs_releasedir
};

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
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
	sfs_usage();

    sfs_data = malloc(sizeof(struct sfs_state));
    if (sfs_data == NULL) {
	perror("main calloc");
	abort();
    }

    // Pull the diskfile and save it in internal data
    sfs_data->diskfile = argv[argc-2];
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;
    
    sfs_data->logfile = log_open();
    
    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
    fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);
    
    return fuse_stat;
}
