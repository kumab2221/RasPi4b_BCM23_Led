#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

int main()
{
	static char buff[256];
	int fd0;

	if ((fd0 = open("/dev/bcm23_led0", O_RDWR)) < 0) perror("open");

	if (write(fd0, "1", 1) < 0) perror("write");
	if (read(fd0, buff, 1) < 0) perror("read");
	printf("%s\n", buff);
	
	if (close(fd0) != 0) perror("close");

	return 0;
}