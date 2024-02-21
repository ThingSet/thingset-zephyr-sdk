#!/bin/bash
#
# Copyright (c) The Libre Solar Project Contributors
# SPDX-License-Identifier: Apache-2.0

DEFAULT_BRANCH="origin/main"

GREEN=$(tput setaf 2)
RED=$(tput setaf 1)
NC=$(tput sgr0)

if [ -x "$(command -v clang-format-diff)" ]; then
CLANG_FORMAT_DIFF="clang-format-diff"
elif [ -x "$(command -v clang-format-diff-15)" ]; then
CLANG_FORMAT_DIFF="clang-format-diff-15"
elif [ -x "$(command -v /usr/share/clang/clang-format-diff.py)" ]; then
CLANG_FORMAT_DIFF="/usr/share/clang/clang-format-diff.py -v"
fi

echo "Style check for diff between branch $DEFAULT_BRANCH"

STYLE_ERROR=0

echo "Checking trailing whitespaces with git diff --check"
GIT_PAGER=cat git diff --check --color=always $DEFAULT_BRANCH
if [[ $? -ne 0 ]]; then
    STYLE_ERROR=1
else
    echo -e "${GREEN}No trailing whitespaces found.${NC}"
fi

echo "Enforcing newlines at EOF"

NEWLINE_FILES_REGEX="(Kconfig|\.(board|conf|c|cpp|dtsi?|h|md|overlay|py|rst|sh|txt|ya?ml)|_defconfig)$"

# https://medium.com/@alexey.inkin/how-to-force-newline-at-end-of-files-and-why-you-should-do-it-fdf76d1d090e
git ls-tree -r HEAD --name-only | grep -E $NEWLINE_FILES_REGEX | xargs -L1 bash -c \
    'test ! "$(tail -c1 "$0")" || (echo -e "$(tput setaf 1)No newline at end of $0$(tput sgr0)" && false)'
if [[ $? -ne 0 ]]; then
    STYLE_ERROR=1
else
    echo -e "${GREEN}All files have newline at EOL.${NC}"
fi

echo "Checking coding style with clang-format"

# clang-format-diff returns 0 even for style differences, so we have to check the length of the
# response
CLANG_FORMAT_DIFF=`git diff $DEFAULT_BRANCH | $CLANG_FORMAT_DIFF -p1 | colordiff`
if [[ "$(echo -n $CLANG_FORMAT_DIFF | wc -c)" -ne 0 ]]; then
    echo "${CLANG_FORMAT_DIFF}"
    STYLE_ERROR=1
else
    echo -e "${GREEN}Coding style valid.${NC}"
fi

exit $STYLE_ERROR
