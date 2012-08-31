
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

unsigned long MAX_MEMORY = 1UL << 47;

void* begin;



void createShadowMemory()
{
	static int alreadyCalled = 0;
	
	if (alreadyCalled) return;
	
	begin = mmap(0, MAX_MEMORY / 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, 0, 0); 
	
	if (begin== MAP_FAILED) 
	{
		printf("Instrumentation MMAP failed. errno=%d\n", errno);
		exit(-1);
	}
	
	alreadyCalled = 1;
}

void* translate(void* addr)
{
	void* ret;
	
	if (addr <= begin)
	{
		 ret = begin + (long) addr;
	}
	else 
	{
		ret = begin - MAX_MEMORY / 2 + (long) addr;
	}
	
	return ret;
}

void myAbort()
{
	printf("An address was leaked\n");
	exit(-1);
}

void myAbort2()
{
	printf("An address was leaked\n");
}

void assertZeroString(char* str)
{
	char* shadowPtr = translate(str);

	while (*str != 0)
	{
		if (*shadowPtr != 0) {
			myAbort();
			return;
		}
		
		shadowPtr++;
		str++;
	}
}
