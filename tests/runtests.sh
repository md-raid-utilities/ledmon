#!/usr/bin/bash

# SPDX-License-Identifier: GPL-3.0-or-later

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

PYTEST=pytest

if ! command -v $PYTEST &> /dev/null
then
	PYTEST=pytest-3
fi

#Put us in a consistent spot
if [ ! -e "./tests" ];then
    echo "We are running from test folder directly."
    cd ..
fi

# Hardware-independent unit tests first, so they run even without test hardware
echo "running block path matching unit test"
./tests/block_match_test || exit 1

echo "running RAID member matching unit test"
./tests/tail_match_test || exit 1

echo "exercising ledctl"
$PYTEST tests --ledctl-binary=src/ledctl/ledctl --slot-filters="$LEDMONTEST_SLOT_FILTER" || exit 1

echo "running library unit test"
./tests/lib_unit_test || exit 1
