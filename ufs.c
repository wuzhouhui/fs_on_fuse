/*
 * ufs file system
 */
#include "ufs.h"

/* errorlog.c needes it */
int log_to_stderr;

/*
 * just a test function
 */
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

static int ufs_open(const char *path, struct fuse_file_info *fi)
{
	log_msg("ufs_open: path = %s", path);
	return(0);
}

struct fuse_operations ufs_oper = {
	.open = ufs_open,
};

int main(int argc, char *argv[])
{
	init(argv[2]);
	return(fuse_main(argc - 1, argv, &ufs_oper, NULL));
}
