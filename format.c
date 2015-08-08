/*
 * ufs formating program.
 *
 * usage:
 *	./format <diskfile>
 */

#include "ufs.h"

int main(int argc, char *argv[])
{
	int	fd, i;
	size_t	size_mb;
	struct stat stat;
	struct d_super_block sb;
	static char buf[BLK_SIZE];

	if (argc != 2)
		err_quit("usage: ./format <diskfile>");
	if ((fd = open(argv[1], O_RDWR)) < 0)
		err_sys("open %s error.", argv[1]);
	if (fstat(fd, &stat) < 0)
		err_sys("stat %s error.", argv[1]);
	if (!S_ISREG(stat.st_mode))
		err_quit("%s is not a regular file", argv[1]);
	size_mb = stat.st_size >> 20;
	if (size_mb < DISK_MIN_SIZE || size_mb > DISK_MAX_SIZE)
		err_quit("the size of %s is inappropriate", argv[1]);

	sb.s_magic = MAGIC;
	if (size_mb <= 10) {
		sb.s_zmap_blocks = 5;
		sb.s_inode_blocks = 256;
	} else if (size_mb >= 21) {
		sb.s_zmap_blocks = 16;
		sb.s_inode_blocks = 1024;
	} else {
		sb.s_zmap_blocks = 11;
		sb.s_inode_blocks = 512;
	}
	sb.s_imap_blocks = 1;
	/* minus one for super block */
	sb.s_zone_blocks = (stat.st_size >> BLK_SIZE_SHIFT) - 1
		- sb.s_zmap_blocks - sb.s_imap_blocks - sb.s_inode_blocks;
	sb.s_max_size = MAX_FILE_SIZE;

	if (lseek(fd, 0, SEEK_SET) < 0)
		err_sys("lseek %s error", argv[1]);
	memcpy(buf, &sb, sizeof(sb));
	if (write(fd, buf, sizeof(buf)) != sizeof(buf))
		err_sys("write %s error", argv[1]);

	memset(&buf, 0, sizeof(buf));
	buf[0] = 1;
	if (write(fd, buf, sizeof(buf)) != sizeof(buf))
		err_sys("write %s error", argv[1]);
	buf[0] = 0;
	for (i = 1; i < sb.s_imap_blocks; i++)
		if (write(fd, buf, sizeof(buf)) != sizeof(buf))
			err_sys("write %s error", argv[1]);

	buf[0] = 1;
	if (write(fd, buf, sizeof(buf)) != sizeof(buf))
		err_sys("write %s error", argv[1]);
	buf[0] = 0;
	for (i = 1; i < sb.s_zmap_blocks; i++)
		if (write(fd, buf, sizeof(buf)) != sizeof(buf))
			err_sys("write %s error", argv[1]);
	close(fd);
	return(0);
}
