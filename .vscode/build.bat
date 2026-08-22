@echo off
cd /d "%~dp0\.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "VCPKG=C:\non-system_programs\vcpkg\installed\x64-windows"
set "LIBVIPS=C:\non-system_programs\dev_libraries\vips-dev-8.18"
cl src\main.c ^
   "src\cleanup interface.c" ^
   "src\growing list.c" ^
   "src\memory buffer.c" ^
   "src\path info.c" ^
   /I"%VCPKG%\include" ^
   /I"%VCPKG%\include\libxml2" ^
   /I"%LIBVIPS%\include" ^
   /I"%LIBVIPS%\include\vips" ^
   /I"%LIBVIPS%\include\glib-2.0" ^
   /I"%LIBVIPS%\lib\glib-2.0\include" ^
   /utf-8 ^
   /Zi ^
   /Od ^
   /W4 ^
   /Fe:bin\app.exe ^
   /Fo:build\ ^
   /Fd:build\app.pdb ^
   /link ^
   /LIBPATH:"%VCPKG%\lib" ^
   libxml2.lib ^
   libcurl.lib ^
   /LIBPATH:"%LIBVIPS%\lib" ^
   libvips.lib ^
   libglib-2.0.lib ^
   libgobject-2.0.lib ^
   libgio-2.0.lib ^
   /PDB:build\app_link.pdb ^
   /INCREMENTAL:NO^
   user32.lib ^
   Shell32.lib ^
   Pathcch.lib ^
   Ole32.lib ^
   Shlwapi.lib ^
   Uuid.lib

rem /Zi     – включает отладочную информацию
rem /Od     – выключает оптимизации
rem /W4     – высокий уровень вспомогательных предупреждений

rem /Fe:    – путь к .exe
rem /Fo:    – путь к объектным файлам
rem /Fd:    – куда компилятор кладёт .pdb – база данных с отладочной информацией

rem /link         – после идут флаги только для компоновщика
rem /PDB:         – куда компоновщик складывает свой .pdb
rem /INCREMENTAL: – отключает появление .ilk файла, ускоряющего пересборку проекта.

rem user32.lib    – подключение библиотеки
rem Shell32.lib   – подключение библиотеки
rem Pathcch.lib   – для absoluteDebugFilePath()
rem Ole32.lib     – подключение библиотеки

