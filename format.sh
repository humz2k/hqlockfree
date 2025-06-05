#!/usr/bin/env bash

clang-format -i *.cpp
clang-format -i include/hqlockfree/*.hpp
clang-format -i src/hqlockfree/*.cpp
clang-format -i tests/*.cpp
clang-format -i perf/*.cpp