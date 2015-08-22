#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

/*
 * ./read file offset size
 */
int main(int argc, char *argv[])
{
	int	fd, size, n, offset;
	char	buf[4096];

	if (argc != 4)
		err_quit("./read <file> <offset> <size>");
	if ((fd = open(argv[1], O_RDWR)) < 0)
		err_sys("open error");

	offset = atoi(argv[2]);
	size = atoi(argv[3]);
	if ((n = pread(fd, buf, size, offset)) < 0)
		err_sys("pread error");
	buf[n] = 0;
	printf("%s\n", buf);
	return(0);
}
