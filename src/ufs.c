/*
 * ufs file system
 */
#include "ufs.h"

/* errorlog.c needes it */
int log_to_stderr;

struct ufs_file ufs_open_files[UFS_OPEN_MAX];
/*
 * just a test function
 static void pr_sb(const struct ufs_msuper_block *sb)
 {
 char	buf[UFS_BLK_SIZE];
 struct ufs_dir_entry *ent;
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

 offset = (1 + sb->s_imap_blocks + sb->s_zmap_blocks
 sb->s_inode_blocks) << UFS_BLK_SIZE_SHIFT;
 pread(sb->s_fd, buf, sizeof(buf), offset);
 ent = (struct ufs_dir_entry *)buf;
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

struct ufs_msuper_block sb;

/* statistic the number of available entries */
static unsigned int left_cnt(void *bitmap, unsigned int blk, int total)
{
	char	*c = (char *)bitmap;
	int	n = blk << UFS_BLK_SIZE_SHIFT;
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

	if ((fd = ufs_read_sb(disk_name)) < 0)
		err_quit("fs_init: ufs_read_sb error");
	if (fstat(fd, &stat) < 0)
		err_sys("fs_init: fstat error");
	if ((addr = mmap(0, stat.st_size, PROT_WRITE, MAP_SHARED, fd, 0))
			== MAP_FAILED)
		err_sys("fs_init: mmap error.");
	sb.s_fd	= fd;
	sb.s_addr = addr;
	sb.s_imap = sb.s_addr + UFS_BLK_SIZE;
	sb.s_zmap = sb.s_imap + (sb.s_imap_blocks << UFS_BLK_SIZE_SHIFT);
	/* plus one for super block */
	sb.s_1st_inode_block = sb.s_imap_blocks + sb.s_zmap_blocks + 1;
	sb.s_1st_zone_block = sb.s_1st_inode_block + sb.s_inode_blocks;
	sb.s_inode_left = (unsigned int)left_cnt(sb.s_imap, sb.s_imap_blocks,
			sb.s_inode_blocks * UFS_INUM_PER_BLK);
	sb.s_block_left = (unsigned int)left_cnt(sb.s_zmap, sb.s_zmap_blocks,
			sb.s_zone_blocks);

	log_msg("init returned");
	return(0);
}

static int ufs_creat(const char *path, mode_t mode,
		struct fuse_file_info *fi)
{
	int	fd, ret;
	mode_t	oldmask;
	char	dir[UFS_PATH_LEN + 1], base[UFS_NAME_LEN + 1];
	struct ufs_minode dirinode, inode;
	char	pathcpy[UFS_PATH_LEN + 1];
	struct ufs_dir_entry entry;

	log_msg("ufs_creat called, path = %s, mode = %o",
			(path == NULL ? "NULL" : path), (int)mode);
	if (path == NULL || path[0] == 0) {
		ret = -EINVAL;
		goto out;
	}
	if (strlen(path) >= UFS_PATH_LEN) {
		ret = -ENAMETOOLONG;
		goto out;
	}

	/* find a available entry in ufs_open_files */
	for (fd = 0; fd < UFS_OPEN_MAX; fd++)
		if (ufs_open_files[fd].f_count == 0)
			break;
	if (fd >= UFS_OPEN_MAX) {
		log_msg("ufs_creat: ufs_open_files full");
		ret = -ENFILE;
		goto out;
	}
	memset(&ufs_open_files[fd], 0, sizeof(ufs_open_files[fd]));

	strcpy(pathcpy, path);
	strcpy(dir, dirname(pathcpy));
	strcpy(pathcpy, path);
	strncpy(base, basename(pathcpy), UFS_NAME_LEN);
	base[UFS_NAME_LEN] = 0;

