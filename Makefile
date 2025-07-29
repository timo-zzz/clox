all:
	gcc main.c common.h debug.h debug.c chunk.h chunk.c memory.h memory.c value.h value.c
	del chunk.h.gch common.h.gch debug.h.gch memory.h.gch value.h.gch

clean:
	del a.exe