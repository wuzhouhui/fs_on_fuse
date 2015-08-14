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

/* the max and min size of disk file, in megabyte */
#define UFS_DISK_MAX_SIZE	32
#define UFS_DISK_MIN_SIZE	1
/* the size of block */
#define UFS_BLK_SIZE_SHIFT	9
#define UFS_BLK_SIZE	(1 << UFS_BLK_SIZE_SHIFT)
#define UFS_MAGIC		0x7594
/* max length of file name, null terminator excluded */
#define UFS_NAME_LEN        27
/* max length of path, null terminator excluded */
#define UFS_PATH_LEN	1024
/* # inode per block */
#define UFS_INUM_PER_BLK	(UFS_BLK_SIZE / sizeof(struct ufs_dinode))
/* # zone nubmer per block */
#define UFS_ZNUM_PER_BLK	(UFS_BLK_SIZE / sizeof(blkcnt_t))

#define UFS_ENTRYNUM_PER_BLK	(UFS_BLK_SIZE / sizeof(struct ufs_dir_entry))

#define UFS_ROOT_INO	1

#define UFS_ISREG(mode)	(((mode) & (1 << 9)) == 0)
#define UFS_ISDIR(mode)	((mode) & (1 << 9))
#define UFS_IFREG	0
#define UFS_IFDIR	(1 << 9)
/* file opened write only */
#define UFS_O_WRONLY	0x1

/* super block in disk */
struct ufs_dsuper_block {
	/* magic number of filesystem */
	unsigned short s_magic;
	/* # block that inode map used */
	blkcnt_t s_imap_blocks;
	/* # block that zone map used */
	blkcnt_t s_zmap_blocks;
	/* # block that inode used */
	blkcnt_t s_inode_blocks;
	/* # block that zone used */
	blkcnt_t s_zone_blocks;
	/* the max file size that filesystem supports */
	off_t	s_max_size;
};

/* super block in memory */
struct ufs_msuper_block {
	/* magic number of filesystem */
	unsigned short s_magic;
	/* # block that inode map used */
	blkcnt_t s_imap_blocks;
	/* # block that zone map used */
	blkcnt_t s_zmap_blocks;
	/* # block that inode used */
	blkcnt_t s_inode_blocks;
	/* # block that zone used */
	blkcnt_t s_zone_blocks;
	/* the max file size that filesystem supports */
	off_t	s_max_size;

	/* following fields exist in memory */
	/* inode bit map */
	char	*s_imap;
	/* zone bit map */
	char	*s_zmap;
	/* 1st inode block' number in disk */
	blkcnt_t s_1st_inode_block;
	/* 1st zone block' number in disk */
	blkcnt_t s_1st_zone_block;
	/* available inode left. XXX deprecated. */
	ino_t	s_inode_left;
	/* available zone bloc left. XXX deprecated */
	blkcnt_t s_block_left;
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
	off_t	i_size;
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
	blkcnt_t i_zones[8];
};

/* inode in memrory */
struct ufs_minode {
	/* # of links into this inode */
	nlink_t	i_nlink;
	/* file type and permission */
	mode_t	i_mode;
	/* the size of file */
	off_t	i_size;
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
	blkcnt_t i_zones[8];

	ino_t	i_ino;		/* number of inode */
	int	i_refcnt;	/* reference count */
};

/* directory entry */
struct ufs_dir_entry {
	ino_t	de_inum;
	char	de_name[UFS_NAME_LEN + 1];
};

#define MAX_FILE_SIZE	(8259 << 10)

struct ufs_file {
	struct ufs_minode f_inode;
	mode_t	f_mode;	
	int	f_flag;
	int	f_count;
	off_t	f_pos;
};
#define OPEN_MAX	64
extern struct ufs_file ufs_open_files[OPEN_MAX];


int ufs_read_sb(const char *);
int ufs_rm_entry(struct ufs_minode *, const struct ufs_dir_entry *);
int ufs_add_entry(struct ufs_minode *, const struct ufs_dir_entry *);
ino_t ufs_new_inode(void);
int ufs_free_inode(ino_t);
int ufs_rd_inode(ino_t, struct ufs_dinode *);
int ufs_wr_inode(const struct ufs_minode *);
blkcnt_t ufs_new_zone(void);
int ufs_free_zone(blkcnt_t);
int ufs_rd_zone(blkcnt_t, void *, size_t);
int ufs_wr_zone(blkcnt_t, const void *, size_t);
blkcnt_t ufs_inum2bnum(ino_t inum);
blkcnt_t ufs_znum2bnum(blkcnt_t);
blkcnt_t ufs_dnum2znum(struct ufs_minode *, blkcnt_t);
int ufs_rd_blk(blkcnt_t, void *, size_t);
int ufs_wr_blk(blkcnt_t, const void *, size_t);
int ufs_path2i(const char *, struct ufs_minode *);
int ufs_dir2i(const char *, struct ufs_minode *);
int ufs_find_entry(struct ufs_minode *, const char *, struct ufs_dir_entry *);
int ufs_truncate(struct ufs_minode *);
int ufs_is_empty(struct ufs_minode *);
mode_t ufs_conv_fmode(mode_t);

#endif /* end of _UFS_H */