	if ((ret = ufs_dir2i(dir, &dirinode)) < 0) {
		log_msg("ufs_creat: ufs_dir2i error for %s", dir);
		goto out;
	}
	ret = ufs_find_entry(&dirinode, base, &entry);
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
	if ((inode.i_ino = ufs_new_inode()) == 0) {
		log_msg("creat: ufs_new_inode return 0");
		ret = -ENOSPC;
		goto out;
	}
	inode.i_nlink = 1;
	oldmask = umask(0);
	umask(oldmask);
	inode.i_mode = ((mode & 0777) | UFS_IFREG) & (~oldmask);
	inode.i_mtime = inode.i_ctime = time(NULL);
	inode.i_uid = getuid();
	inode.i_gid = getgid();
	if ((ret = ufs_wr_inode(&inode)) < 0) {
		log_msg("ufs_creat: ufs_wr_inode error for inode"
				" %u", (unsigned int)inode.i_ino);
		goto out;
	}

	/*
	 * add an new entry in parent directory
	 */
	entry.de_inum = inode.i_ino;
	strcpy(entry.de_name, base);
	if ((ret = ufs_add_entry(&dirinode, &entry)) < 0) {
		log_msg("ufs_creat: ufs_add_entry error for "
				"%s in %s", base, dir);
		goto out;
	}

	ufs_open_files[fd].f_inode = inode;
	ufs_open_files[fd].f_mode = inode.i_mode;
	ufs_open_files[fd].f_flag = UFS_O_WRONLY;
	ufs_open_files[fd].f_count = 1;
	ufs_open_files[fd].f_pos = 0;
	fi->fh = fd;
	ret = 0;
out:
	if (ret < 0 && inode.i_ino)
		ufs_free_inode(inode.i_ino);
	log_msg("ufs_creat return %d", ret);
	return(ret);
}

