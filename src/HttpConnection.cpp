#include "HttpConnection.h"
#include<sstream>
#include<string>
#include<algorithm>
#include<cctype>
#include<fstream>
#include<sys/stat.h>
#include<cerrno>
#include<iostream>
#include<codecvt>
#include<fcntl.h>
#include<sys/sendfile.h>
namespace {
    static const size_t MAX_HEADER_LINE_LENGTH = 4096; 
    static const size_t MAX_HEADERS_COUNT = 64; 
}
std::string to_lower(const std::string& str) {
    std::string result = str; 
    
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { 
                       return std::tolower(c); 
                   });
    return result;
}
int hexToDecimal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1; // 应对无效字符
}
void url_decode(std::string& path) {
    std::string decoded_path;
    decoded_path.reserve(path.length()); 
    
    for (size_t i = 0; i < path.length(); ++i) {
        if (path[i] == '%') {
            if (i + 2 < path.length() && isxdigit(path[i+1]) && isxdigit(path[i+2])) {
                int high = hexToDecimal(path[i+1]);
                int low = hexToDecimal(path[i+2]);
                
                decoded_path += static_cast<char>(high * 16 + low);
                
                i += 2; 
            } else {
                decoded_path += path[i];
            }
        } else if (path[i] == '+') {
            decoded_path += ' '; 
        } else {
            decoded_path += path[i];
        }
    }
    path = std::move(decoded_path);
}
static std::map<std::string, std::string> mime_map = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".txt", "text/plain"},
    {".ico", "image/x-icon"},
    {"default", "application/octet-stream"}
};
std::string HttpConnection::getMimeType(const std::string& path){
    size_t dot_pos=path.find_last_of('.');
    if(dot_pos!=std::string::npos){
        std::string ext=to_lower(path.substr(dot_pos));
        auto it=mime_map.find(ext);
        if(it!=mime_map.end())return it->second;
    }
    return mime_map["default"];
}
void HttpConnection::handleGetRequest() {
    //静态文件根目录webroot
    const std::string WEB_ROOT = "webroot"; 
    std::string path = _httpRequest.getPath();
    url_decode(path); 
    if (path == "/") {
        path = "/index.html";
    }
    std::string request_filepath_raw = WEB_ROOT + path; 
    char resolved_request_path[PATH_MAX];
    char resolved_webroot_path[PATH_MAX];
    
    //安全检查: 边界检查
    if (realpath(WEB_ROOT.c_str(), resolved_webroot_path) == nullptr) {
        std::cerr << "Error: Webroot path cannot be resolved or is inaccessible." << std::endl;
        _httpResponse.setStatusCode(500, "Internal Server Error");
        _httpResponse.setBody("500 Internal Server Error: Server configuration issue.");
        _httpResponse.setHeader("Content-Type", "text/plain");
        _httpResponse.setCloseConnection(true);
        return;
    }
    std::string webroot_canonical(resolved_webroot_path);
    
    //获取请求文件路径的规范化绝对路径
    if (realpath(request_filepath_raw.c_str(), resolved_request_path) == nullptr) {
        if (errno != ENOENT) {
             _httpResponse.setStatusCode(403, "Forbidden");
             _httpResponse.setBody("403 Forbidden: Invalid path resolution attempt.");
             _httpResponse.setHeader("Content-Type", "text/plain");
             _httpResponse.setCloseConnection(true);
             return;
        } 
    } else {
        std::string request_canonical(resolved_request_path);
        if (request_canonical.size() < webroot_canonical.size() || 
            request_canonical.substr(0, webroot_canonical.size()) != webroot_canonical) 
        {
            _httpResponse.setStatusCode(403, "Forbidden");
            _httpResponse.setBody("403 Forbidden: Path traversal attempt detected.");
            _httpResponse.setHeader("Content-Type", "text/plain");
            _httpResponse.setCloseConnection(true);
            return;
        }
        request_filepath_raw = request_canonical;
    }
    
    //文件状态和 I/O 检查
    struct stat file_stat;
    if (stat(request_filepath_raw.c_str(), &file_stat) < 0) {
        if (errno == ENOENT) {
            _httpResponse.setStatusCode(404, "Not Found");
            _httpResponse.setBody("404 Not Found: The requested resource was not found.");
        } else {
            _httpResponse.setStatusCode(403, "Forbidden");
            _httpResponse.setBody("403 Forbidden: Access denied or file system error.");
        }
        _httpResponse.setHeader("Content-Type", "text/plain");
        _httpResponse.setCloseConnection(true); 
        return;
    }

    //检查是否为常规文件
    if (!S_ISREG(file_stat.st_mode)) {
        _httpResponse.setStatusCode(403, "Forbidden");
        _httpResponse.setBody("403 Forbidden: Cannot serve non-regular files.");
        _httpResponse.setHeader("Content-Type", "text/plain");
        _httpResponse.setCloseConnection(true);
        return;
    }
    _fileFd=::open(request_filepath_raw.c_str(),O_RDONLY);
    if (_fileFd<0) {
        _httpResponse.setStatusCode(500, "Internal Server Error");
        _httpResponse.setBody("500 Internal Server Error: Failed to read file content.");
        _httpResponse.setHeader("Content-Type", "text/plain");
        _httpResponse.setCloseConnection(true);
        return;
    } 
    _fileTotalSize=file_stat.st_size;
    _fileSentOffset=0;
    _httpResponse.setStatusCode(200,"OK");
    _httpResponse.setHeader("Content-Length", std::to_string(_fileTotalSize)); 
    std::string mimeType = getMimeType(path);
    _httpResponse.setHeader("Content-Type", mimeType);
    //将 Header写入_outBuffer
    _httpResponse.appendToBuffer(&_outBuffer);
    //状态机清理和事件更新
    _httpRequest.reset();
    _httpRPS = HttpRequestParseState::kExpectRequestLine;
    updateEvents(EPOLLIN | EPOLLOUT | EPOLLET);
}
HttpConnection::HttpConnection(int fd,EpollCallback mod_cb,EpollCallback close_cb):_clientFd(fd),
_mod_callback(std::move(mod_cb)),_close_callback(std::move(close_cb)),_lastActiveTime(Clock::now()),
_fileFd(-1),_fileSentOffset(0),_fileTotalSize(0){
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
        if (::shutdown(_clientFd, SHUT_RDWR) == -1) {
             if (errno != ENOTCONN) {
                 std::cerr << "Warning: Shutdown failed for FD " << _clientFd << ": " << strerror(errno) << std::endl;
             }
        }
        
        if(::close(_clientFd)==-1){
            std::cerr << "Error closing FD " << _clientFd << ": " << strerror(errno) << std::endl;
        }
        _clientFd=-1;
    }
    if(_fileFd>=0){
        if(::close(_fileFd)==-1){
            std::cerr << "Error closing FD " << _fileFd << ": " << strerror(errno) << std::endl;
        }
        _fileFd=-1;
    }
}
void HttpConnection::handleProcess(){
    bool parse_result=true;

    while(_httpRPS!=HttpRequestParseState::kGotAll&& _inBuffer.readableBytes() > 0){
        //解析数据
        if(!parseRequest()){//解析失败
            _httpRPS=HttpRequestParseState::kParseError;
            parse_result=false;
            break;
        }  
    }
    if(_httpRPS==HttpRequestParseState::kGotAll){
            processRequest();
    }
    if(_httpRPS==HttpRequestParseState::kParseError){//解析失败，关闭连接
        std::cerr << "Request parse error on FD " << _clientFd << std::endl;
        handleClose();
        return ;
    }
    if(_outBuffer.readableBytes()>0||_fileFd>=0){
        updateEvents(EPOLLIN | EPOLLOUT | EPOLLET);
    }else{
        updateEvents(EPOLLIN | EPOLLET);
    }
}
void HttpConnection::handleRead(){
    int savedErrno=0;
    ssize_t n=_inBuffer.readFd(_clientFd,&savedErrno);
    if(n>0){
        updateActiveTime();
        return;
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
    if (_clientFd >= 0) {
        if (::shutdown(_clientFd, SHUT_RDWR) == -1 && errno != ENOTCONN) {
             std::cerr << "Warning: Shutdown failed for FD " << _clientFd << ": " << strerror(errno) << std::endl;
        }
        _clientFd = -1; }
}
void HttpConnection::handleWrite() {
    //发送 Header
    if (_outBuffer.readableBytes() > 0) {
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
        if (_outBuffer.readableBytes() > 0) return; // Header 还没发完，等待下次 EPOLLOUT
    }

    //发送 File Body
    if (_fileFd >= 0 && _fileSentOffset < (off_t)_fileTotalSize) {
        size_t remaining_bytes = _fileTotalSize - _fileSentOffset;
        ssize_t sent_now = ::sendfile(_clientFd, _fileFd, &_fileSentOffset, remaining_bytes);

        if (sent_now < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞，等待下一次 EPOLLOUT
                return;
            }
            std::cerr << "Fatal sendfile error on FD " << _clientFd << ": " << strerror(errno) << std::endl;
            handleClose();
            return;
        }
    }

    //发送完成后的清理
    if (_fileFd >= 0 && _fileSentOffset == (off_t)_fileTotalSize) {
        ::close(_fileFd);
        _fileFd = -1;
        _fileTotalSize = 0;
        _fileSentOffset = 0;

        // 如果是短连接，关闭客户端连接；否则，切换回 EPOLLIN 等待下一个请求
        if (_httpResponse.isCloseConnection()) {
            handleClose(); 
        } else {
            updateActiveTime();
            updateEvents(EPOLLIN | EPOLLET);
        }
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
    _chunkState=kExpectChunkSize;
    _chunkSize=0;
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
    if(line.size()>MAX_HEADER_LINE_LENGTH){
        _httpRPS=HttpRequestParseState::kParseError;
        return false;
    }
    std::stringstream ss(line);
    std::string method_str, uri, version_str;
    
    //尝试读取Method, URI, Version
    if (!(ss >> method_str >> uri >> version_str)) {
        return false; // 读取失败
    }

    //解析 Method
    _httpRequest.setMethod(stringToHttpMethod(method_str));
    if (_httpRequest.getMethod() == HttpMethod::kUnknown) {
        return false; // 不支持的 Method
    }

    //解析 Version
    _httpRequest.setVersion(stringToHttpVersion(version_str));
    if (_httpRequest.getVersion() == HttpVersion::kUnknown) {
        return false; // 不支持的 Version
    }
    
    //解析 URI并分离 Path 和 Query
    size_t query_pos = uri.find('?');
    if (query_pos != std::string::npos) {
        _httpRequest.setPath(uri.substr(0, query_pos));
    } else {
        _httpRequest.setPath(uri);
    }
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
                const char* begin_read = _inBuffer.begin() + _inBuffer.prependableBytes();
                
                //提取请求行
                std::string line(begin_read, crlf); 
                //尝试解析请求行
                ok = parseRequestLine(line);
                //消耗缓冲区中的数据
                size_t line_length = crlf - begin_read;
                _inBuffer.retrieve(line_length + 2); 

                if (ok) {
                    _httpRPS = HttpRequestParseState::kExpectHeaders;
                } else {
                    _httpRPS = HttpRequestParseState::kParseError; 
                }
            } else {
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
            if(line_length>MAX_HEADER_LINE_LENGTH||_httpRequest._headers.size()>MAX_HEADERS_COUNT){
                _httpRPS=HttpRequestParseState::kParseError;
                return false;
            }
            
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
        }
        else if (_httpRPS == HttpRequestParseState::kExpectBody) {
            const std::string& te_str = _httpRequest.getHeader("transfer-encoding");
            
            //检查分块传输
            if (to_lower(te_str) == "chunked") {
                // 调用分块解析函数
                ok = parseChunkedBody();
                
                if (ok && _httpRequest._chunkState == HttpRequest::kExpectChunkBodyDone) {
                    _httpRPS = HttpRequestParseState::kGotAll;
                } else if (!ok) {
                    _httpRPS = HttpRequestParseState::kParseError;
                } else {
                    hasMore = false; // 等待更多数据
                }
            }
            //检查是否为 Content-Length 传输
            else {
                const std::string& cl_str=_httpRequest.getHeader("content-length");
                long long content_length=0;
                // 尝试解析 Content-Length
                if (!cl_str.empty()) {
                    try{
                        content_length=std::stoll(cl_str);
                    }catch(const std::exception& e){
                        std::cerr << "Error parsing Content-Length in Body: " << e.what() << std::endl;
                        ok=false; // 格式错误，设置 ok=false
                    }
                }
                if (ok) {
                    if(content_length>0){
                        //数据完整，可以解析
                        if(_inBuffer.readableBytes()>=content_length){
                            const char* body_start=_inBuffer.begin()+_inBuffer.prependableBytes();
                            std::string body_content(body_start,(size_t)content_length);
                            _httpRequest.setBody(body_content); 
                            _inBuffer.retrieve((size_t)content_length);
                            _httpRPS=HttpRequestParseState::kGotAll;
                        } else {
                            hasMore=false; 
                        }
                    } else {
                        _httpRPS=HttpRequestParseState::kGotAll;
                    }
                }
                if(_httpRPS==HttpRequestParseState::kGotAll||!ok){
                hasMore=false;
                }
            }
        }
        if (!ok) {
            return false; 
        }
    }
    return true; 
}

