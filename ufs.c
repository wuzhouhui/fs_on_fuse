/*
 * ufs file system
 */
#include "ufs.h"

/* errorlog.c needes it */
int log_to_stderr;

struct file open_files[OPEN_MAX];
/*
 * just a test function
 static void pr_sb(const struct m_super_block *sb)
 {
 char	buf[BLK_SIZE];
 struct dir_entry *ent;
 off_t	offset;

 printf("s_magic = %x\n", (int)sb->s_magic);
 printf("s_imap_blocks = %d\n", (int)sb->s_imap_blocks);
 printf("s_zmap_blocks = %d\n", (int)sb->s_zmap_blocks);
 printf("s_inode_blocks = %d\n", (int)sb->s_inode_blocks);
 printf("s_zone_blocks = %d\n", (int)sb->s_zone_blocks);
 printf("s_max_size = %d\n", (int)sb->s_max_size);
 printf("s_imap = %p\n", sb->s_imap);
 printf("s_zmap = %p\n", sb->s_zmap);
 printf("s_1st_inode_block = %d\n", (int)sb->s_1st_inode_block);
 printf("s_1st_zone_block = %d\n", (int)sb->s_1st_zone_block);
 printf("s_inode_left = %d\n", (int)sb->s_inode_left);
 printf("s_block_left = %d\n", (int)sb->s_block_left);
 printf("s_fd = %d\n", (int)sb->s_fd);
 printf("s_addr = %p\n", sb->s_addr);

 offset = (1 + sb->s_imap_blocks + sb->s_zmap_blocks +
 sb->s_inode_blocks) << BLK_SIZE_SHIFT;
 pread(sb->s_fd, buf, sizeof(buf), offset);
 ent = (struct dir_entry *)buf;
 printf("%d, %s\n", (int)ent->de_inum, ent->de_name);
 ent++;
 printf("%d, %s\n", (int)ent->de_inum, ent->de_name);
 return;
 }
 */

static char bit[] = {
	0, 1, 1, 2,
	1, 2, 2, 3,
	1, 2, 2, 3,
	2, 3, 3, 4,
};

struct m_super_block sb;

/* statistic the number of available entries */
static unsigned int left_cnt(void *bitmap, blkcnt_t blk, int total)
{
	char	*c = (char *)bitmap;
	int	n = blk << BLK_SIZE_SHIFT;
	int	one, i;

	for (one = i = 0; i < n; i++) {
		one += bit[c[i] & 0xf];
		one += bit[c[i] >> 4];
	}

	/* 1st bit of bit map reserved, but 1st entry still available */
	return(total - one + 1);
}

static int init(const char *disk_name)
{
	int	fd;
	void	*addr;
	struct stat stat;

	/* print log to syslog */
	log_to_stderr = 0;
	log_open("ufs", LOG_PID, LOG_USER);
	log_msg("init called");

	if ((fd = read_sb(disk_name)) < 0)
		err_quit("fs_init: read_sb error");
	if (fstat(fd, &stat) < 0)
		err_sys("fs_init: fstat error");
	if ((addr = mmap(0, stat.st_size, PROT_WRITE, MAP_SHARED, fd, 0))
			== MAP_FAILED)
		err_sys("fs_init: mmap error.");
	sb.s_fd	= fd;
	sb.s_addr = addr;
	sb.s_imap = sb.s_addr + BLK_SIZE;
	sb.s_zmap = sb.s_imap + (sb.s_imap_blocks << BLK_SIZE_SHIFT);
	/* plus one for super block */
	sb.s_1st_inode_block = sb.s_imap_blocks + sb.s_zmap_blocks + 1;
	sb.s_1st_zone_block = sb.s_1st_inode_block + sb.s_inode_blocks;
	sb.s_inode_left = (ino_t)left_cnt(sb.s_imap, sb.s_imap_blocks,
			sb.s_inode_blocks * INUM_PER_BLK);
	sb.s_block_left = (blkcnt_t)left_cnt(sb.s_zmap, sb.s_zmap_blocks,
			sb.s_zone_blocks);

	log_msg("init returned");
	return(0);
}

static int ufs_creat(const char *path, mode_t mode,
		struct fuse_file_info *fi)
{
	int	fd, ret;
	mode_t	oldmask;
	char	dir[PATH_LEN + 1], base[NAME_LEN + 1];
	struct m_inode dirinode, inode;
	char	pathcpy[PATH_LEN + 1];
	struct dir_entry entry;

	log_msg("ufs_creat called, path = %s, mode = %o",
			(path == NULL ? "NULL" : path), mode);
	if (path == NULL || path[0] == 0) {
		ret = -EINVAL;
		goto out;
	}
	if (strlen(path) >= PATH_LEN) {
		ret = -ENAMETOOLONG;
		goto out;
	}

