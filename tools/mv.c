#include <stdio.h>

/*
 * ./mv <oldpath> <newpath>
 */
int main(int argc, char *argv[])
{
	if (argc != 3)
		err_quit("./mv <oldpath> <newpath>");
	if (rename(argv[1], argv[2]) < 0)
		err_sys("rename error");
	return(0);
}
