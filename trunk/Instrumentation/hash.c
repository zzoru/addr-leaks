
#include <sys/mman.h>
#include <errno.h>


#define MEM_BEGIN  0x400000
#define MEM_END    0x800000000000
#define MEM_MID    0x400000000000
#define BLOCK_SIZE  0x10000000000

void createShadowMemory()
{
	void* result = mmap(MEM_MID - BLOCK_SIZE, 3 * BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, 0, 0); 
	if (result == MAP_FAILED) {
		printf("Instrumentation MMAP failed. errno=%d\n", errno);
		exit(-1);
	}
}

void* translate(void* addr)
{
	if (addr >=  0x7f0000000000)
	{

		long offset = MEM_END -  (long) addr;
		return MEM_MID - offset;
	}
	else if (addr >= 0x010000000000)
	{
        long offset = addr - ((long)addr & 0xff0000000000);
        return MEM_MID + BLOCK_SIZE + offset;
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

void assertString(char* str)
{
	char* shadowStr = translate(str);
	
	while (*str != 0)
	{
		if (*shadowStr)
		{
			myAbort();
		}
		
		str++;
		shadowStr++;
	}
}


//void* createArgvShadow(int argc, void** argv) //@todo Need to discover how to make it i32 rather than int (there are other solutions of course)
//{
//	for (int i = 0; i < argc; i++)
//	{
//		void* p = argv[i];
//		
//		while (*(int*)p != 0)
//		{
//			p++;
//		}
//		
//		int size = p - argv[i];
//		void* shadow = malloc(size * 8);
//		addInHash(argv[i], shadow);
//		memset(shadow, 0, size * 8);
//	}
//	
//	char** argvShadow = malloc(sizeof(void*) * argc);
//	
//	addInHash(argv, argvShadow);
//	memset(argvShadow, -1, argc * sizeof(void*));
//	return argvShadow;
//}

