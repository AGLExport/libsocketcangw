#!/bin/sh

clang-format --dry-run src/*.c
clang-format --dry-run src/*.h
clang-format --dry-run include/*.h
