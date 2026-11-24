this mod is a pathfinder project that generates a macro from .gdlvl files. 
commands;
cmake -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPATHFINDER_BUILD_CLI=ON
cmake --build build

Follow style guides at /docs/gppstyleguide when writing/editing code.
