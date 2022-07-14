#include <syscall.h>

int main(int argc, char *argv[])
{
	int pid = getpid();
	putchar((pid%10) + '0');
	while (1)
	{
		/* code */
	}
	
	return 0;
}