set CC65PATHBINROOT=F:\X16\cc65-snapshot-win32\bin
set CC65PATHINCROOT=F:\X16\cc65-snapshot-win32\include

REM COMPILER to assembly
rem set OPTIMIZE=
set OPTIMIZE=-O -Oi -Or -Os
%CC65PATHBINROOT%\cc65  %OPTIMIZE% --target cx16 --include-dir ..\include --include-dir %CC65PATHINCROOT% main_xinfo.c 

REM ASSEMBLER to object code
%CC65PATHBINROOT%\ca65 --target cx16 main_xinfo.s

REM LINKER to executable
%CC65PATHBINROOT%\ld65 --target cx16 --obj main_xinfo.o --lib ..\lib\cx16.lib -o main_cx16.out
