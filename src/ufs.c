/*
 * ufs file system
 */
#include "ufs.h"

/* errorlog.c needes it */
int log_to_stderr;

struct ufs_file ufs_open_files[UFS_OPEN_MAX];

static char bit[] = {
	0, 1, 1, 2,
	1, 2, 2, 3,
	1, 2, 2, 3,
	2, 3, 3, 4,
};

struct ufs_msuper_block sb;
static int ufs_release(const char *, struct fuse_file_info *);

/* statistic the number of available entries */
static unsigned int ufs_left_cnt(void *bitmap, unsigned int blk, int total)
{
	char	*c = (char *)bitmap;
	int	one, i;

	blk <<= UFS_BLK_SIZE_SHIFT;
	for (one = i = 0; i < blk; i++) {
		one += bit[c[i] & 0xf];
		one += bit[(c[i] >> 4) & 0xf];
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

	log_msg("init returned");
	return(0);
}

static int ufs_access(const char *path, int mode)
{
	int	ret = 0, m;
	struct ufs_minode inode;

	log_msg("ufs_access called, path = %s, mode = %o", path == NULL ?
			"NULL" : path, mode);

	if (!path || !path[0]) {
		log_msg("path is null");
		ret = -EINVAL;
		goto out;
	}
	if (!(mode == F_OK || (mode & (R_OK | W_OK | X_OK)))) {
		log_msg("mode invalid");
		ret = -EINVAL;
		goto out;
	}
	if (strlen(path) >= UFS_PATH_LEN) {
		log_msg("path too long");
		ret = -ENAMETOOLONG;
		goto out;
	}
	if ((ret = ufs_path2i(path, &inode)) < 0) {
		log_msg("ufs_path2i error");
		goto out;
	}
	if (mode == F_OK) {
		ret = 0;
		goto out;
	}

	if (getuid() == inode.i_uid)
		m = (inode.i_mode >> 6) & 0x7;
	else if (getgid() == inode.i_gid)
		m = (inode.i_mode >> 3) & 0x7;
	else
		m = inode.i_mode & 0x7;
	ret = ((m & mode) == mode) ? 0 : -EACCES;
out:
	log_msg("ufs_access return %d", ret);
	return(ret);
}

static int ufs_chmod(const char *path, mode_t mode)
{
	int	ret;
	struct ufs_minode inode;

	log_msg("ufs_chmod called, path = %s, mode = %o",
			!path ? "NULL" : path, mode);
	if ((ret = ufs_path2i(path, &inode)) < 0) {
		log_msg("ufs_chmod: ufs_path2i error");
		goto out;
	}
	inode.i_mode &= ~(0777);
	inode.i_mode |= mode & 0777;
	inode.i_ctime = time(NULL);
	if ((ret = ufs_wr_inode(&inode)) < 0) {
		log_msg("ufs_chmod: ufs_wr_inode error");
		goto out;
	}
	ret = 0;
out:
	log_msg("ufs_chmod return %d", ret);
	return(ret);
}

static int ufs_chown(const char *path, uid_t uid, gid_t gid)
{
	int	ret;
	struct ufs_minode inode;

	log_msg("ufs_chown called, path = %s, uid = %d, gid = %d",
			!path ? "NULL" : path, (int)uid, (int)gid);
	if (!path || !path[0]) {
		log_msg("path is null");
		ret = -EINVAL;
		goto out;
	}
	if (strlen(path) >= UFS_PATH_LEN) {
		log_msg("path is too long");
		ret = -ENAMETOOLONG;
		goto out;
	}
	if ((ret = ufs_path2i(path, &inode)) < 0) {
		log_msg("ufs_chown: ufs_path2i error");
		goto out;
	}
	if (uid != -1)
		inode.i_uid = uid;
	if (gid != -1)
		inode.i_gid = gid;
	inode.i_ctime = time(NULL);
	if ((ret = ufs_wr_inode(&inode)) < 0) {
		log_msg("ufs_chown: ufs_wr_inode error");
		goto out;
	}
	ret = 0;
out:
	log_msg("ufs_chown return %d", ret);
	return(ret);
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
		if (!ufs_open_files[fd].f_inode)
			break;
	if (fd >= UFS_OPEN_MAX) {
		log_msg("ufs_creat: ufs_open_files full");
		ret = -ENFILE;
		goto out;
	}

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

	memset(&ufs_open_files[fd], 0, sizeof(ufs_open_files[fd]));
	ufs_open_files[fd].f_inode = malloc(sizeof(struct ufs_minode));
	if (!ufs_open_files[fd].f_inode) {
		log_msg("ufs_creat: malloc error");
		ret = -ENOMEM;
		goto out;
	}
	memcpy(ufs_open_files[fd].f_inode, &inode, sizeof(inode));
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

static int ufs_flush(const char *path, struct fuse_file_info *fi)
{
	log_msg("ufs_flush called, path = %s", path == NULL ? "NULL" :
			path);
	log_msg("ufs_flush return 0");
	return(0);
}

static int ufs_fsync(const char *path, int datasync,
		struct fuse_file_info *fi)
{
	int	ret;

	log_msg("ufs_fsync called, path = %s, datasync = %d",
			(!path) ? "NULL" : path, datasync);
	if (datasync) {
		if (fdatasync(sb.s_fd) < 0) {
			log_msg("ufs_fsync: fdatasync error");
			ret = -errno;
			goto out;
		}
		ret = 0;
		goto out;
	}

	if (fsync(sb.s_fd) < 0) {
		log_msg("ufs_fsync: fsync error");
		ret = -errno;
		goto out;
	}
	ret = 0;

out:
	log_msg("ufs_fsync return %d", ret);
	return(ret);
}

static int ufs_fsyncdir(const char *path, int datasync,
		struct fuse_file_info *fi)
{
	int	ret;

	log_msg("ufs_fsyncdir called, path = %s, datasync = %d",
			(!path) ? "NULL" : path, datasync);
	if (datasync) {
		if (fdatasync(sb.s_fd) < 0) {
			log_msg("ufs_fsyncdir: fdatasync error");
			ret = -errno;
			goto out;
		}
		ret = 0;
		goto out;
	}

	if (fsync(sb.s_fd) < 0) {
		log_msg("ufs_fsyncdir: fsync error");
		ret = -errno;
		goto out;
	}
	ret = 0;

out:
	log_msg("ufs_fsyncdir return %d", ret);
	return(ret);
}

static int ufs_fgetattr(const char *path, struct stat *statptr,
		struct fuse_file_info *fi)
{
	int	ret;
	struct ufs_minode *iptr;

	log_msg("ufs_fgetattr called, path = %s, fd = %d", (!path) ? "NULL"
			: path, (int)fi->fh);
	if (fi->fh < 0 || fi->fh >= UFS_OPEN_MAX) {
		log_msg("ufs_fgetattr: fd out of range");
		ret = -EBADF;
		goto out;
	}
	if (!ufs_open_files[fi->fh].f_inode) {
		log_msg("ufs_fgetattr: file not opened");
		ret = -EBADF;
		goto out;
	}
	iptr = ufs_open_files[fi->fh].f_inode;
	statptr->st_mode = ufs_conv_fmode(iptr->i_mode);
	statptr->st_ino = iptr->i_ino;
	statptr->st_nlink = iptr->i_nlink;
	statptr->st_uid = iptr->i_uid;
	statptr->st_gid = iptr->i_gid;
	statptr->st_size = iptr->i_size;
	statptr->st_ctime = iptr->i_ctime;
	statptr->st_mtime = iptr->i_mtime;
	statptr->st_blksize = UFS_BLK_SIZE;
	statptr->st_blocks = iptr->i_blocks;
	ret = 0;
out:
	log_msg("ufs_fgetattr return %d", ret);
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
	statptr->st_blksize = UFS_BLK_SIZE;
	statptr->st_blocks = inode.i_blocks;
	ret = 0;
out:
	log_msg("ufs_getattr return %d", ret);
	return(ret);
}

static int ufs_link(const char *oldpath, const char *newpath)
{
	int	ret, i;
	struct ufs_minode oldi, newpari;
	char	newname[UFS_NAME_LEN + 1], newpar[UFS_PATH_LEN + 1];
	char	pathcpy[UFS_PATH_LEN + 1];
	struct ufs_dir_entry ent;

	log_msg("ufs_link called, oldpath = %s, newpath = %s",
			!oldpath ? "NULL" : oldpath,
			!newpath ? "NULL" : newpath);
	if (!oldpath || !oldpath[0] || !newpath || !newpath[0]) {
		ret = -EINVAL;
		goto out;
	}
	if ((ret = ufs_path2i(oldpath, &oldi)) < 0) {
		log_msg("ufs_link: ufs_path2i error");
		goto out;
	}
	if (UFS_ISDIR(oldi.i_mode)) {
		ret = -EPERM;
		goto out;
	}
	if (oldi.i_nlink > UFS_LINK_MAX) {
		ret = -EMLINK;
		goto out;
	}
	strcpy(pathcpy, newpath);
	strcpy(newpar, dirname(pathcpy));
	strcpy(pathcpy, newpath);
	strncpy(newname, basename(pathcpy), UFS_NAME_LEN);
	newname[UFS_NAME_LEN] = 0;
	if ((ret = ufs_dir2i(newpar, &newpari)) < 0)
		goto out;
	ret = ufs_find_entry(&newpari, newname, &ent);
	if (!ret) {
		ret = -EEXIST;
		goto out;
	}
	if (ret != -ENOENT)
		goto out;
	ent.de_inum = oldi.i_ino;
	strcpy(ent.de_name, newname);
	if ((ret = ufs_add_entry(&newpari, &ent)) < 0)
		goto out;
	if ((ret = ufs_wr_inode(&newpari)) < 0) {
		log_msg("ufs_link: ufs_wr_inode error");
		goto out;
	}
	oldi.i_nlink++;
	oldi.i_ctime = time(NULL);
	for (i = 0; i < UFS_OPEN_MAX; i++)
		if (ufs_open_files[i].f_inode && oldi.i_ino ==
				ufs_open_files[i].f_inode->i_ino)
			memcpy(ufs_open_files[i].f_inode, &oldi,
					sizeof(oldi));
	if ((ret = ufs_wr_inode(&oldi)) < 0)
		goto out;
	ret = 0;

out:
	log_msg("ufs_link return %d", ret);
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
	parinode.i_ctime = time(NULL);
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

static int ufs_mknod(const char *path, mode_t mode, dev_t dev)
{
	int	ret;
	struct fuse_file_info fi;

	log_msg("ufs_mknod called, path = %s, mode = %d",
			path == NULL ?  "NULL" : path, mode);
	if (!path || !path[0]) {
		log_msg("ufs_mknod: path is null");
		ret = -EINVAL;
		goto out;
	}
	if (strlen(path) >= UFS_PATH_LEN) {
		log_msg("ufs_mknod: path is too long");
		ret = -ENAMETOOLONG;
		goto out;
	}
	if (!S_ISREG(mode)) {
		log_msg("ufs_mknod: file type unsupported");
		ret = -ENOTSUP;
		goto out;
	}
	if ((ret = ufs_creat(path, mode, &fi)) < 0) {
		log_msg("ufs_mknod: ufs_creat error");
		goto out;
	}
	ufs_release(path, &fi);
	ret = 0;

out:
	log_msg("ufs_mknod return %d", ret);
	return(0);
}

static int ufs_open(const char *path, struct fuse_file_info *fi)
{
	int	ret, oflag, fd, acc;
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
		if (!ufs_open_files[fd].f_inode)
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

	if (getuid() == inode.i_uid)
		acc = inode.i_mode >> 6;
	else if (getgid() == inode.i_gid)
		acc = inode.i_mode >> 3;
	else
		acc = inode.i_mode;
	acc &= 0x7;
	if ((oflag & UFS_O_ACCMODE) != UFS_O_RDONLY && !(acc & W_OK)) {
		ret = -EACCES;
		goto out;
	}
	if ((oflag & UFS_O_ACCMODE) != UFS_O_WRONLY && !(acc & R_OK)) {
		ret = -EACCES;
		goto out;
	}

	if ((oflag & UFS_O_ACCMODE) != UFS_O_RDONLY &&
			UFS_ISDIR(inode.i_mode)) {
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
		if ((ret = ufs_truncatei(&inode)) < 0) {
			log_msg("ufs_open: ufs_truncatei error");
			goto out;
		}
		if ((ret = ufs_wr_inode(&inode)) < 0) {
			log_msg("ufs_open: ufs_wr_inode error");
			goto out;
		}
	}

	ufs_open_files[fd].f_inode = malloc(sizeof(struct ufs_minode));
	if (!ufs_open_files[fd].f_inode) {
		log_msg("ufs_open: malloc error");
		ret = -ENOMEM;
		goto out;
	}
	memcpy(ufs_open_files[fd].f_inode, &inode, sizeof(inode));
	ufs_open_files[fd].f_mode = inode.i_mode;
	ufs_open_files[fd].f_flag = oflag;
	ufs_open_files[fd].f_count = 1;
	ufs_open_files[fd].f_pos = (oflag & UFS_O_APPEND) ? inode.i_size : 0;
	fi->fh = fd;
	ret = 0;
	log_msg("ufs_open: fi->fd = %d", fd);

out:
	log_msg("ufs_open return %d", ret);
	return(ret);
}

static int ufs_opendir(const char *path, struct fuse_file_info *fi)
{
	int	ret;
	struct ufs_minode inode;

	log_msg("ufs_opendir called, path = %s", path == NULL ? "NULL" :
			path);
	if (!path || !path[0]) {
		log_msg("ufs_opendir: path is null");
		ret = -EINVAL;
		goto out;
	}
	if (strlen(path) >= UFS_PATH_LEN) {
		log_msg("ufs_opendir: path is too long");
		ret = -ENAMETOOLONG;
		goto out;
	}
	if ((ret = ufs_dir2i(path, &inode)) < 0) {
		log_msg("ufs_opendir: ufs_dir2i error");
		goto out;
	}
	/*
	 * opendir() return nonnull pointer if successed, and fi->fh is
	 * returned as opendir()'s return value.
	 */
	fi->fh = !0;
	ret = 0;

out:
	log_msg("ufs_opendir return %d", ret);
	return(0);
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

	log_msg("ufs_read called, path = %s, fd = %d, size = %d, "
			"offset = %d",
			(path == NULL ? "NULL" : path), (int)fi->fh,
			(int)size, (int)offset);
	if (fi->fh < 0 || fi->fh >= UFS_OPEN_MAX) {
		log_msg("ufs_read: fd out of range");
		ret = -EBADF;
		goto out;
	}
	if (!ufs_open_files[fi->fh].f_inode) {
		log_msg("ufs_read: file not opened");
		ret = -EBADF;
		goto out;
	}
	if ((ufs_open_files[fi->fh].f_flag & UFS_O_ACCMODE) == 
			UFS_O_WRONLY) {
		log_msg("ufs_read: file not opend for reading");
		ret = -EBADF;
		goto out;
	}

	iptr = ufs_open_files[fi->fh].f_inode;
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
	pos = offset;
	while (s < size && pos < iptr->i_size) {
		znum = ufs_dnum2znum(iptr, pos >> UFS_BLK_SIZE_SHIFT);

		if (!znum) {
			/* there is an hole in the file */
			memset(block, 0, sizeof(block));
		} else {
			ret = ufs_rd_zone(znum, block, sizeof(block));
			if (ret < 0) {
				log_msg("ufs_read: ufs_rd_zone error");
				goto out;
			}
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
	log_msg("ufs_read return %d", ret);
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
	struct ufs_minode *iptr;

	log_msg("ufs_release called, path = %s, fd = %d",
			(path == NULL ? "NULL" : path), (int)fi->fh);
	if (fi->fh < 0 || fi->fh >= UFS_OPEN_MAX) {
		ret = -EBADF;
		log_msg("ufs_release: fd out of range");
		goto out;
	}
	if (!ufs_open_files[fi->fh].f_inode) {
		ret = -EBADF;
		log_msg("ufs_release: ufs_open_files[%d] not opened",
				(int)fi->fh);
		goto out;
	}
	ufs_open_files[fi->fh].f_count--;
	if (ufs_open_files[fi->fh].f_count) {
		ret = 0;
		goto out;
	}

	iptr = ufs_open_files[fi->fh].f_inode;
	if (!iptr->i_nlink) {
		ret = ufs_truncatei(iptr);
		if (ret < 0) {
			log_msg("ufs_release: ufs_truncatei"
				" error");
			goto out;
		}
		ret = ufs_free_inode(iptr->i_ino);
		if (ret < 0) {
			log_msg("ufs_release: ufs_free_inode"
				" error");
			goto out;
		}

	}
	free(iptr);
	ufs_open_files[fi->fh].f_inode = NULL;
	ret = 0;

out:
	log_msg("ufs_release return %d", ret);
	return(ret);
}

static int ufs_releasedir(const char *path, struct fuse_file_info *fi)
{
	int	ret;

	log_msg("ufs_releasedir called, path = %s", path == NULL ?
			"NULL" : path);
	if (!fi->fh) {
		ret = -EBADF;
		goto out;
	}
	ret = 0;
out:
	log_msg("ufs_releasedir return %d", ret);
	return(ret);
}

static int ufs_rename(const char *oldpath, const char *newpath)
{
	int	ret = 0;
	struct ufs_minode opi;	/* old path's inode */
	struct ufs_minode npi;	/* new path's inode */
	struct ufs_minode oppi;	/* old path parent's inode */
	struct ufs_minode nppi;	/* new path parent's inode */
	struct ufs_dir_entry ent;
	char	pathcpy[UFS_PATH_LEN + 1], dir[UFS_PATH_LEN + 1];
	char	base[UFS_NAME_LEN + 1];

	log_msg("ufs_rename called, oldpath = %s, newpath = %s",
			(oldpath == NULL ? "NULL" : oldpath),
			(newpath == NULL ? "NULL" : newpath));
	if (!oldpath || !oldpath[0] || !newpath || !newpath[0]) {
		log_msg("ufs_rename: path is null");
		ret = -EINVAL;
		goto out;
	}
	ret = strlen(oldpath);
	if (!strncmp(oldpath, newpath, ret)
			&& newpath[ret] == '/') {
		log_msg("ufs_rename: oldpath is prefix of newpath");
		ret = -EINVAL;
		goto out;
	}
	if (!strcmp(oldpath, "/") || !strcmp(newpath, "/")) {
		ret = -EBUSY;
		goto out;
	}
	if (strlen(oldpath) >= UFS_PATH_LEN || strlen(newpath) >=
			UFS_PATH_LEN) {
		log_msg("path name is too long");
		ret = -ENAMETOOLONG;
		goto out;
	}

	if ((ret = ufs_path2i(oldpath, &opi)) < 0) {
		log_msg("ufs_rename: ufs_path2i error for %s", oldpath);
		goto out;
	}

	if (!strcmp(oldpath, newpath)) {
		ret = 0;
		goto out;
	}

	strcpy(pathcpy, oldpath);
	strcpy(dir, dirname(pathcpy));
	if ((ret = ufs_dir2i(dir, &oppi)) < 0) {
		log_msg("ufs_rename: ufs_dir2i error for %s", dir);
		goto out;
	}

	strcpy(pathcpy, newpath);
	strcpy(dir, dirname(pathcpy));
	if ((ret = ufs_dir2i(dir, &nppi)) < 0) {
		log_msg("ufs_rename: ufs_dir2i error for %s", dir);
		goto out;
	}

	strcpy(pathcpy, newpath);
	strncpy(base, basename(pathcpy), UFS_NAME_LEN);
	base[UFS_NAME_LEN] = 0;
	ret = ufs_find_entry(&nppi, base, &ent);
	if (!ret) {
		/* newpath existed */

		if ((ret = ufs_path2i(newpath, &npi)) < 0) {
			log_msg("ufs_rename: ufs_path2i error for newpath");
			goto out;
		}
		if (UFS_ISDIR(npi.i_mode)) {
			if (!UFS_ISDIR(opi.i_mode)) {
				ret = -EISDIR;
				goto out;
			}
			if (!ufs_is_dirempty(&npi)) {
				ret = -ENOTEMPTY;
				goto out;
			}

			/*
			 * both oldpath and newpath are directory, and
			 * newpath is an empty directory.
			 */
			ent.de_inum = opi.i_ino;
			strcpy(pathcpy, oldpath);
			strncpy(ent.de_name, basename(pathcpy), UFS_NAME_LEN);
			ent.de_name[UFS_NAME_LEN] = 0;
			oppi.i_nlink--;
			oppi.i_ctime = time(NULL);
			if ((ret = ufs_rm_entry(&oppi, &ent)) < 0) {
				log_msg("ufs_rename: ufs_rm_entry error");
				goto out;
			}

			ent.de_inum = npi.i_ino;
			strcpy(pathcpy, newpath);
			strncpy(ent.de_name, basename(pathcpy), UFS_NAME_LEN);
			ent.de_name[UFS_NAME_LEN] = 0;
			if (oppi.i_ino == nppi.i_ino)
				memcpy(&nppi, &oppi, sizeof(oppi));
			nppi.i_nlink--;
			nppi.i_ctime = time(NULL);
			if ((ret = ufs_rm_entry(&nppi, &ent)) < 0) {
				log_msg("ufs_rename: ufs_rm_entry error");
				goto out;
			}
			if ((ret = ufs_truncatei(&npi)) < 0) {
				log_msg("ufs_rename: ufs_truncatei error");
				goto out;
			}
			if ((ret = ufs_free_inode(npi.i_ino)) < 0) {
				log_msg("ufs_rename: ufs_free_inode error");
				goto out;
			}
			ent.de_inum = opi.i_ino;
			nppi.i_nlink++;
			nppi.i_ctime = time(NULL);
			if ((ret = ufs_add_entry(&nppi, &ent)) < 0) {
				log_msg("ufs_rename: ufs_add_entry error");
				goto out;
			}
			goto out;
		}

		/*
		 * newpath is a file.
		 */
		if (UFS_ISDIR(opi.i_mode)) {
			ret = -ENOTDIR;
			goto out;
		}

		/*
		 * both oldpath and newpath is a file.
		 */
		ent.de_inum = opi.i_ino;
		strcpy(pathcpy, oldpath);
		strncpy(ent.de_name, basename(pathcpy), UFS_NAME_LEN);
		ent.de_name[UFS_NAME_LEN] = 0;
		if ((ret = ufs_rm_entry(&oppi, &ent)) < 0) {
			log_msg("ufs_rename: ufs_rm_entry error");
			goto out;
		}
		ent.de_inum = npi.i_ino;
		strcpy(pathcpy, newpath);
		strncpy(ent.de_name, basename(pathcpy), UFS_NAME_LEN);
		ent.de_name[UFS_NAME_LEN] = 0;
		if (oppi.i_ino == nppi.i_ino)
			memcpy(&nppi, &oppi, sizeof(oppi));
		if ((ret = ufs_rm_entry(&nppi, &ent)) < 0) {
			log_msg("ufs_rename: ufs_rm_entry error");
			goto out;
		}
		if (--npi.i_nlink) {
			if ((ret = ufs_wr_inode(&npi)) < 0) {
				log_msg("ufs_rename: ufs_wr_inode error");
				goto out;
			}
		} else {
			if ((ret = ufs_truncatei(&npi)) < 0) {
				log_msg("ufs_rename: ufs_truncatei error");
				goto out;
			}
			if ((ret = ufs_free_inode(npi.i_ino)) < 0) {
				log_msg("ufs_rename: ufs_free_inode error");
				goto out;
			}
		}
		ent.de_inum = opi.i_ino;
		if ((ret = ufs_add_entry(&nppi, &ent)) < 0) {
			log_msg("ufs_rename: ufs_add_entry error");
			goto out;
		}
		goto out;
	}

	if (ret != -ENOENT) {
		log_msg("ufs_rename: ufs_find_entry error");
		goto out;
	}

	/* newpath doesn't existed */
	ent.de_inum = opi.i_ino;
	strcpy(pathcpy, oldpath);
	strncpy(ent.de_name, basename(pathcpy), UFS_NAME_LEN);
	ent.de_name[UFS_NAME_LEN] = 0;
	if (UFS_ISDIR(opi.i_mode)) {
		oppi.i_nlink--;
		oppi.i_ctime = time(NULL);
	}
	if ((ret = ufs_rm_entry(&oppi, &ent)) < 0) {
		log_msg("ufs_rename: ufs_rm_entry error");
		goto out;
	}
	ent.de_inum = opi.i_ino;
	strcpy(pathcpy, newpath);
	strncpy(ent.de_name, basename(pathcpy), UFS_NAME_LEN);
	ent.de_name[UFS_NAME_LEN] = 0;
	if (oppi.i_ino == nppi.i_ino)
		memcpy(&nppi, &oppi, sizeof(oppi));
	if (UFS_ISDIR(opi.i_mode)) {
		nppi.i_nlink++;
		nppi.i_ctime = time(NULL);
	}
	if ((ret = ufs_add_entry(&nppi, &ent)) < 0) {
		log_msg("ufs_rename: ufs_rm_entry error");
		goto out;
	}
	ret = 0;
out:
	log_msg("ufs_rename return %d", ret);
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
	parinode.i_nlink--;
	if ((ret = ufs_wr_inode(&parinode)) < 0) {
		log_msg("ufs_rmdir: ufs_wr_inode error");
		goto out;
	}

	if ((ret = ufs_truncatei(&inode)) < 0) {
		log_msg("ufs_rmdir: ufs_truncatei error");
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

static int ufs_statfs(const char *path, struct statvfs *stat)
{
	int	ret = 0;

	log_msg("ufs_statfs called, path = %s", path == NULL ? "NULL" :
			path);
	if (!path || !path[0]) {
		log_msg("ufs_statfs: path is null");
		ret = -EINVAL;
		goto out;
	}
	if (!stat) {
		log_msg("ufs_statfs: stat is null");
		ret = -EINVAL;
		goto out;
	}
	stat->f_bsize	= stat->f_frsize = UFS_BLK_SIZE;
	stat->f_blocks	= sb.s_zone_blocks;
	stat->f_bfree	= ufs_left_cnt(sb.s_zmap, sb.s_zmap_blocks,
			sb.s_zone_blocks);
	stat->f_bavail	= stat->f_bfree;
	stat->f_files	= sb.s_inode_blocks * UFS_INUM_PER_BLK;
	stat->f_ffree	= ufs_left_cnt(sb.s_imap, sb.s_imap_blocks,
			stat->f_files);
	stat->f_namemax	= UFS_NAME_LEN;
	ret = 0;
out:
	log_msg("ufs_statfs return %d", ret);
	return(ret);
}

static int ufs_truncate(const char *path, off_t length)
{
	int	ret;
	struct ufs_minode inode;
	int	i;

	log_msg("ufs_truncate called, path = %s, length = %d",
			!path ? "NULL" : path, (int)length);
	if (!path || !path[0] || length < 0) {
		ret = -EINVAL;
		goto out;
	}

	if (strlen(path) >= UFS_PATH_LEN) {
		ret = -ENAMETOOLONG;
		goto out;
	}
	if (length > sb.s_max_size) {
		ret = -EFBIG;
		goto out;
	}
	if ((ret = ufs_path2i(path, &inode)) < 0) {
		log_msg("ufs_truncate: ufs_path2i error");
		goto out;
	}
	if (UFS_ISDIR(inode.i_mode)) {
		ret = -EISDIR;
		goto out;
	}
	if ((ret = ufs_shrink(&inode, length)) < 0) {
		log_msg("ufs_truncate: ufs_shrink error");
		goto out;
	}
	inode.i_size = length;
	inode.i_mtime = inode.i_ctime = time(NULL);

	/* the file maybe opened */
	for (i = 0; i < UFS_OPEN_MAX; i++)
		if (ufs_open_files[i].f_inode &&
			ufs_open_files[i].f_inode->i_ino == inode.i_ino)
			memcpy(ufs_open_files[i].f_inode, &inode,
					sizeof(inode));

	if ((ret = ufs_wr_inode(&inode)) < 0) {
		log_msg("ufs_truncate: ufs_wr_inode error");
		goto out;
	}
	ret = 0;
out:
	log_msg("ufs_truncate return %d", ret);
	return(ret);
}

static int ufs_ftruncate(const char *path, off_t length,
		struct fuse_file_info *fi)
{
	int	ret;
	struct ufs_minode *iptr;

	log_msg("ufs_ftruncate called, path = %s, length = %d, fd = %d",
			!path ? "NULL" : path, (int)length,
			(int)fi->fh);
	if (fi->fh < 0 || fi->fh >= UFS_OPEN_MAX) {
		log_msg("ufs_ftruncate: fd out of range");
		ret = -EBADF;
		goto out;
	}
	if (!ufs_open_files[fi->fh].f_inode) {
		log_msg("ufs_ftruncate: fd not opend");
		ret = -EBADF;
		goto out;
	}
	iptr = ufs_open_files[fi->fh].f_inode;
	if (!UFS_ISREG(iptr->i_mode)) {
		log_msg("ufs_ftruncate: fd is not a regular file");
		ret = -EISDIR;
		goto out;
	}
	if (length > sb.s_max_size) {
		ret = -EFBIG;
		goto out;
	}
	if ((ret = ufs_shrink(iptr, length)) < 0) {
		log_msg("ufs_ftruncate: ufs_shrink error");
		goto out;
	}
	iptr->i_size = length;
	iptr->i_mtime = iptr->i_ctime = time(NULL);
	if ((ret = ufs_wr_inode(iptr)) < 0) {
		log_msg("ufs_ftruncate: ufs_wr_inode error");
		goto out;
	}
	ret = 0;
out:
	log_msg("ufs_ftruncate return %d", ret);
	return(ret);
}

static int ufs_unlink(const char *path)
{
	int	ret, i;
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

	inode.i_nlink--;
	inode.i_ctime = time(NULL);
	if ((ret = ufs_wr_inode(&inode)) < 0) {
		log_msg("ufs_unlink: ufs_wr_inode error");
		goto out;
	}
	/* XXX: the file must opened only once */
	for (i = 0; i < UFS_OPEN_MAX; i++)
		if (ufs_open_files[i].f_inode && inode.i_ino ==
				ufs_open_files[i].f_inode->i_ino) {
			memcpy(ufs_open_files[i].f_inode, &inode,
					sizeof(inode));
			break;
		}
	if (!inode.i_nlink && i >= UFS_OPEN_MAX) {
		ret = ufs_truncatei(&inode);
		if (ret < 0) {
			log_msg("ufs_unlink: ufs_truncatei error");
			goto out;
		}
		ret = ufs_free_inode(inode.i_ino);
		if (ret < 0) {
			log_msg("ufs_unlink: ufs_free_inode error");
			goto out;
		}
	}

	ret = 0;
out:
	log_msg("ufs_unlink return %d", ret);
	return(ret);
}

static int ufs_utimens(const char *path, const struct timespec tv[2])
{
	int	ret;
	struct ufs_minode inode;

	log_msg("ufs_utimens called, path = %s", !path ? "NULL" : path);
	if (!path || !path[0]) {
		log_msg("path is null");
		ret = -EINVAL;
		goto out;
	}
	if (strlen(path) >= UFS_PATH_LEN) {
		log_msg("path is too long");
		ret = -ENAMETOOLONG;
		goto out;
	}
	if ((ret = ufs_path2i(path, &inode)) < 0) {
		log_msg("ufs_path2i error");
		goto out;
	}
	
	if (!tv || tv[1].tv_nsec == UTIME_NOW)
		inode.i_mtime = time(NULL);
	else if (tv[1].tv_nsec == UTIME_OMIT)
		goto out;
	else
		inode.i_mtime = tv[1].tv_sec + tv[1].tv_nsec / 1000000000L;
	inode.i_ctime = time(NULL);
	if ((ret = ufs_wr_inode(&inode)) < 0) {
		log_msg("ufs_utimens: ufs_wr_inode error");
		goto out;
	}
	ret = 0;
out:
	log_msg("ufs_utimens return %d", ret);
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

	log_msg("ufs_write called, path = %s, fi->fh = %d, "
            "size = %d, offset = %d",
			(path == NULL ? "NULL" : path), (int)fi->fh,
            (int)size, (int)offset);

	if (fi->fh < 0 || fi->fh >= UFS_OPEN_MAX) {
		log_msg("ufs_write: fd %d out out range", (int)fi->fh);
		ret = -EBADF;
		goto out;
	}
	if (!ufs_open_files[fi->fh].f_inode) {
		log_msg("ufs_write: file not opened");
		ret = -EBADF;
		goto out;
	}

	if ((ufs_open_files[fi->fh].f_flag & UFS_O_ACCMODE)
			== UFS_O_RDONLY) {
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

	iptr = ufs_open_files[fi->fh].f_inode;

	pos = (ufs_open_files[fi->fh].f_flag & UFS_O_APPEND) ?
			ufs_open_files[fi->fh].f_inode->i_size :
			offset;
	s = 0;
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
			iptr->i_ctime = time(NULL);
		}
	}
	iptr->i_mtime = time(NULL);
	if ((ret = ufs_wr_inode(iptr)) < 0) {
		log_msg("ufs_write: ufs_wr_inode error");
		goto out;
	}
	if (!(ufs_open_files[fi->fh].f_flag & UFS_O_APPEND))
		ufs_open_files[fi->fh].f_pos = pos;
	ret = s;
out:
	log_msg("ufs_write return %d", ret);
	return(ret);
}

struct fuse_operations ufs_oper = {
	.access		= ufs_access,
	.chmod		= ufs_chmod,
	.chown		= ufs_chown,
	.create		= ufs_creat,
	.flush		= ufs_flush,
	.fsync		= ufs_fsync,
	.fsyncdir	= ufs_fsyncdir,
	.fgetattr	= ufs_fgetattr,
	.getattr	= ufs_getattr,
	.link		= ufs_link,
	.mkdir		= ufs_mkdir,
	.mknod		= ufs_mknod,
	.open		= ufs_open,
	.opendir	= ufs_opendir,
	.read		= ufs_read,
	.readdir	= ufs_readdir,
	.release	= ufs_release,
	.releasedir	= ufs_releasedir,
	.rename		= ufs_rename,
	.rmdir		= ufs_rmdir,
	.statfs		= ufs_statfs,
	.truncate	= ufs_truncate,
	.ftruncate	= ufs_ftruncate,
	.unlink		= ufs_unlink,
	.utimens	= ufs_utimens,
	.write		= ufs_write,
};

int main(int argc, char *argv[])
{
	init(argv[argc - 1]);
	return(fuse_main(argc - 1, argv, &ufs_oper, NULL));
}
