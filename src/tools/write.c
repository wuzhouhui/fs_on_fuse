#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * ./write <file> <offset> <data>
 */

int main(int argc, char *argv[])
{
	int	fd, offset, size;

	if (argc != 4)
		err_quit("./write <file> <offset> <data>");

	offset = atoi(argv[2]);
	size = strlen(argv[3]);
	if ((fd = open(argv[1], O_RDWR)) < 0)
		err_sys("open error");
	if (pwrite(fd, argv[3], size, offset) < 0)
		err_sys("pwrite error");
	return(0);
}
