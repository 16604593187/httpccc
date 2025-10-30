#ifndef HTTPCCC_HTTPREQUEST_H 
#define HTTPCCC_HTTPREQUEST_H 
#include <string>
#include <map>
#include <iostream>
#include "HttpConnection.h"
// 定义请求方法
enum class HttpMethod {
    kGet, kPost, kHead, kPut, kDelete, kUnknown
};

// 定义HTTP版本
enum class HttpVersion {
    kHttp10, kHttp11, kUnknown
};
enum class HttpRequestParseState{
    kExpectRequestLine,
    kExpectHeaders,
    kExpectBody,
    kGotAll,
    kParseError
};
class HttpRequest {

    
public:
    HttpRequest() : _method(HttpMethod::kUnknown), _version(HttpVersion::kUnknown) {}
    //状态设置方法
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
    void reset(); 
    bool isParsedCompletely() const; 
private:
    friend class HttpConnection;
    HttpMethod _method;
    HttpVersion _version;
    std::string _path;
    std::string _query; 
    std::map<std::string, std::string> _headers;
    std::string _body;
    
    enum ChunkParseState{
        kExpectChunkSize,
        kExpectChunkData,
        kExpectChunkCRLF,
        kExpectChunkBodyDone
    };
    ChunkParseState _chunkState;
    size_t _chunkSize;
};
#endif