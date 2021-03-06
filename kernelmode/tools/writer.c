#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>

int main()
{
	void *addr;
	unsigned long *intaddr;
	unsigned long tmp = 0, i, pg = 1024*4;

	addr = (void *)0x8000000000;
	start_timer();
	for(i = 0; i < 250000; i++)
	//for(i = 0; i < 8; i++)
	{
		intaddr = (unsigned long *)(addr+(i*pg));
		*intaddr = (i*i);
	}
	end_timer();

	print_time_diff();

	return 0;
}
