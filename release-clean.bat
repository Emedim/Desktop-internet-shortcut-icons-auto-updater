@echo off
chcp 65001 > nul
echo Cleaning project...

REM Получаем путь к корню проекта (папка, где лежит этот bat)
set ROOT=%~dp0

echo.
echo Cleaning logs...
del /s /q "%ROOT%logs\*" > nul 2>&1

echo Cleaning response...
del /s /q "%ROOT%response\*" > nul 2>&1

echo Cleaning resources/icons...
del /s /q "%ROOT%resources\icons\*" > nul 2>&1

echo Cleaning resources/temp...
del /s /q "%ROOT%resources\temp\*" > nul 2>&1

echo.
echo Done!
pause