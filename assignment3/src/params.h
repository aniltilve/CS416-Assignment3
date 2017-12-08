/*
  Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.

  There are a couple of symbols that need to be #defined before
  #including all the headers.
*/

#ifndef _PARAMS_H_
#define _PARAMS_H_

// The FUSE API has been changed a number of times.  So, our code
// needs to define the version of the API that we assume.  As of this
// writing, the most current API version is 26
#define FUSE_USE_VERSION 26

// need this to get pwrite().  I have to use setvbuf() instead of
// setlinebuf() later in consequence.
#define _XOPEN_SOURCE 500

//size of disk
//#define DISK 16777216 not necessary since we are using blocks as our granularity

#define NUMBLOCKS 2048  //number of blocks in our disk
#define INODE_NUM 32    //number of inodes (can be altered)
#define INODE_SIZE 128  //bytes
#define I_NUM_PER_BLOCK (BLOCK_SIZE/INODE_SIZE) //number of inodes per block
#define INODE_BLOCK_NUM 10 //last index value for inodes (inode index limit)
#define FS_ID 0x0495 //unique filesystem identifier (check for this in init())


//inode structure
struct inode{
    unsigned int inode_mode;    //type of file (read, write, execute)
	unsigned int inode_uid;     //user id
	unsigned int inode_gid;     //group id
	unsigned int inode_atime;   //access
	unsigned int inode_ctime;   //changed
	unsigned int inode_mtime;   //modify
	unsigned int inode_links;   //links to file
    unsigned int inode_size;    //size of file
    unsigned int inode_blocks;  //blocks number
    unsigned int inode_addresses[INODE_BLOCK_NUM];   //Physical block addresses of inodes (0-9)
    unsigned char padding[INODE_SIZE-74];            //used to make a power of 2
};

//superblock structure
struct superBlock{
	unsigned int fsid;                  //filesystem ID
	unsigned int blocks;                //total number of blocks
    unsigned int root;                  //inode number of root directory
	unsigned int inode_start;           //starting index of inode
	unsigned int inode_blocks;          //number of inode blocks
	unsigned int inode_bitmap_start;    //starting index of inode bitmap
	unsigned int inode_bitmap_blocks;   //number of inode bitmap blocks
    unsigned int data_start;            //starting index of data
    unsigned int data_blocks;           //number of data blocks
};

// maintain bbfs state in here
#include <limits.h>
#include <stdio.h>
struct sfs_state {
    FILE *logfile;
    char *diskfile;
};
#define SFS_DATA ((struct sfs_state *) fuse_get_context()->private_data)

#endif
