// include/http_request.h
#ifndef HTTPCCC_HTTPREQUEST_H // 【新增：检查是否已定义】
#define HTTPCCC_HTTPREQUEST_H 
#include <string>
#include <map>
#include <iostream>

// 定义请求方法
enum class HttpMethod {
    kGet, kPost, kHead, kPut, kDelete, kUnknown
};

// 定义HTTP版本
enum class HttpVersion {
    kHttp10, kHttp11, kUnknown
};
enum class HttpRequestParseState{
    kExpectRequestLine,//期待解析请求行
    kExpectHeaders,//期待解析header字段
    kExpectBody,//期待解析body
    kGotAll,//已经完整解析一个请求
    kParseError//解析过程中发生错误
};
class HttpRequest {
private:
    HttpMethod _method;
    HttpVersion _version;
    std::string _path;
    std::string _query; // URL中的查询参数部分，例如 ?key=value
    std::map<std::string, std::string> _headers;
    std::string _body;
    
    // 【重要】定义解析状态枚举（将在HttpConnection中实现状态机）
    // 暂且留空，后续与 HttpConnection 集成时再定义
    
public:
    HttpRequest() : _method(HttpMethod::kUnknown), _version(HttpVersion::kUnknown) {}
    
    // --- 状态设置方法（供解析器调用） ---
    // 为了效率，通常接收原始指针范围
    void setMethod(HttpMethod method) { _method = method; }
    void setVersion(HttpVersion v) { _version = v; }
    void setPath(const std::string& path) { _path = path; }
    void setBody(const std::string& body) { _body = body; }
    void addHeader(const std::string& field, const std::string& value);
    
    // --- 状态获取方法 ---
    HttpMethod getMethod() const { return _method; }
    HttpVersion getVersion()const {return _version;}
    const std::string& getPath() const { return _path; }
    const std::string& getHeader(const std::string& field) const;
    bool hasHeader(const std::string& field) const;

    // --- 生命周期管理 ---
    void reset(); // 清除所有状态，准备接收下一个请求 (用于长连接)
    bool isParsedCompletely() const; // 检查请求是否解析完毕
};
#endif