#include "HttpConnection.h"
#include<sstream>
#include<string>
#include<algorithm>
#include<cctype>
HttpConnection::HttpConnection(int fd,EpollCallback mod_cb,EpollCallback close_cb):_clientFd(fd),_mod_callback(std::move(mod_cb)),_close_callback(std::move(close_cb)){
    if(_clientFd<0){
        throw std::runtime_error("Invalid file descriptor passed to HttpConnection.");
    }
    Socket::set_nonblocking(fd);

}
HttpConnection::~HttpConnection(){
    closeConnection();
}
void HttpConnection::closeConnection(){
    if(_clientFd>=0){
        if(::close(_clientFd)==-1){
            std::cerr << "Error closing FD " << _clientFd << ": " << strerror(errno) << std::endl;
        }
        _clientFd=-1;
    }
}
void HttpConnection::handleRead(){
    int savedErrno=0;
    ssize_t n=_inBuffer.readFd(_clientFd,&savedErrno);
    if(n>0){
        bool parse_result=true;

        while(_httpRPS!=HttpRequestParseState::kGotAll&& _inBuffer.readableBytes() > 0){
            //使用状态机解析_inBuffer中的数据
            if(!parseRequest()){//解析失败
                _httpRPS=HttpRequestParseState::kParseError;
                parse_result=false;
                break;
            }  
        }
        if(_httpRPS==HttpRequestParseState::kGotAll){//完整的协议请求已解析
                processRequest();
        }
        if(_httpRPS==HttpRequestParseState::kParseError){//解析失败，关闭连接
            std::cerr << "Request parse error on FD " << _clientFd << std::endl;
            handleClose();
            return ;
        }
        if(_outBuffer.readableBytes()>0){
            updateEvents(EPOLLIN | EPOLLOUT | EPOLLET);
        }
    }else if(n==0){
        handleClose();
    }else{
        if(savedErrno==EAGAIN||savedErrno==EWOULDBLOCK){
            return;
        }else{
            std::cerr << "Fatal read error on FD " << _clientFd << ": " << strerror(savedErrno) << std::endl;
            handleClose();
        }
    }
}
void HttpConnection::handleClose(){
    if(_close_callback){
        _close_callback(_clientFd,0);
    }
    closeConnection();
}
void HttpConnection::handleWrite(){
    if(_outBuffer.readableBytes()==0)return ;
    size_t data_to_send=_outBuffer.readableBytes();
    ssize_t bytes_written=0;
    while(data_to_send>0){
        const char* write_ptr=_outBuffer.begin()+_outBuffer.prependableBytes();
        size_t len=data_to_send;
        ssize_t written_now=::write(_clientFd,write_ptr,len);
        if(written_now<0){
            if(errno==EAGAIN||errno==EWOULDBLOCK)break;
            std::cerr << "Fatal write error on FD " << _clientFd << ": " << strerror(errno) << std::endl;
            handleClose();
            return;
        }
        _outBuffer.retrieve(written_now);
        data_to_send-=written_now;
        bytes_written+=written_now;
    }
    if(_outBuffer.readableBytes()==0){
        updateEvents(EPOLLIN | EPOLLET);
    }
}
void HttpConnection::updateEvents(uint32_t events){
    if(_mod_callback){
        _mod_callback(_clientFd,events);
    }
}
void HttpRequest::addHeader(const std::string& field,const std::string& value){
    _headers[field]=value;
}
const std::string& HttpRequest::getHeader(const std::string& field) const{
    static const std::string empty;
    auto it =_headers.find(field);
    if(it==_headers.end()){
        return empty;
    }
    return it->second;
}
bool HttpRequest::hasHeader(const std::string& field) const {
    return _headers.count(field)>0;
}
void HttpRequest::reset(){
    _method=HttpMethod::kUnknown;
    _version=HttpVersion::kUnknown;
    _path.clear();
    _query.clear();
    _headers.clear();
    _body.clear();
}
bool HttpRequest::isParsedCompletely()const{
    return _method!=HttpMethod::kUnknown&&_version!=HttpVersion::kUnknown;
}
HttpMethod stringToHttpMethod(const std::string& method_str) {
    if (method_str == "GET") return HttpMethod::kGet;
    if (method_str == "POST") return HttpMethod::kPost;
    if (method_str == "HEAD") return HttpMethod::kHead;
    if (method_str == "PUT") return HttpMethod::kPut;
    if (method_str == "DELETE") return HttpMethod::kDelete;
    return HttpMethod::kUnknown;
}
HttpVersion stringToHttpVersion(const std::string& version_str) {
    if (version_str == "HTTP/1.1") return HttpVersion::kHttp11;
    if (version_str == "HTTP/1.0") return HttpVersion::kHttp10;
    return HttpVersion::kUnknown;
}
bool HttpConnection::parseRequestLine(const std::string& line) {
    std::stringstream ss(line);
    std::string method_str, uri, version_str;
    
    // 尝试读取三个部分：Method, URI, Version
    if (!(ss >> method_str >> uri >> version_str)) {
        return false; // 读取失败 (格式不正确)
    }

    // 1. 解析 Method
    _httpRequest.setMethod(stringToHttpMethod(method_str));
    if (_httpRequest.getMethod() == HttpMethod::kUnknown) {
        return false; // 不支持的 Method
    }

    // 2. 解析 Version
    _httpRequest.setVersion(stringToHttpVersion(version_str));
    if (_httpRequest.getVersion() == HttpVersion::kUnknown) {
        return false; // 不支持的 Version
    }
    
    // 3. 解析 URI (并分离 Path 和 Query)
    size_t query_pos = uri.find('?');
    if (query_pos != std::string::npos) {
        _httpRequest.setPath(uri.substr(0, query_pos));
        // 这里只是简单存储，实际 Query 参数的键值对解析可以后续处理
        // _httpRequest.setQuery(uri.substr(query_pos + 1)); 
    } else {
        _httpRequest.setPath(uri);
    }

    // 格式检查通过
    return true; 
}
bool HttpConnection::shouldHaveBody()const{
    if(_httpRequest.getMethod()==HttpMethod::kGet||_httpRequest.getMethod()==HttpMethod::kHead)return false;
    const std::string& cl_str=_httpRequest.getHeader("Content-Length");
    if(!cl_str.empty()){
        try{
            long long content_length=std::stoll(cl_str);
            if(content_length>0)return true;
        }catch (const std::exception& e){
            std::cerr << "Error parsing Content-Length: " << e.what() << std::endl;
            return false;
        }
        
    }
    const std::string& te_str=_httpRequest.getHeader("Transfer-Encoding");
    if(te_str=="chunked")return true;
    return false;
}
bool HttpConnection::parseRequest() {
    bool ok = true;
    bool hasMore = true; 

    while (_httpRPS != HttpRequestParseState::kGotAll && hasMore) {
        if (_httpRPS == HttpRequestParseState::kExpectRequestLine) {
            const char* crlf = _inBuffer.findCRLF();
            
            if (crlf) {
                // 找到了完整的请求行 (\r\n)
                const char* begin_read = _inBuffer.begin() + _inBuffer.prependableBytes();
                
                // 1. 提取请求行 (不包含 \r\n)
                std::string line(begin_read, crlf); 
                
                // 2. 尝试解析请求行
                ok = parseRequestLine(line);
                
                // 3. 消耗缓冲区中的数据 (请求行 + \r\n)
                size_t line_length = crlf - begin_read;
                _inBuffer.retrieve(line_length + 2); 

                if (ok) {
                    _httpRPS = HttpRequestParseState::kExpectHeaders; // 成功，进入下一状态
                } else {
                    _httpRPS = HttpRequestParseState::kParseError; // 格式错误
                }
            } else {
                // 没有找到完整的请求行 (\r\n)，数据不完整，等待下一次读取
                hasMore = false; 
            }
        } 
        else if (_httpRPS == HttpRequestParseState::kExpectHeaders) {
            const char* crlf=_inBuffer.findCRLF();
            if(crlf==nullptr){
                hasMore=false;
                break;
            }
            const char* begin_read=_inBuffer.begin()+_inBuffer.prependableBytes();
            size_t line_length=crlf-begin_read;
            if(line_length==0){
                _inBuffer.retrieve(2);
                if(shouldHaveBody())_httpRPS=HttpRequestParseState::kExpectBody;
                else{
                    _httpRPS=HttpRequestParseState::kGotAll;
                    hasMore = false;
                }
                
            }else if(line_length>0){
                const std::string line(begin_read,crlf);
                if(!parseHeaderLine(line)){
                    _httpRPS=HttpRequestParseState::kParseError;
                }
                _inBuffer.retrieve(line_length+2);
            }
            // TODO: 实现 Header 解析 (下一步)
            // 暂时跳过，以便测试请求行解析
            
            
        }
        else if (_httpRPS == HttpRequestParseState::kExpectBody) {
            // TODO: 实现 Body 解析
            _httpRPS = HttpRequestParseState::kGotAll; 
            hasMore = false;
        }
        else {
            hasMore = false;
        }

        if (!ok) {
            return false; // 格式错误，中断并返回失败
        }
    }
    return true; // 解析流程正常
}

