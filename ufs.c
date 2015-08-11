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
	int	fd;
	char	*dir, *base;
	struct m_inode *inode;

	log_msg("ufs_creat called, path = %s, mode = %o", (path == NULL ?
				"NULL" : path), mode);
	if (path == NULL) {
		ret = -EINVAL;
		goto out;
	}
	for (fd = 0; fd < OPEN_MAX; fd++)
		if (open_files[fd].f_count == 0)
			break;
	if (fd >= OPEN_MAX) {
		log_msg("ufs_creat: open_files full");
		ret = -ENFILE;
		goto out;
	}
	dir = dirname(path);
	base = basename(path);
	if ((ret = dir2inum(dir, &dirinum)) < 0) {
		log_msg("ufs_creat: dir2inum error for %s", dir);
		goto out;
	}
	if ((ret = rd_inode(dirinum, (struct d_inode *)&inode)) < 0) {
		log_msg("ufs_creat: rd_inode error");
		goto out;
	}
	if (srch_dir_entry(&inode, base, &entry) == 0) {

		if ((ret = add_entry(dirinum, base, &entry)) < 0) {
			log_msg("ufs_creat: add_entry error for "
					"%s in %s", base, dir);
			goto out;
		}
	}
	ret = rd_inode(entry->de_inum,
			(struct d_inode *)open_files[fd].f_inode)
	if (ret < 0) {
		log_msg("ufs_creat: rd_inode error for %u", entry->de_inum);
		goto out;
	}
	open_files[fd].f_mode = open_files[fd].f_inode.i_mode;
	open_files[fd].f_flag = UFS_O_WRONLY;
	open_files[fd].f_count = 1;
	open_files[fd].f_pos = 0;
	fi->fh = fd;
	ret = 0;
out:
	log_msg("ufs_creat return %d", ret);
	return(ret);
}

static int ufs_getattr(const char *path, struct stat *statptr)
{
	int	ret = 0;
	ino_t	inum;
	struct d_inode inode;

	log_msg("ufs_getattr called, path = %s", path);
	if ((ret = path2inum(path, &inum)) == 0) {
		log_msg("ufs_open: path2inum return 0");
		goto out;
	}
	if ((ret = rd_inode(inum, &inode)) < 0) {
		log_msg("ufs_open: rd_inode error");
		goto out;
	}
	statptr->st_mode = conv_fmode(inode.i_mode);
	statptr->st_ino = inum;
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
	int	ret = 0, j;
	ino_t	inum;
	blkcnt_t dnum, znum;
	off_t	i;
	char	blkbuf[BLK_SIZE];
	struct dir_entry *de;
	struct d_inode inode;

	log_msg("ufs_readdir called, path = %s", path);
	if ((ret = path2inum(path, &inum)) == 0) {
		log_msg("ufs_readdir: path2inum return 0");
		ret = -ENOENT;
		goto out;
	}
	if ((ret = rd_inode(inum, &inode) < 0)) {
		log_msg("ufs_readdir: rd_inode error");
		goto out;
	}
	i = 0;
	dnum = 0;
	while (i < inode.i_size) {
		if ((znum = datanum2zonenum(inum, dnum)) == 0) {
			log_msg("readdir: datanum2zonenum return "
					"zero for data %u", dnum);
			ret = -EINVAL;
			goto out;
		}
		if ((ret = rd_zone(znum, &blkbuf, sizeof(blkbuf))) < 0) {
			log_msg("readdir: rd_zone error for data"
					" %u", znum);
			goto out;
		}
		log_msg("inode.i_size == %u", inode.i_size);
		for (de = (struct dir_entry *)blkbuf, j = 0;
				j < ENTRYNUM_PER_BLK && i < inode.i_size; j++) {
			if (de[j].de_inum == 0)
				continue;
			i += sizeof(*de);
			if (filler(buf, de[j].de_name, NULL, 0) != 0) {
				log_msg("readdir: filler error for filling %s",
						de[j].de_name);
				ret =  -ENOMEM;
				goto out;
			}
		}
		dnum++;
	}
out:
	log_msg("ufs_readdir return %d", ret);
	return(ret);
}

struct fuse_operations ufs_oper = {
	.create		= ufs_creat,
	.getattr	= ufs_getattr,
	.open		= ufs_open,
	.readdir	= ufs_readdir,
};

int main(int argc, char *argv[])
{
	init(argv[argc - 1]);
	return(fuse_main(argc - 1, argv, &ufs_oper, NULL));
}
