#!/bin/bash

# --- 配置 ---
HOST="127.0.0.1"
PORT="8080"
SCRIPT_NAME="test_chunked.sh"

echo "--- [${SCRIPT_NAME}] Sending Chunked POST Request to ${HOST}:${PORT} ---"

# 使用 { ... } | nc 结构，并利用 printf 确保 \r\n (CRLF) 被正确发送。
{
    # 请求行和必要头
    printf "POST /test.html HTTP/1.1\r\n"
    printf "Host: %s:%s\r\n" "$HOST" "$PORT"
    printf "Transfer-Encoding: chunked\r\n"
    printf "Connection: close\r\n"
    printf "\r\n" # 结束 Headers
    
    # 第一个分块 (5 字节)
    printf "5\r\n"
    printf "Hello\r\n"
    
    # 第二个分块 (7 字节)
    printf "7\r\n"
    printf " World!\r\n" # 注意这里的空格也是数据的一部分
    
    # 零块 (0 字节) - 标记消息体结束，并处理 Footer
    printf "0\r\n"  
    printf "X-Chunk-Footer: Test\r\n" # Footer (测试 kExpectChunkBodyDone 逻辑)
    
    printf "\r\n" # 最后的空行 (标记整个请求结束)
} | nc "$HOST" "$PORT"

echo "--- Request Sent. Check Server Log for successful parsing (405 response expected). ---"