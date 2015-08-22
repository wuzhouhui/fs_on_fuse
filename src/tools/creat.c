#include <fcntl.h>

/*
 * ./creat <file>
 */
int main(int argc, char *argv[])
{
	int	fd;

	if (argc != 2)
		err_quit("./creat <file>");
	if ((fd = creat(argv[1], 0)) < 0)
		err_sys("creat error");
	return(0);
}
