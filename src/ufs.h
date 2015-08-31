#ifndef _UFS_H
#define _UFS_H

#define FUSE_USE_VERSION 26
#include "apue.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#include <fuse.h>
#include <errno.h>		/* for definition of errno */
#include <stdarg.h>		/* ISO C variable arguments */
#include <syslog.h>
#include <libgen.h>

/* turn off logging */
#define log_msg(...)

/* the max and min size of disk file, in megabyte */
#define UFS_DISK_MAX_SIZE	32
#define UFS_DISK_MIN_SIZE	1
/* the size of block */
#define UFS_BLK_SIZE_SHIFT	9
#define UFS_BLK_SIZE	(1 << UFS_BLK_SIZE_SHIFT)
#define UFS_MAGIC		0x7594
/* max length of file name, null terminator excluded */
#define UFS_NAME_LEN        (64 - sizeof(unsigned int) - 1)
/* max length of path, null terminator excluded */
#define UFS_PATH_LEN	512
/* max number of links */
#define UFS_LINK_MAX	128
/* # inode per block */
#define UFS_INUM_PER_BLK	(UFS_BLK_SIZE / sizeof(struct ufs_dinode))
/* # zone nubmer per block */
#define UFS_ZNUM_PER_BLK	(UFS_BLK_SIZE / sizeof(unsigned int))

#define UFS_ENTRYNUM_PER_BLK	(UFS_BLK_SIZE / sizeof(struct ufs_dir_entry))

#define UFS_ROOT_INO	1

#define UFS_ISREG(mode)	(((mode) & (1 << 9)) == 0)
#define UFS_ISDIR(mode)	((mode) & (1 << 9))
#define UFS_IFREG	0
#define UFS_IFDIR	(1 << 9)

/* file open flags */
#define UFS_O_ACCMODE	0x3
#define UFS_O_RDONLY	0x0
#define UFS_O_RDWR	0x1
#define UFS_O_WRONLY	0x2
#define UFS_O_APPEND	0x4
#define UFS_O_DIR	0x8
#define UFS_O_TRUNC	0x10

/* super block in disk */
struct ufs_dsuper_block {
	/* magic number of filesystem */
	unsigned short s_magic;
	/* # block that inode map used */
	unsigned int s_imap_blocks;
	/* # block that zone map used */
	unsigned int s_zmap_blocks;
	/* # block that inode used */
	unsigned int s_inode_blocks;
	/* # block that zone used */
	unsigned int s_zone_blocks;
	/* the max file size that filesystem supports */
	off_t	s_max_size;
};

/* super block in memory */
struct ufs_msuper_block {
	/* magic number of filesystem */
	unsigned short s_magic;
	/* # block that inode map used */
	unsigned int s_imap_blocks;
	/* # block that zone map used */
	unsigned int s_zmap_blocks;
	/* # block that inode used */
	unsigned int s_inode_blocks;
	/* # block that zone used */
	unsigned int s_zone_blocks;
	/* the max file size that filesystem supports */
	off_t	s_max_size;

	/* following fields exist in memory */
	/* inode bit map */
	char	*s_imap;
	/* zone bit map */
	char	*s_zmap;
	/* 1st inode block' number in disk */
	unsigned int s_1st_inode_block;
	/* 1st zone block' number in disk */
	unsigned int s_1st_zone_block;
	/* file descriptor of disk file */
	int	s_fd;
	/* address of disk file in memeory */
	void	*s_addr;
};

/* inode in disk */
struct ufs_dinode {
	/* # of links into this inode */
	nlink_t	i_nlink;
	/* file type and permission */
	mode_t	i_mode;
	/* the size of file */
	unsigned int i_size;
	/* # of disk blocks allocated */
	unsigned int i_blocks;
	/* the last time this file modifed */
	time_t	i_mtime;
	/* the last time this inode changed */
	time_t	i_ctime;
	/* the owner of this file */
	uid_t	i_uid;
	/* the owner'group of this file */
	gid_t	i_gid;
	/*
	 * 0-5: direct block.
	 * 6: indirect block.
	 * 7: double indirect block.
	 */
	unsigned int i_zones[8];
};

/* inode in memrory */
struct ufs_minode {
	/* # of links into this inode */
	nlink_t	i_nlink;
	/* file type and permission */
	mode_t	i_mode;
	/* the size of file */
	unsigned int i_size;
	/* # of disk blocks allocated */
	unsigned int i_blocks;
	/* the last time this file modifed */
	time_t	i_mtime;
	/* the last time this inode changed */
	time_t	i_ctime;
	/* the owner of this file */
	uid_t	i_uid;
	/* the owner'group of this file */
	gid_t	i_gid;
	/*
	 * 0-5: direct block.
	 * 6: indirect block.
	 * 7: double indirect block.
	 */
	unsigned int i_zones[8];

	unsigned int	i_ino;		/* number of inode */
};

/* directory entry */
struct ufs_dir_entry {
	unsigned int	de_inum;
	char	de_name[UFS_NAME_LEN + 1];
};

#define MAX_FILE_SIZE	(8259 << 10)

struct ufs_file {
	struct ufs_minode *f_inode;
	mode_t	f_mode;
	int	f_flag;
	int	f_count;
	off_t	f_pos;
};
#define UFS_OPEN_MAX	64
extern struct ufs_file ufs_open_files[UFS_OPEN_MAX];


int ufs_read_sb(const char *);
int ufs_rm_entry(struct ufs_minode *, const struct ufs_dir_entry *);
int ufs_add_entry(struct ufs_minode *, const struct ufs_dir_entry *);
unsigned int ufs_new_inode(void);
int ufs_free_inode(unsigned int);
int ufs_rd_inode(unsigned int, struct ufs_dinode *);
int ufs_wr_inode(const struct ufs_minode *);
unsigned int ufs_new_zone(void);
int ufs_free_zone(unsigned int);
int ufs_rd_zone(unsigned int, void *, size_t);
int ufs_wr_zone(unsigned int, const void *, size_t);
unsigned int ufs_inum2bnum(unsigned int inum);
unsigned int ufs_znum2bnum(unsigned int);
unsigned int ufs_dnum2znum(struct ufs_minode *, unsigned int);
unsigned int ufs_creat_zone(struct ufs_minode *, unsigned int);
int ufs_rd_blk(unsigned int, void *, size_t);
int ufs_wr_blk(unsigned int, const void *, size_t);
int ufs_path2i(const char *, struct ufs_minode *);
int ufs_dir2i(const char *, struct ufs_minode *);
int ufs_find_entry(struct ufs_minode *, const char *, struct ufs_dir_entry *);
int ufs_truncatei(struct ufs_minode *);
int ufs_is_dirempty(struct ufs_minode *);
mode_t ufs_conv_fmode(mode_t);
int ufs_conv_oflag(int oflag);
int ufs_shrink(struct ufs_minode *, off_t);

#endif /* end of _UFS_H */
