
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

unsigned long MAX_MEMORY = 1UL << 47;

void* begin;

void createShadowMemory()
{
	void* begin = mmap(0, MAX_MEMORY / 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, 0, 0); 
	if (begin== MAP_FAILED) {
		printf("Instrumentation MMAP failed. errno=%d\n", errno);
		exit(-1);
	}
}

void* translate(void* addr)
{
	if (addr <= begin)
	{
		 return begin + (long) addr;
	}
	else if (addr >= 0x010000000000)
	{
		return begin - MAX_MEMORY / 2 + (long) addr;
	}
    else // 0x00XXXXXXXXXX
    {
        long offset = addr;
		return  MEM_MID + offset;
    }
}

void myAbort()
{
	printf("An address was leaked\n");
	exit(-1);
}
