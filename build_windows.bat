@echo off
rem Локальна збірка (потрібні CMake + Visual Studio Build Tools або MinGW)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release || goto :err
cmake --build build --config Release --parallel || goto :err
echo.
echo Готово: build\Release\RustClone.exe  (або build\RustClone.exe для MinGW)
goto :eof
:err
echo Помилка збірки
exit /b 1
