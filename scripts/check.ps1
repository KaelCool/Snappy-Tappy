cmake --build build
if ($LASTEXITCODE -ne 0) { exit 1 }
ctest --test-dir build --output-on-failure -C Debug