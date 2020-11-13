#!/bin/bash
format_valid=0
echo -e "--- Running \033[33mclang-format check\033[0m :clipboard:"
if ! ./extra/validate_format.py; then
    echo "^^^ +++"]
    echo -e "\033[31mclang-format validation failed\033[0m"
    format_valid=1
fi

rm -r build/
mkdir -p build
echo -e "--- Running \033[33mcmake configure\033[0m :video_game:"
if ! cmake -DCMAKE_BUILD_TYPE=Release -S . -B ./build -GNinja; then
    echo "^^^ +++"]
    echo "\033[31mCMake Configure failed\033[0m"
    exit 1
fi

echo -e "--- Running \033[33mcmake build\033[0m :rocket:"
if ! cmake --build ./build --config Release --target all; then
    echo "^^^ +++"]
    echo "\033[31mCMake Build failed\033[0m"
    exit 1
fi

echo -e "+++ Running \033[33mtests\033[0m :camera_with_flash:"
cd build/tests

success=0
for file in ./Tecs-tests ./Tecs-benchmark; do
    "./$file"
    result=$?
    if [ $result -ne 0 ]; then
        echo -e "\033[31mTest failed with response code: $result\033[0m"
        success=$result
    else
        echo -e "\033[32mTest successful\033[0m"
    fi
done

if [ $success -ne 0 ]; then
    echo -e "\033[31mTest failures detected\033[0m"
    exit $success
fi
if [ $format_valid -ne 0 ]; then
    echo -e "\033[31mclang-format errors detected\033[0m"
    echo -e "Run clang-format with ./extra/validate_format.py --fix"
    exit $format_valid
fi

