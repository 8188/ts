#!/bin/bash

MAIN_EXEC="./main"
RETRY_INTERVAL=10
LOG_FILE="./process_log.txt"

start_process() {
    local exec_cmd=$1
    local pid_var_name=$2

    if [ -z "${!pid_var_name}" ] || ! kill -0 "${!pid_var_name}" 2>/dev/null; then
        echo "Starting $exec_cmd ..." | tee -a $LOG_FILE
        $exec_cmd &
        eval "$pid_var_name=\$!"
        echo "Started process with PID: ${!pid_var_name}" | tee -a $LOG_FILE
    else
        echo "Process ${!pid_var_name} is running." | tee -a $LOG_FILE
    fi
}

cleanup() {
    echo "Stopping script..." | tee -a $LOG_FILE
    for pid in $MAIN_PID $MODBUS_PID; do
        if kill -0 $pid 2>/dev/null; then
            echo "Killing process $pid" | tee -a $LOG_FILE
            kill $pid
            sleep 2
            if kill -0 $pid 2>/dev/null; then
                kill -9 $pid
            fi
        fi
    done
    exit 0
}

trap cleanup SIGINT

while true; do
    start_process "$MAIN_EXEC" MAIN_PID
    sleep $RETRY_INTERVAL
done