	/* find a available entry in open_files */
	for (fd = 0; fd < OPEN_MAX; fd++)
		if (open_files[fd].f_count == 0)
			break;
	if (fd >= OPEN_MAX) {
		log_msg("ufs_creat: open_files full");
		ret = -ENFILE;
		goto out;
	}
	memset(&open_files[fd], 0, sizeof(open_files[fd]));

	strcpy(pathcpy, path);
	strcpy(dir, dirname(pathcpy));
	strcpy(pathcpy, path);
	strcpy(base, basename(pathcpy));

	if ((ret = dir2i(dir, &dirinode)) < 0) {
		log_msg("ufs_creat: dir2i error for %s", dir);
		goto out;
	}
	ret = find_entry(&dirinode, base, &entry);
	if (ret == 0) {
		ret = -EEXIST;
		goto out;
	}
	if (ret != -ENOENT)
		goto out;


	/*
	 * allocate a inode and initialize it.
	 */
	memset(&inode, 0, sizeof(inode));
	if ((inode.i_ino = new_inode()) == 0) {
		log_msg("creat: new_inode return 0");
		ret = -ENOSPC;
		goto out;
	}
	inode.i_nlink = 1;
	oldmask = umask(0);
	umask(oldmask);
	inode.i_mode = ((mode & 0777) | UFS_IFREG) & (~oldmask);
	inode.i_atime = inode.i_mtime = inode.i_ctime = time(NULL);
	inode.i_uid = getuid();
	inode.i_gid = getgid();
	if ((ret = wr_inode(&inode)) < 0) {
		log_msg("ufs_creat: wr_inode error for inode"
				" %u", inode.i_ino);
		goto out;
	}

	/*
	 * add an new entry in parent directory
	 */
	entry.de_inum = inode.i_ino;
	strcpy(entry.de_name, base);
	if ((ret = add_entry(&dirinode, &entry)) < 0) {
		log_msg("ufs_creat: add_entry error for "
				"%s in %s", base, dir);
		goto out;
	}

	open_files[fd].f_inode = inode;
	open_files[fd].f_mode = inode.i_mode;
	open_files[fd].f_flag = UFS_O_WRONLY;
	open_files[fd].f_count = 1;
	open_files[fd].f_pos = 0;
	fi->fh = fd;
	ret = 0;
out:
	if (ret < 0 && inode.i_ino)
		free_inode(inode.i_ino);
	log_msg("ufs_creat return %d", ret);
	return(ret);
}

static int ufs_getattr(const char *path, struct stat *statptr)
{
	int	ret = 0;
	struct m_inode inode;

	log_msg("ufs_getattr called, path = %s", path);
	if ((ret = path2i(path, &inode)) < 0) {
		log_msg("ufs_getattr: path2i error");
		goto out;
	}
	statptr->st_mode = conv_fmode(inode.i_mode);
	statptr->st_ino = inode.i_ino;
	statptr->st_nlink = inode.i_nlink;
	statptr->st_uid = inode.i_uid;
	statptr->st_gid = inode.i_gid;
	statptr->st_size = inode.i_size;
	statptr->st_atime = inode.i_atime;
	statptr->st_ctime = inode.i_ctime;
	statptr->st_mtime = inode.i_mtime;
	ret = 0;
out:
	log_msg("ufs_getattr return %d", ret);
	return(ret);
}

static int ufs_mkdir(const char *path, mode_t mode)
{
	int	ret;
	mode_t	oldmask;
	char	pathcpy[PATH_LEN + 1], dir[PATH_LEN + 1],
		base[NAME_LEN + 1];
	struct m_inode parinode, dirinode;
	struct dir_entry entry;

	log_msg("ufs_mkdir called, path = %s, mode = %o",
			(path == NULL ? "NULL" : path), mode);
	if (path == NULL || path[0] == 0) {
		log_msg("ufs_mkdir: path invalid");
		ret = -EINVAL;
		goto out;
	}
	if (strlen(path) > PATH_LEN) {
		log_msg("ufs_mkdir: path too long");
		ret = -ENAMETOOLONG;
		goto out;
	}

	strcpy(pathcpy, path);
	strcpy(dir, dirname(pathcpy));
	strcpy(pathcpy, path);
	strcpy(base, basename(pathcpy));

	if ((ret = dir2i(dir, &parinode)) < 0) {
		log_msg("ufs_mkdir: dir2i return %d", ret);
		goto out;
	}
	ret = find_entry(&parinode, base, &entry);
	if (ret == 0) {
		log_msg("ufs_mkdir: parent diectory doesn't exist");
		ret = -EEXIST;
		goto out;
	}
	if (ret != -ENOENT) {
		log_msg("ufs_mkdir: find_entry error");
		goto out;
	}

	memset(&dirinode, 0, sizeof(dirinode));
	if ((dirinode.i_ino = new_inode()) == 0) {
		log_msg("ufs_mkdir: new_inode return 0");
		ret = -ENOSPC;
		goto out;
	}

	/*
	 * initialize an empty directory, empty directory contains 
	 * . and .. 
	 */
	dirinode.i_nlink = 2;
	oldmask = umask(0);
	umask(oldmask);
	dirinode.i_mode = ((mode & 0777) | UFS_IFDIR) & (~oldmask);
	dirinode.i_atime = dirinode.i_ctime = dirinode.i_mtime
		= time(NULL);
	dirinode.i_uid = getuid();
	dirinode.i_gid = getgid();
	entry.de_inum = dirinode.i_ino;
	strcpy(entry.de_name, ".");
	if ((ret = add_entry(&dirinode, &entry)) < 0) {
		log_msg("ufs_mkdir: add_entry error for adding .");
		goto out;
	}
	entry.de_inum = parinode.i_ino;
	strcpy(entry.de_name, "..");
	if ((ret = add_entry(&dirinode, &entry)) < 0) {
		log_msg("ufs_mkdir: add_entry error for adding ..");
		goto out;
	}
	if ((ret = wr_inode(&dirinode)) < 0) {
		log_msg("ufs_mkdir: wr_inode error");
		goto out;
	}

	/* 
	 * add an entry of new directory in parent directory
	 */
	entry.de_inum = dirinode.i_ino;
	strcpy(entry.de_name, base);
	if ((ret = add_entry(&parinode, &entry)) < 0) {
		log_msg("ufs_mkdir: add_entry error for adding ..");
		goto out;
	}

	/* update parent directory's nlinks */
	parinode.i_nlink++;
	if ((ret = wr_inode(&parinode)) < 0) {
		log_msg("ufs_mkdir: wr_inode error for adding ..");
		goto out;
	}
	ret = 0;

out:
	if (ret < 0 && dirinode.i_ino)
		free_inode(dirinode.i_ino);
	log_msg("ufs_mkdir return %d", ret);
	return(ret);
}