static int ufs_getattr(const char *path, struct stat *statptr)
{
	int	ret = 0;
	struct ufs_minode inode;

	log_msg("ufs_getattr called, path = %s", path == NULL ? "NULL" :
			path);
	if ((ret = ufs_path2i(path, &inode)) < 0) {
		log_msg("ufs_getattr: ufs_path2i error");
		goto out;
	}
	statptr->st_mode = ufs_conv_fmode(inode.i_mode);
	statptr->st_ino = inode.i_ino;
	statptr->st_nlink = inode.i_nlink;
	statptr->st_uid = inode.i_uid;
	statptr->st_gid = inode.i_gid;
	statptr->st_size = inode.i_size;
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
	char	pathcpy[UFS_PATH_LEN + 1], dir[UFS_PATH_LEN + 1],
		base[UFS_NAME_LEN + 1];
	struct ufs_minode parinode, dirinode;
	struct ufs_dir_entry entry;

	log_msg("ufs_mkdir called, path = %s, mode = %o",
			(path == NULL ? "NULL" : path), (int)mode);
	if (path == NULL || path[0] == 0) {
		log_msg("ufs_mkdir: path invalid");
		ret = -EINVAL;
		goto out;
	}
	if (strlen(path) > UFS_PATH_LEN) {
		log_msg("ufs_mkdir: path too long");
		ret = -ENAMETOOLONG;
		goto out;
	}

	strcpy(pathcpy, path);
	strcpy(dir, dirname(pathcpy));
	strcpy(pathcpy, path);
	strncpy(base, basename(pathcpy), UFS_NAME_LEN);
	base[UFS_NAME_LEN] = 0;

	if ((ret = ufs_dir2i(dir, &parinode)) < 0) {
		log_msg("ufs_mkdir: ufs_dir2i return %d", ret);
		goto out;
	}
	ret = ufs_find_entry(&parinode, base, &entry);
	if (ret == 0) {
		log_msg("ufs_mkdir: parent diectory doesn't exist");
		ret = -EEXIST;
		goto out;
	}
	if (ret != -ENOENT) {
		log_msg("ufs_mkdir: ufs_find_entry error");
		goto out;
	}

	memset(&dirinode, 0, sizeof(dirinode));
	if ((dirinode.i_ino = ufs_new_inode()) == 0) {
		log_msg("ufs_mkdir: ufs_new_inode return 0");
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
	dirinode.i_ctime = dirinode.i_mtime
		= time(NULL);
	dirinode.i_uid = getuid();
	dirinode.i_gid = getgid();
	entry.de_inum = dirinode.i_ino;
	strcpy(entry.de_name, ".");
	if ((ret = ufs_add_entry(&dirinode, &entry)) < 0) {
		log_msg("ufs_mkdir: ufs_add_entry error for adding .");
		goto out;
	}
	entry.de_inum = parinode.i_ino;
	strcpy(entry.de_name, "..");
	if ((ret = ufs_add_entry(&dirinode, &entry)) < 0) {
		log_msg("ufs_mkdir: ufs_add_entry error for adding ..");
		goto out;
	}
	if ((ret = ufs_wr_inode(&dirinode)) < 0) {
		log_msg("ufs_mkdir: ufs_wr_inode error");
		goto out;
	}

	/*
	 * add an entry of new directory in parent directory
	 */
	entry.de_inum = dirinode.i_ino;
	strcpy(entry.de_name, base);
	if ((ret = ufs_add_entry(&parinode, &entry)) < 0) {
		log_msg("ufs_mkdir: ufs_add_entry error for adding ..");
		goto out;
	}

	/* update parent directory's nlinks */
	parinode.i_nlink++;
	if ((ret = ufs_wr_inode(&parinode)) < 0) {
		log_msg("ufs_mkdir: ufs_wr_inode error for adding ..");
		goto out;
	}
	ret = 0;

out:
	if (ret < 0 && dirinode.i_ino)
		ufs_free_inode(dirinode.i_ino);
	log_msg("ufs_mkdir return %d", (int)ret);
	return(ret);
}

static int ufs_open(const char *path, struct fuse_file_info *fi)
{
	int	ret, oflag, fd;
	struct ufs_minode inode;

	log_msg("ufs_open: path = %s", path == NULL ? "NULL" : path);
	if (path == NULL || path[0] == 0) {
		log_msg("ufs_open: path is null");
		ret = -EINVAL;
		goto out;
	}
	oflag = ufs_conv_oflag(fi->flags);
	if (strlen(path) >= UFS_PATH_LEN) {
		log_msg("ufs_open: path name too long");
		ret = -ENAMETOOLONG;
		goto out;
	}

	for (fd = 0; fd < UFS_OPEN_MAX; fd++)
		if (ufs_open_files[fd].f_count == 0)
			break;
	if (fd >= UFS_OPEN_MAX) {
		log_msg("ufs_open: ufs_open_files is full");
		ret = -ENFILE;
		goto out;
	}

	if ((ret = ufs_path2i(path, &inode)) < 0) {
		log_msg("ufs_open: ufs_path2i error");
		goto out;
	}

	if ((oflag & UFS_O_WRITE) && UFS_ISDIR(inode.i_mode)) {
		log_msg("ufs_open: %s is diectory, but flag has write",
				path);
		ret = -EISDIR;
		goto out;
	}
	if ((oflag & UFS_O_DIR) && !UFS_ISDIR(inode.i_mode)) {
		ret = -ENOTDIR;
		goto out;
	}
	if (oflag & UFS_O_TRUNC) {
		if ((ret = ufs_truncate(&inode)) < 0) {
			log_msg("ufs_open: ufs_truncate error");
			goto out;
		}
		if ((ret = ufs_wr_inode(&inode)) < 0) {
			log_msg("ufs_open: ufs_wr_inode error");
			goto out;
		}
	}

	memcpy(&ufs_open_files[fd].f_inode, &inode, sizeof(inode));
	ufs_open_files[fd].f_mode = inode.i_mode;
	ufs_open_files[fd].f_flag = oflag;
	ufs_open_files[fd].f_count = 1;
	ufs_open_files[fd].f_pos = (oflag & UFS_O_APPEND ? inode.i_size : 0);
	fi->fh = fd;
	ret = 0;
	log_msg("ufs_open: fi->fd = %d", fd);

out:
	log_msg("ufs_open return %d", ret);
	return(ret);
}

static int ufs_read(const char *path, char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
	int	ret = 0;
	off_t	pos, p;
	size_t	s, c;
	struct ufs_minode *iptr;
	char	block[UFS_BLK_SIZE];
	unsigned int znum;

	log_msg("ufs_read called, path = %s, fd = %d",
			(path == NULL ? "NULL" : path), (int)fi->fh);
	if (fi->fh < 0 || fi->fh >= UFS_OPEN_MAX) {
		log_msg("ufs_read: fd out of range");
		ret = -EBADF;
		goto out;
	}
	if (!ufs_open_files[fi->fh].f_count) {
		log_msg("ufs_read: file not opened");
		ret = -EBADF;
		goto out;
	}
	if (!(ufs_open_files[fi->fh].f_flag & UFS_O_READ)) {
		log_msg("ufs_read: file not opend for reading");
		ret = -EBADF;
		goto out;
	}

	iptr = &ufs_open_files[fi->fh].f_inode;
	if (!UFS_ISREG(iptr->i_mode)) {
		log_msg("ufs_read: file is a directory");
		ret = -EISDIR;
		goto out;
	}
	if (!buf || !size) {
		ret = 0;
		goto out;
	}

	s = 0;
	pos = ufs_open_files[fi->fh].f_pos;
	while (s < size && pos < iptr->i_size) {
		znum = ufs_dnum2znum(iptr, pos >> UFS_BLK_SIZE_SHIFT);
		if (!znum)
			break;
		if ((ret = ufs_rd_zone(znum, block, sizeof(block))) < 0) {
			log_msg("ufs_read: ufs_rd_zone error");
			goto out;
		}
		p = pos % UFS_BLK_SIZE;
		c = UFS_BLK_SIZE - p;
		if (c > (size - s))
			c = size - s;
		if (c > iptr->i_size - pos)
			c = iptr->i_size - pos;
		memcpy(buf + s, block + p, c);
		s += c;
		pos += c;
		ufs_open_files[fi->fh].f_pos = pos;
	}

	ret = s;
out:
	log_msg("ufs_read return %d");
	return(ret);
}

static int ufs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi)
{
	int	ret, i;
	struct ufs_minode inode;
	struct ufs_dir_entry *entptr;
	unsigned int dnum, znum;
	off_t	size;
	char	blkbuf[UFS_BLK_SIZE];

	log_msg("ufs_readdir called, path = %s", (path == NULL ? "NULL" :
				path));
	if (path == NULL || path[0] == 0) {
		ret = -EINVAL;
		log_msg("ufs_readdir: path invalid");
		goto out;
	}
	if (strlen(path) >= UFS_PATH_LEN) {
		ret = -ENAMETOOLONG;
		log_msg("ufs_readdir: path name too long");
		goto out;
	}
	if ((ret = ufs_dir2i(path, &inode)) < 0) {
		log_msg("ufs_readdir: ufs_dir2i error for %s", path);
		goto out;
	}

	/* iterates all data of directory */
	dnum = 0;
	size = 0;
	while (size < inode.i_size) {
		if ((znum = ufs_dnum2znum(&inode, dnum++)) == 0) {
			if (size < inode.i_size)
				ret = -EIO;
			goto out;
		}
		if ((ret = ufs_rd_zone(znum, blkbuf, sizeof(blkbuf))) < 0) {
			log_msg("ufs_readdir; ufs_rd_zone error for zone %u",
					(unsigned int)znum);
			goto out;
		}
		entptr = (struct ufs_dir_entry *)blkbuf;
		for (i = 0; i < UFS_ENTRYNUM_PER_BLK; i++) {
			if (entptr[i].de_inum == 0)
				continue;
			size += sizeof(struct ufs_dir_entry);
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
	if (fi->fh < 0 || fi->fh >= UFS_OPEN_MAX) {
		ret = -EBADF;
		log_msg("ufs_release: fd out of range");
		goto out;
	}
	if (ufs_open_files[fi->fh].f_count == 0) {
		ret = -EBADF;
		log_msg("ufs_release: ufs_open_files[%d] not opened",
				(int)fi->fh);
		goto out;
	}
	ufs_open_files[fi->fh].f_count--;
	ret = 0;

out:
	log_msg("ufs_release return %d", ret);
	return(ret);
}

static int ufs_rmdir(const char *path)
{
	int	ret = 0;
	struct ufs_minode inode, parinode;
	char	pathcpy[UFS_PATH_LEN + 1], dir[UFS_PATH_LEN + 1];
	struct ufs_dir_entry ent;

	log_msg("ufs_rmdir called, path = %s", path == NULL ? "NULL" : path);
	if (path == NULL || path[0] == 0) {
		log_msg("ufs_rmdir path invalid");
		ret = -EINVAL;
		goto out;
	}
	if (strlen(path) >= UFS_PATH_LEN) {
		log_msg("ufs_rmdir path too long");
		ret = -ENAMETOOLONG;
		goto out;
	}
	if (strcmp("/", path) == 0) {
		log_msg("ufs_rmdir: root directory can't be removed");
		ret = -EPERM;
		goto out;
	}

	if ((ret = ufs_dir2i(path, &inode)) < 0) {
		log_msg("ufs_rmdir: ufs_dir2i error for %s", path);
		goto out;
	}

	if (!UFS_ISDIR(inode.i_mode)) {
		log_msg("ufs_rmdir: %s is not directory", path);
		ret = -EISDIR;
		goto out;
	}
	if (!ufs_is_dirempty(&inode)) {
		log_msg("ufs_rmdir: %s is not empty", path);
		ret = -ENOTEMPTY;
		goto out;
	}

	strcpy(pathcpy, path);
	strcpy(dir, dirname(pathcpy));
	if ((ret = ufs_dir2i(dir, &parinode)) < 0) {
		log_msg("ufs_rmdir: ufs_dir2i error for %s", dir);
		goto out;
	}
	ent.de_inum = inode.i_ino;
	strcpy(pathcpy, path);
	strncpy(ent.de_name, basename(pathcpy), UFS_NAME_LEN);
	ent.de_name[UFS_NAME_LEN] = 0;
	if ((ret = ufs_rm_entry(&parinode, &ent)) < 0) {
		log_msg("ufs_rmdir: ufs_rm_entry error");
		goto out;
	}
	if ((ret = ufs_wr_inode(&parinode)) < 0) {
		log_msg("ufs_rmdir: ufs_wr_inode error");
		goto out;
	}

	if ((ret = ufs_truncate(&inode)) < 0) {
		log_msg("ufs_rmdir: ufs_truncate error");
		goto out;
	}
	if ((ret = ufs_free_inode(inode.i_ino)) < 0) {
		log_msg("ufs_rmdir: ufs_free_inode error");
		goto out;
	}
	ret = 0;

out:
	log_msg("ufs_rmdir return %d", ret);
	return(ret);
}

static int ufs_unlink(const char *path)
{
	int	ret;
	char	pathcpy[UFS_PATH_LEN + 1], dir[UFS_PATH_LEN + 1],
		name[UFS_NAME_LEN + 1];
	struct ufs_minode inode, parinode;
	struct ufs_dir_entry entry;

	log_msg("ufs_unlink called, path = %s", path == NULL ? "NULL" : path);
	if (path == NULL || path[0] == 0) {
		ret = -EINVAL;
		log_msg("ufs_unlink: path invalid");
		goto out;
	}
	if (strlen(path) >= UFS_PATH_LEN) {
		log_msg("ufs_unlink: path too long");
		ret = -ENAMETOOLONG;
		goto out;
	}

	strcpy(pathcpy, path);
	strcpy(dir, dirname(pathcpy));
	strcpy(pathcpy, path);
	strncpy(name, basename(pathcpy), UFS_NAME_LEN);
	name[UFS_NAME_LEN] = 0;

	if ((ret = ufs_path2i(path, &inode)) < 0) {
		log_msg("ufs_unlink: ufs_path2i error");
		goto out;
	}
	if (UFS_ISDIR(inode.i_mode)) {
		log_msg("ufs_unlink: %s is directory", path);
		ret = -EISDIR;
		goto out;
	}
	if ((ret = ufs_dir2i(dir, &parinode)) < 0) {
		log_msg("ufs_unlink: ufs_dir2i error");
		goto out;
	}

	entry.de_inum = inode.i_ino;
	strcpy(entry.de_name, name);
	if ((ret = ufs_rm_entry(&parinode, &entry)) < 0) {
		log_msg("ufs_unlink: ufs_rm_entry error for %s",
				entry.de_name);
		goto out;
	}
	if ((ret = ufs_wr_inode(&parinode)) < 0) {
		log_msg("ufs_unlink: ufs_wr_inode error");
		goto out;
	}

	if (--inode.i_nlink) {
		if ((ret = ufs_wr_inode(&inode)) < 0) {
			log_msg("ufs_unlink: ufs_wr_inode error");
			goto out;
		}
	} else {
		if ((ret = ufs_truncate(&inode)) < 0) {
			log_msg("ufs_unlink: ufs_truncate error");
			goto out;
		}
		if ((ret = ufs_free_inode(inode.i_ino)) < 0) {
			log_msg("ufs_unlink: ufs_free_inode error");
			goto out;
		}
	}

	ret = 0;
out:
	log_msg("ufs_unlink return %d", ret);
	return(ret);
}

static int ufs_write(const char *path, const char *buf, size_t size,
		off_t offset, struct fuse_file_info *fi)
{
	size_t	s, c;
	unsigned int znum;
	off_t	pos, p;
	int	ret = 0;
	struct ufs_minode *iptr;
	char	block[UFS_BLK_SIZE];

	log_msg("ufs_write called, path = %s", path == NULL ? "NULL" : path);
	if (fi->fh < 0 || fi->fh >= UFS_OPEN_MAX) {
		log_msg("ufs_write: fd %d out out range", (int)fi->fh);
		ret = -EBADF;
		goto out;
	}
	if (ufs_open_files[fi->fh].f_count == 0) {
		log_msg("ufs_write: file not opened");
		ret = -EBADF;
		goto out;
	}
	if ((ufs_open_files[fi->fh].f_flag & UFS_O_WRITE) == 0) {
		log_msg("ufs_write: file not opened for writing");
		ret = -EBADF;
		goto out;
	}
	if (buf == NULL) {
		log_msg("ufs_write: buf is null");
		ret = -EINVAL;
		goto out;
	}
	if (size <= 0) {
		log_msg("ufs_write: size is less than or equals to zero");
		ret = -EINVAL;
		goto out;
	}

	pos = (ufs_open_files[fi->fh].f_flag & UFS_O_APPEND ? 
			ufs_open_files[fi->fh].f_inode.i_size :
			ufs_open_files[fi->fh].f_pos);
	s = 0;
	iptr = &ufs_open_files[fi->fh].f_inode;
	while (s < size) {
		znum = ufs_creat_zone(iptr, pos >> UFS_BLK_SIZE_SHIFT);
		if (znum == 0) {
			if (iptr->i_size >= sb.s_max_size)
				ret = -EFBIG;
			else
				ret = -ENOSPC;
			goto out;
		}
		if ((ret = ufs_rd_zone(znum, block, sizeof(block))) < 0) {
			log_msg("ufs_write: ufs_rd_zone error");
			goto out;
		}
		p = pos % UFS_BLK_SIZE;
		c = UFS_BLK_SIZE - p;
		if (c > (size - s))
			c = size - s;
		memcpy(block + p, buf + s, c);
		if ((ret = ufs_wr_zone(znum, block, sizeof(block))) < 0) {
			log_msg("ufs_write: ufs_wr_zone error");
			goto out;
		}
		s += c;
		pos += c;
		if (pos > iptr->i_size) {
			iptr->i_size = pos;
			if ((ret = ufs_wr_inode(iptr)) < 0) {
				log_msg("ufs_write: ufs_wr_inode error");
				goto out;
			}
		}
	}
	if (!(ufs_open_files[fi->fh].f_flag & UFS_O_APPEND))
		ufs_open_files[fi->fh].f_pos = pos;
	ret = s;
out:
	log_msg("ufs_write return %d", ret);
	return(ret);
}

struct fuse_operations ufs_oper = {
	.create		= ufs_creat,
	.getattr	= ufs_getattr,
	.mkdir		= ufs_mkdir,
	.open		= ufs_open,
	.read		= ufs_read,
	.readdir	= ufs_readdir,
	.release	= ufs_release,
	.rmdir		= ufs_rmdir,
	.unlink		= ufs_unlink,
	.write		= ufs_write,
};

int main(int argc, char *argv[])
{
	init(argv[argc - 1]);
	return(fuse_main(argc - 1, argv, &ufs_oper, NULL));
}
