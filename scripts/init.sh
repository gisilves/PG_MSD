#!/bin/bash

if [[ $INIT_DONE == true ]]; then
    echo "Environment is already initialized, not running init.sh again"
else

    echo ""
    echo "init.sh script started"

    export SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    export HOME_DIR=$(dirname $SCRIPTS_DIR)
    echo " Repository home is $HOME_DIR"

    export JSON_DIR=$HOME_DIR"/json/"
    export PYTHON_DIR=$HOME_DIR"/python/"
    export SRC_DIR=$HOME_DIR"/src/"
    export BUILD_DIR=$HOME_DIR"/build/"

    export INIT_DONE=true

    echo "init.sh finished"
    echo ""

fi