std::string HttpConnection::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    return (start == std::string::npos) ? "" : str.substr(start, end - start + 1);
}
std::string to_lower(const std::string& str) {
    std::string result = str; // 创建一个副本进行操作
    
    // 使用 std::transform 遍历字符串中的每个字符
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { 
                       return std::tolower(c); 
                   });
    return result;
}
bool HttpConnection::parseHeaderLine(const std::string& line){
    size_t colon_position;
    if((colon_position=line.find(':'))==-1)return false;
    const std::string& fieldName=HttpConnection::trim(line.substr(0,colon_position));
    const std::string& value=HttpConnection::trim(line.substr(colon_position+1));
    std::string lowerCaseFieldName = to_lower(fieldName);
    _httpRequest.addHeader(lowerCaseFieldName,value);
    return true;
}
// 【新增】processRequest 框架 (负责业务逻辑)
void HttpConnection::processRequest() {
    const std::string& conn_value = _httpRequest.getHeader("connection");
    bool keepAlive = (conn_value == "keep-alive");
    _httpResponse.setCloseConnection(!keepAlive); 
    if (_httpRequest.getMethod() == HttpMethod::kGet) {
        std::cout << "Received GET request for path: " << _httpRequest.getPath() << std::endl;
        
        // 临时的响应生成：返回一个简单的 200 OK
        _httpResponse.setStatusCode(200, "OK");
        _httpResponse.setBody("Hello from your C++ HTTP Server!");
        _httpResponse.setHeader("Content-Type", "text/plain");
        
    } else {
        // 其他 Method，返回 405
        _httpResponse.setStatusCode(405, "Method Not Allowed");
        _httpResponse.setBody("Method not supported.");
        _httpResponse.setHeader("Content-Type", "text/plain");
        
    }
    _httpResponse.appendToBuffer(&_outBuffer);
    _httpRequest.reset();
    _httpRPS = HttpRequestParseState::kExpectRequestLine;
    updateEvents(EPOLLIN | EPOLLOUT | EPOLLET);
}