std::string HttpConnection::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    return (start == std::string::npos) ? "" : str.substr(start, end - start + 1);
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
//processRequest 框架 (负责业务逻辑)
void HttpConnection::processRequest() {
    _httpResponse.reset();
    const std::string& conn_value = _httpRequest.getHeader("connection");
    bool keepAlive = (conn_value == "keep-alive");
    _httpResponse.setCloseConnection(!keepAlive); 
    
    // 检查 GET 和 HEAD 方法
    if (_httpRequest.getMethod() == HttpMethod::kGet || _httpRequest.getMethod() == HttpMethod::kHead) {
        
        std::cout << "Received " 
                  << (_httpRequest.getMethod() == HttpMethod::kHead ? "HEAD" : "GET") 
                  << " request for path: " << _httpRequest.getPath() << std::endl;
        //执行文件安全检查,准备零拷贝状态
        handleGetRequest(); 
        //如果是 HEAD 请求，必须清除响应体
        if (_httpRequest.getMethod() == HttpMethod::kHead) {
            _httpResponse.setBody(""); 
        }

    } else if(_httpRequest.getMethod()==HttpMethod::kPost||_httpRequest.getMethod()==HttpMethod::kPut){
        handlePostOrPutRequest();
    }
    else {
        _httpResponse.setStatusCode(405, "Method Not Allowed");
        _httpResponse.setBody("Method not supported.");
        _httpResponse.setHeader("Content-Type", "text/plain");
        _httpResponse.setCloseConnection(true);
    }
    
    _httpResponse.appendToBuffer(&_outBuffer);
    _httpRequest.reset();
    _httpRPS = HttpRequestParseState::kExpectRequestLine;
    updateEvents(EPOLLIN | EPOLLOUT | EPOLLET);
}

