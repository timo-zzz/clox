FILES = main.c common.h debug.h debug.c chunk.h chunk.c memory.h memory.c value.h value.c vm.h vm.c compiler.h compiler.c scanner.h scanner.c object.h object.c table.h table.c
COMPILEDHEADERS = chunk.h.gch common.h.gch debug.h.gch memory.h.gch value.h.gch vm.h.gch compiler.h.gch scanner.h.gch object.h.gch table.h.gch

all:
	gcc $(FILES)
	del $(COMPILEDHEADERS)

clean:
	del a.exe
	del $(COMPILEDHEADERS)