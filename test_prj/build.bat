@set TARGET=test_jacefs.exe
@set CC=gcc
@echo buding...
@%CC% -c *.c ../other/*.c ../fs_kernel/*.c -I../other -I../fs_kernel 
@%CC% *.o -o %TARGET%
@del *.o 
@echo finish.
%TARGET% > build_log.txt
build_log.txt
@pause