bool HttpConnection::parseChunkedBody() {
    bool ok = true;
    do{
        switch (_httpRequest._chunkState) {
            case HttpRequest::kExpectChunkSize:{
                const char* crlf = _inBuffer.findCRLF();
                if (crlf == nullptr) {
                    return true; // 数据不足，等待下次读取
                }

                const char* begin_read = _inBuffer.begin() + _inBuffer.prependableBytes();
                size_t chunk_size_val = 0;
                bool hex_found = false;
                
                //迭代解析十六进制大小
                const char* current = begin_read;
                for (; current < crlf; ++current) {
                    int digit = hexToDecimal(*current); 
                    
                    if (digit != -1) { 
                        chunk_size_val = chunk_size_val * 16 + digit;
                        hex_found = true;
                    } else if (*current == ' ' || *current == ';') {
                        
                        break;
                    } else if (isxdigit(*current)) { 
                         ok = false; 
                         break;
                    }
                }
                if (!ok || !hex_found) { 
                    _httpRequest._chunkSize = 0;
                    ok = false;
                    break;
                }
                
                _httpRequest._chunkSize = chunk_size_val;
                _inBuffer.retrieve((crlf - begin_read) + 2);
                if (_httpRequest._chunkSize > 0) {
                    _httpRequest._chunkState = HttpRequest::kExpectChunkData;
                } else {
                    _httpRequest._chunkState = HttpRequest::kExpectChunkBodyDone;
                }
                break;}
            case HttpRequest::kExpectChunkData:{
            size_t chunk_len = _httpRequest._chunkSize;
                if (chunk_len > _inBuffer.readableBytes()) {
                    return true; 
                }
                
                const char* begin_read = _inBuffer.begin() + _inBuffer.prependableBytes();
                
                _httpRequest._body.append(begin_read, chunk_len);

                _inBuffer.retrieve(chunk_len);
                
                _httpRequest._chunkSize = 0; 
                _httpRequest._chunkState = HttpRequest::kExpectChunkCRLF;
                break;}
            case HttpRequest::kExpectChunkCRLF:{
                if(_inBuffer.readableBytes()<2)return true;
                else{
                    const char* crlf=_inBuffer.begin()+_inBuffer.prependableBytes();
                    if(crlf[0]=='\r'&&crlf[1]=='\n'){
                        _inBuffer.retrieve(2);
                        _httpRequest._chunkState=HttpRequest::kExpectChunkSize;
                        
                    }
                    else ok=false;
                }
                break;
            case HttpRequest::kExpectChunkBodyDone:
                const char* crlf=_inBuffer.findCRLF();
                if(crlf==nullptr)return true;
                const char* begin_read=_inBuffer.begin()+_inBuffer.prependableBytes();
                size_t line_length=crlf-begin_read;
                _inBuffer.retrieve(line_length+2);
                if(line_length>0){
                    const std::string line(begin_read,line_length);
                    if(!parseHeaderLine(line)){
                        ok=false;
                        break;
                    }
                }
                if(line_length==0)return true;
                break;}
            default:{
                ok = false;
                break;}
        }
    }while (ok && _httpRequest._chunkState != HttpRequest::kExpectChunkBodyDone);
    return ok;
}
void HttpConnection::handlePostOrPutRequest(){
    //静态文件根目录webroot
    const std::string WEB_ROOT = "webroot"; 
    std::string path = _httpRequest.getPath();
    std::string request_filepath_raw;
    std::string webroot_canonical; 
    std::string request_canonical;
    bool file_exists;
    //URL 解码路径
    url_decode(path); 
    request_filepath_raw = WEB_ROOT + path;
    char resolved_request_path[PATH_MAX];
    char resolved_webroot_path[PATH_MAX];
    
    //边界检查
    if (realpath(WEB_ROOT.c_str(), resolved_webroot_path) == nullptr) {
        std::cerr << "Error: Webroot path cannot be resolved or is inaccessible." << std::endl;
        _httpResponse.setStatusCode(500, "Internal Server Error");
        _httpResponse.setBody("500 Internal Server Error: Server configuration issue.");
        _httpResponse.setHeader("Content-Type", "text/plain");
        _httpResponse.setCloseConnection(true);
        goto end_request;
    }
    webroot_canonical = resolved_webroot_path; 
    
    //获取请求文件路径的规范化绝对路径
    if (realpath(request_filepath_raw.c_str(), resolved_request_path) == nullptr) {
        if (errno != ENOENT) {
             _httpResponse.setStatusCode(403, "Forbidden");
             _httpResponse.setBody("403 Forbidden: Invalid path resolution attempt.");
             _httpResponse.setHeader("Content-Type", "text/plain");
             _httpResponse.setCloseConnection(true);
             goto end_request;
        } 
    } else {
        request_canonical = resolved_request_path;
        
        if (request_canonical.size() < webroot_canonical.size() || 
            request_canonical.substr(0, webroot_canonical.size()) != webroot_canonical) 
        {
            _httpResponse.setStatusCode(403, "Forbidden");
            _httpResponse.setBody("403 Forbidden: Path traversal attempt detected.");
            _httpResponse.setHeader("Content-Type", "text/plain");
            _httpResponse.setCloseConnection(true);
            goto end_request;
        }
        request_filepath_raw = request_canonical;
    }
    struct stat file_stat;
    file_exists=(stat(request_filepath_raw.c_str(),&file_stat)==0);
    if(file_exists){
        if(!S_ISREG(file_stat.st_mode)){
            _httpResponse.setStatusCode(403, "Forbidden");
            _httpResponse.setBody("403 Forbidden: Cannot write to non-regular files or directories.");
            _httpResponse.setHeader("Content-Type", "text/plain");
            _httpResponse.setCloseConnection(true);
            goto end_request; 
        }
    }
    if (_httpRequest.getMethod() == HttpMethod::kPut) {
        //尝试打开/创建文件
        int target_fd = ::open(request_filepath_raw.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0664);

        if (target_fd < 0) {
            _httpResponse.setStatusCode(403, "Forbidden");
            _httpResponse.setBody("403 Forbidden: Failed to open or create file for writing.");
             _httpResponse.setHeader("Content-Type", "text/plain");
             _httpResponse.setCloseConnection(true);
        } else {
            //写入数据
            size_t body_size = _httpRequest._body.size();
            ssize_t bytes_written = ::write(target_fd, _httpRequest._body.data(), body_size);
            ::close(target_fd);
            if (bytes_written == (ssize_t)body_size) {
                //写入成功，设置响应
                _httpResponse.setStatusCode(200, "OK"); 
                _httpResponse.setBody("Resource updated/created successfully.");
                _httpResponse.setHeader("Content-Type", "text/plain");
            } else {
                //写入失败
                _httpResponse.setStatusCode(500, "Internal Server Error");
                _httpResponse.setBody("500 Internal Server Error: Failed to write all content to file.");
                _httpResponse.setHeader("Content-Type", "text/plain");
                _httpResponse.setCloseConnection(true);
            }
        } 
    }
    else if (_httpRequest.getMethod() == HttpMethod::kPost) {
            _httpResponse.setStatusCode(204, "No Content"); 
    }
    end_request:
        _httpResponse.appendToBuffer(&_outBuffer);
        _httpRequest.reset();
        _httpRPS = HttpRequestParseState::kExpectRequestLine;
        updateEvents(EPOLLIN | EPOLLOUT | EPOLLET);
    
}