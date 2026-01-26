@echo off
cd /d "%~dp0\.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"

cl src\main.c ^
   /Zi ^
   /Od ^
   /W4 ^
   /Fe:bin\app.exe ^
   /Fo:build\ ^
   /Fd:build\app.pdb ^
   /link ^
   /PDB:build\app_link.pdb ^
   /INCREMENTAL:NO^
   user32.lib^
   Shell32.lib^
   Ole32.lib

rem /Zi     – включает отладочную информацию
rem /Od     – выключает оптимизации
rem /W4     – высокий уровень вспомогательных предупреждений
rem /Fe:    – путь к .exe
rem /Fo:    – путь к объектным файлам
rem /Fd:    – куда компилятор кладёт .pdb – база данных с отладочной информацией
rem /link   – после идут флаги только для компоновщика
rem /PDB:   – куда компоновщик складывает свой .pdb
rem /INCREMENTAL: – отключает появление .ilk файла, ускоряющего пересборку проекта.
rem user32.lib    – подключение библиотеки
rem Shell32.lib   – подключение библиотеки
rem Ole32.lib     – подключение библиотеки

