#!/bin/sh

rm -Rf ./coverage
mkdir -p ./coverage
mkdir -p ./coverage/report

lcov -c --rc branch_coverage=1,geninfo_unexecuted_blocks=1 -d . -o ./coverage/testcoverage.info
genhtml --rc branch_coverage=1 --ignore-errors inconsistent,corrupt,source,unused,gcov -o ./coverage/report -f ./coverage/testcoverage.info --num-spaces 4

