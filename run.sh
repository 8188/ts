#!/bin/bash

# 设置需要运行的程序和参数
UTILS_EXEC="./utils"
UTILS_PID=

# 定义等待的秒数
RETRY_INTERVAL=10

# 定义信号处理函数
cleanup() {
    echo "Stopping script..."
    # 检查是否有正在运行的进程，如果有则杀死它
    if [ -n "$UTILS_PID" ] && kill -0 $UTILS_PID 2>/dev/null; then
        echo "Killing process $UTILS_PID"
        kill -9 $UTILS_PID
    fi
    exit 0
}

# 注册信号处理函数
trap cleanup SIGINT

# 循环检查进程是否存在
while true; do
    # 检查是否已经有 pid 存在
    if [ -n "$UTILS_PID" ] && kill -0 $UTILS_PID 2>/dev/null; then
        echo "Process $UTILS_PID is running."
    else
        echo "Process is not running. Starting $UTILS_EXEC ..."
        # 启动程序并获取其 pid
        $UTILS_EXEC &
        UTILS_PID=$!
        echo "Started process with PID: $UTILS_PID"
    fi

    # 等待一段时间后重试
    sleep $RETRY_INTERVAL
done
