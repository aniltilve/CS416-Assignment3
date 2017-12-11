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
    struct superBlock sup_blk;
    struct inode ino;

    memset(buf, 0, BLOCK_SIZE);
    disk_open(SFS_DATA->diskfile);

    if((ret=block_read(0, buf)) <= 0){
        sup_blk.fsid = FS_ID;
        sup_blk.blocks = NUMBLOCKS;
        sup_blk.root = 0;
        sup_blk.inode_start = (unsigned int)(sizeof(struct superBlock)/BLOCK_SIZE)+1;
        sup_blk.inode_blocks = INODE_NUM/I_NUM_PER_BLOCK;
        sup_blk.inode_bitmap_start = sup_blk.inode_start + sup_blk.inode_blocks;
        sup_blk.inode_bitmap_blocks = 1;
        sup_blk.data_start = sup_blk.inode_bitmap_start + sup_blk.inode_bitmap_blocks;
        sup_blk.data_blocks = 0;

        ino.inode_mode = S_IFDIR | S_IRWXU | S_IRWXG;
        ino.inode_uid = getuid();
        ino.inode_gid = getgid();
        ino.inode_atime = time(NULL);
        ino.inode_ctime = ino.inode_atime;
        ino.inode_mtime = ino.inode_atime;
        ino.inode_links = 1;
        ino.inode_size = 0;
        ino.inode_blocks = 0;

        memset((void*)buf, 0, BLOCK_SIZE);
        memcpy((void*)buf, (void*)&sup_blk, sizeof(struct superBlock));
        block_write(0, (void*) buf);
        memset((void*)buf, 0, BLOCK_SIZE);
        memcpy((void*)buf, (void*)&ino, sizeof(struct inode));
        block_write(sup_blk.inode_start, (void*)buf);
    }else{
        memcpy((void*)&sup_blk, (void*)buf, sizeof(struct superBlock));
        if(sup_blk.fsid != FS_ID){
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

	char buf[BLOCK_SIZE];
	char *info;
	struct dirent *entry;
	struct superBlock sup_blk;
	struct inode root_ino, ino;
	int num_entries, idx;

	memset(buf, 0, BLOCK_SIZE);
	block_read(0, buf);
	memcpy((void*)&sup_blk, (void*)buf, sizeof(struct superBlock));
	memset(buf, 0, BLOCK_SIZE);
	block_read(sup_blk.inode_start, buf);
	memcpy((void*)&root_ino, (void*)buf, sizeof(struct inode));

	if(strcmp("/", path) == 0)
	{
		statbuf->st_mode = root_ino.inode_mode;
		statbuf->st_uid = root_ino.inode_uid;
		statbuf->st_gid = root_ino.inode_gid;
		statbuf->st_rdev = 0;	//not part of our implementation
		statbuf->st_atime = root_ino.inode_atime;
		statbuf->st_ctime = root_ino.inode_ctime;
		statbuf->st_mtime = root_ino.inode_mtime;
		statbuf->st_nlink = root_ino.inode_links;
		statbuf->st_blocks = root_ino.inode_blocks;
		statbuf->st_size = root_ino.inode_size;
	}
	else
	{
		info = (char*) malloc(BLOCK_SIZE * root_ino.inode_blocks);

		for(idx = 0; (unsigned int)idx != root_ino.inode_blocks; idx++)
		{
			memset(buf, 0, BLOCK_SIZE);
			block_read(root_ino.inode_addresses[idx], buf);
			memcpy((void*)&info[BLOCK_SIZE * idx], (void*)buf, BLOCK_SIZE);
		}

		num_entries = root_ino.inode_size/sizeof(struct dirent);
		entry = (struct dirent*) info;
		for(idx = 0; idx != num_entries; idx++)
		{
			if(strcmp(&path[1], entry[idx].d_name) == 0)
			{
				//get this inode
				memset(buf, 0, BLOCK_SIZE);
				block_read(sup_blk.inode_start+(unsigned int)((entry[idx].d_ino)/I_NUM_PER_BLOCK), buf);
				memcpy((void*)&ino, (void*)&((struct inode *)buf)[(entry[idx].d_ino)%I_NUM_PER_BLOCK], sizeof(struct inode));
				statbuf->st_mode = root_ino.inode_mode;
				statbuf->st_uid = root_ino.inode_uid;
				statbuf->st_gid = root_ino.inode_gid;
				statbuf->st_rdev = 0;	//not part of our implementation
				statbuf->st_atime = root_ino.inode_atime;
				statbuf->st_ctime = root_ino.inode_ctime;
				statbuf->st_mtime = root_ino.inode_mtime;
				statbuf->st_nlink = root_ino.inode_links;
				statbuf->st_blocks = root_ino.inode_blocks;
				statbuf->st_size = root_ino.inode_size;
				break;
			}
		}
		
		if(idx == num_entries)
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
    struct inode ino;
    struct dirent* entry;
    char buf[BLOCK_SIZE], *info;
    int num_entries, idx;

    log_msg("\nreaddir begins\n");
    memset(buf, 0, BLOCK_SIZE);
    block_read(0, buf);
    memcpy((void*)&sup_blk, (void*)buf, sizeof(struct superBlock));

    block_read(sup_blk.inode_start, buf);
    memcpy((void*)&ino, (void*)buf, sizeof(struct inode));

    info = malloc(ino.inode_blocks * BLOCK_SIZE);
    memset(info, 0, ino.inode_blocks * BLOCK_SIZE);
    for(idx = 0; idx < ino.inode_blocks; idx++)
    {
        block_read(ino.inode_addresses[i], buf);
        memcpy((void*)&info[BLOCK_SIZE * idx], (void*)buf, BLOCK_SIZE);
    }
    num_entries = ino.inode_size / sizeof(struct dirent);
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
