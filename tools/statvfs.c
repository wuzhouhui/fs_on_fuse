#include <sys/statvfs.h>
#include <stdio.h>

int main(void)
{
	struct statvfs buf;

	if (statvfs("mnt/", &buf) < 0)
		return(0);
	printf("block size = %d\n"
			"block free = %d\n"
			"total inodes = %d\n"
			"inode free = %d\n",
			(int)buf.f_bsize,
			(int)buf.f_bfree,
			(int)buf.f_files,
			(int)buf.f_ffree);
	return(0);
}