static int ufs_open(const char *path, struct fuse_file_info *fi)
{
	int	ret = 0;

	log_msg("ufs_open: path = %s", path);
	log_msg("ufs_open return %d", ret);
	return(ret);
}

static int ufs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi)
{
	int	ret, i;
	struct m_inode inode;
	struct dir_entry *entptr;
	blkcnt_t dnum, znum;
	off_t	size;
	char	blkbuf[BLK_SIZE];

	log_msg("ufs_readdir called, path = %s", (path == NULL ? "NULL" :
				path));
	if (path == NULL || path[0] == 0) {
		ret = -EINVAL;
		log_msg("ufs_readdir: path invalid");
		goto out;
	}
	if (strlen(path) >= PATH_LEN) {
		ret = -ENAMETOOLONG;
		log_msg("ufs_readdir: path name too long");
		goto out;
	}
	if ((ret = dir2i(path, &inode)) < 0) {
		log_msg("ufs_creat: dir2i error for %s", path);
		goto out;
	}

	/* iterates all data of directory */
	dnum = 0;
	size = 0;
	while (size < inode.i_size) {
		if ((znum = dnum2znum(&inode, dnum++)) == 0) {
			if (size < inode.i_size)
				ret = -EIO;
			goto out;
		}
		if ((ret = rd_zone(znum, blkbuf, sizeof(blkbuf))) < 0) {
			log_msg("ufs_readdir; rd_zone error for zone %u",
					znum);
			goto out;
		}
		entptr = (struct dir_entry *)blkbuf;
		for (i = 0; i < ENTRYNUM_PER_BLK; i++) {
			if (entptr[i].de_inum == 0)
				continue;
			size += sizeof(struct dir_entry);
			log_msg("readdir: entptr[%d].de_name = %s",
					i, entptr[i].de_name);
			if (filler(buf, entptr[i].de_name, NULL, 0)) {
				log_msg("ufs_readdir: filler error");
				ret = -ENOMEM;
				goto out;
			}
		}
	}
	ret = 0;
out:
	log_msg("ufs_readdir return %d", ret);
	return(ret);
}

static int ufs_release(const char *path, struct fuse_file_info *fi)
{
	int	ret;

	log_msg("ufs_release called, path = %s, fd = %d",
			(path == NULL ? "NULL" : path), (int)fi->fh);
	if (fi->fh < 0 || fi->fh >= OPEN_MAX) {
		ret = -EBADF;
		log_msg("ufs_release: fd out of range");
		goto out;
	}
	if (open_files[fi->fh].f_count == 0) {
		ret = -EBADF;
		log_msg("ufs_release: open_files[%d] not opened",
				(int)fi->fh);
		goto out;
	}
	open_files[fi->fh].f_count--;
	ret = 0;

out:
	log_msg("ufs_release return %d", ret);
	return(ret);
}

struct fuse_operations ufs_oper = {
	.create		= ufs_creat,
	.getattr	= ufs_getattr,
	.mkdir		= ufs_mkdir,
	.open		= ufs_open,
	.readdir	= ufs_readdir,
	.release	= ufs_release,
};

int main(int argc, char *argv[])
{
	init(argv[argc - 1]);
	return(fuse_main(argc - 1, argv, &ufs_oper, NULL));
}
