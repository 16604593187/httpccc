// include/http_response.h
#ifndef HTTPCCC_HTTPRESPONSE_H // 【新增：检查是否已定义】
#define HTTPCCC_HTTPRESPONSE_H 
#include "Buffer.h" // 需要使用Buffer类进行序列化
#include <string>
#include <map>
#include <vector>
#include <cstring>

class HttpResponse {
private:
    int _statusCode; // 状态码，如 200
    std::string _statusMessage; // 状态消息，如 "OK"
    std::map<std::string, std::string> _headers;
    std::string _body; // 响应主体内容
    bool _closeConnection; // 是否为短连接 (Connection: close)

public:
    HttpResponse() : _statusCode(0), _closeConnection(false) {}

    // --- 设置方法（供业务逻辑调用） ---
    void setStatusCode(int code, const std::string& message);
    void setHeader(const std::string& field, const std::string& value);
    void setBody(const std::string& body);
    void setCloseConnection(bool close) { _closeConnection = close; }
    
    // --- 状态获取方法 ---
    bool isCloseConnection() const { return _closeConnection; }
    int getStatusCode() const { return _statusCode; }

    // 【核心】序列化：将响应内容（状态行+Header+Body）写入Buffer
    void appendToBuffer(Buffer* outputBuffer); 
    
    // 辅助工具：获取状态消息的字符串 (可在cpp中实现映射表)
    static const char* getStatusMessage(int code); 
};
#endif