#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct List
{
  struct List* next;
  void* ptr;
  void* dest;
};

typedef struct List List;

typedef struct 
{
  void* ptr;
  void* dest;
  List* list;
} Hashitem;

Hashitem hash[65536];

void initHash()
{
	memset(hash, 0, sizeof(hash));
}

void init()
{
  initHash();
}

void addInList(List* list, void* ptr, void* dest)
{
  while (list->next != 0)
  {
    list = list->next;
  }
  
    list->next = malloc(sizeof(List));
    list->next->ptr = ptr;
    list->next->dest = dest;
    list->next->next = 0;
}

void addInHash(void* ptr, void* dest)
{
  int key = (int) ptr & 0xffff;
  if (hash[key].ptr == 0)
  {
    hash[key].ptr = ptr;
    hash[key].dest = dest;
  }
  else if (hash[key].list == 0)
  {
    hash[key].list = malloc(sizeof(List));
    hash[key].list->ptr = ptr;
    hash[key].list->dest = dest;
    hash[key].list->next = 0;
  }
  else
  {
    addInList(hash[key].list, ptr, dest);    
  }
}

void* getFromList(List* list, void* ptr)
{
  do
  {
    if (list->ptr == ptr) return list->dest;
    list = list->next;
  } while (list != 0);
  
  return 0;
}

void* getFromHash(void* ptr)
{
  int key = (int) ptr & 0xffff;
  if (hash[key].ptr == ptr)
  {
    return hash[key].dest;
  }
  else
  {
    return getFromList(hash[key].list, ptr);
  }
}

void* createArgvShadow(int argc, void** argv) //@todo Need to discover how to make it i32 rather than int (there are other solutions of course)
{
	for (int i = 0; i < argc; i++)
	{
		void* p = argv[i];
		
		while (*(int*)p != 0)
		{
			p++;
		}
		
		int size = p - argv[i];
		void* shadow = malloc(size * 8);
		addInHash(argv[i], shadow);
		memset(shadow, 0, size * 8);
	}
	
	char** argvShadow = malloc(sizeof(void*) * argc);
	
	addInHash(argv, argvShadow);
	memset(argvShadow, -1, argc * sizeof(void*));
	return argvShadow;
}

