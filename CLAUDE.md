\# Project: MyProject



\## Build and test

Always run `.\\scripts\\check.ps1` after making any change.

Do not consider a task finished until it runs with no errors and shows "100% tests passed."



\## Code style

\- C++20

\- 4-space indentation

\- Use RAII and smart pointers (unique\_ptr, shared\_ptr); avoid raw new/delete

\- Prefer const-correctness everywhere

\- Header guards use #pragma once



\## Structure

\- src/ contains implementation

\- tests/ contains unit tests



\## Notes

\- This is a Windows machine using Visual Studio's compiler (MSVC) through CMake

\- Tests are run with ctest using the Debug configuration (-C Debug)



