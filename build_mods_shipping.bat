if not exist "build" mkdir build
cd build
cmake -G"Visual Studio 17 2022" ..
cmake --build . --config Game__Shipping__Win64
pause