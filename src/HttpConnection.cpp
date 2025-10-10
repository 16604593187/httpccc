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
std::string to_lower(const std::string& str) {
    std::string result = str; // 创建一个副本进行操作
    
    // 使用 std::transform 遍历字符串中的每个字符
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
    // 预留空间，避免多次分配
    decoded_path.reserve(path.length()); 
    
    for (size_t i = 0; i < path.length(); ++i) {
        if (path[i] == '%') {
            // 检查是否有足够的字符进行解码，并且后两位是十六进制数字
            if (i + 2 < path.length() && isxdigit(path[i+1]) && isxdigit(path[i+2])) {
                // 1. 获取高位和低位的十进制值
                int high = hexToDecimal(path[i+1]);
                int low = hexToDecimal(path[i+2]);
                
                // 2. 合并并转换回字符
                decoded_path += static_cast<char>(high * 16 + low);
                
                // 3. 跳过已处理的两位十六进制字符
                i += 2; 
            } else {
                // 编码格式不完整或无效，按字面值处理 '%'
                decoded_path += path[i];
            }
        } else if (path[i] == '+') {
            // 虽然标准 HTTP 路径不常使用，但 Web 表单中 '+' 通常被解码为空格
            decoded_path += ' '; 
        } else {
            // 其他字符，直接追加
            decoded_path += path[i];
        }
    }
    // 使用 std::move解码后的内容赋值回原字符串
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
    // 假设静态文件根目录为 webroot
    const std::string WEB_ROOT = "webroot"; 
    std::string path = _httpRequest.getPath();
    
    // 0. URL 解码路径
    url_decode(path); 
    
    // 1. 默认路径处理: 如果请求根目录，则返回 index.html
    if (path == "/") {
        path = "/index.html";
    }

    // 2. 构造请求的文件路径和 Webroot 的绝对路径
    std::string request_filepath_raw = WEB_ROOT + path; // e.g., "webroot/../.gitignore"
    
    // POSIX 规范化路径所需的缓冲区
    char resolved_request_path[PATH_MAX];
    char resolved_webroot_path[PATH_MAX];
    
    // --- 3. 安全检查: 边界检查 (使用 realpath) ---
    
    // A. 获取 Webroot 目录的规范化绝对路径
    // 注意：如果 WEB_ROOT 不存在或无法访问，realpath 会返回 nullptr
    if (realpath(WEB_ROOT.c_str(), resolved_webroot_path) == nullptr) {
        std::cerr << "Error: Webroot path cannot be resolved or is inaccessible." << std::endl;
        _httpResponse.setStatusCode(500, "Internal Server Error");
        _httpResponse.setBody("500 Internal Server Error: Server configuration issue.");
        _httpResponse.setHeader("Content-Type", "text/plain");
        _httpResponse.setCloseConnection(true);
        return;
    }
    std::string webroot_canonical(resolved_webroot_path);
    
    // B. 获取请求文件路径的规范化绝对路径
    // 如果文件不存在，realpath 会失败，此时我们不判断 403，而是让 stat 检查返回 404。
    if (realpath(request_filepath_raw.c_str(), resolved_request_path) == nullptr) {
        // 如果 realpath 失败且错误不是 ENOENT (文件不存在)，则可能是一个非法的路径操作，我们按 403 处理
        if (errno != ENOENT) {
             _httpResponse.setStatusCode(403, "Forbidden");
             _httpResponse.setBody("403 Forbidden: Invalid path resolution attempt.");
             _httpResponse.setHeader("Content-Type", "text/plain");
             _httpResponse.setCloseConnection(true);
             return;
        }
        // 如果是 ENOENT，我们继续到 stat 检查，它会返回 404
        
    } else {
        // C. 执行边界检查：确认规范化后的请求路径是否以 Webroot 路径开头
        std::string request_canonical(resolved_request_path);
        
        // 检查请求路径是否是 webroot 路径的前缀 (必须使用 starts_with 或等效逻辑)
        if (request_canonical.size() < webroot_canonical.size() || 
            request_canonical.substr(0, webroot_canonical.size()) != webroot_canonical) 
        {
            // 规范化后的路径不再是 webroot 的子目录 -> 目录穿越成功
            _httpResponse.setStatusCode(403, "Forbidden");
            _httpResponse.setBody("403 Forbidden: Path traversal attempt detected.");
            _httpResponse.setHeader("Content-Type", "text/plain");
            _httpResponse.setCloseConnection(true);
            return;
        }
        
        // D. 路径安全，更新用于 stat 的 filepath
        request_filepath_raw = request_canonical;
    }
    
    // --- 4. 文件状态和 I/O 检查 (使用 stat) ---

    // 此时 request_filepath_raw 可能是规范化后的绝对路径，或者如果 realpath 失败，仍然是原始的相对路径（等待 stat 检查 404）
    
    struct stat file_stat;
    if (stat(request_filepath_raw.c_str(), &file_stat) < 0) {
        // 文件不存在 (ENOENT) 或其他错误
        if (errno == ENOENT) {
            // 404 Not Found
            _httpResponse.setStatusCode(404, "Not Found");
            _httpResponse.setBody("404 Not Found: The requested resource was not found.");
        } else {
            // 403 Forbidden (权限问题或服务器内部错误)
            _httpResponse.setStatusCode(403, "Forbidden");
            _httpResponse.setBody("403 Forbidden: Access denied or file system error.");
        }
        _httpResponse.setHeader("Content-Type", "text/plain");
        _httpResponse.setCloseConnection(true); 
        return;
    }

    // 5. 检查是否为常规文件 (S_ISREG) - 防止目录列表泄露
    if (!S_ISREG(file_stat.st_mode)) {
        // 403 Forbidden (尝试访问目录、管道等)
        _httpResponse.setStatusCode(403, "Forbidden");
        _httpResponse.setBody("403 Forbidden: Cannot serve non-regular files.");
        _httpResponse.setHeader("Content-Type", "text/plain");
        _httpResponse.setCloseConnection(true);
        return;
    }
    
    // --- 200 OK: 文件找到并可读 ---
    
    // 6. 尝试读取文件内容 (使用 request_filepath_raw, 它现在是一个安全路径)
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
    
    // 4. 将 Header 写入 _outBuffer
    _httpResponse.appendToBuffer(&_outBuffer);
    
    // 5. 状态机清理和事件更新
    _httpRequest.reset();
    _httpRPS = HttpRequestParseState::kExpectRequestLine;
    updateEvents(EPOLLIN | EPOLLOUT | EPOLLET);
}
HttpConnection::HttpConnection(int fd,EpollCallback mod_cb,EpollCallback close_cb):_clientFd(fd),
_mod_callback(std::move(mod_cb)),_close_callback(std::move(close_cb)),
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
void HttpConnection::handleWrite() {
    // --- 阶段 1: 发送 Header (使用 Buffer) ---
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

    // --- 阶段 2: 发送 File Body (使用 sendfile) ---
    // 只有当 Header 发送完毕 (_outBuffer 为空) 且有文件待发送时才进入
    if (_fileFd >= 0 && _fileSentOffset < (off_t)_fileTotalSize) {
        size_t remaining_bytes = _fileTotalSize - _fileSentOffset;
        ssize_t sent_now = ::sendfile(_clientFd, _fileFd, &_fileSentOffset, remaining_bytes);

        if (sent_now < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞，等待下一次 EPOLLOUT
                return;
            }
            // 致命错误，关闭连接
            std::cerr << "Fatal sendfile error on FD " << _clientFd << ": " << strerror(errno) << std::endl;
            handleClose();
            return;
        }
        
        // 注意：sendfile 的偏移量 _fileSentOffset 会自动更新！
        // 无需手动更新 _fileSentOffset += sent_now;
    }

    // --- 阶段 3: 发送完成后的清理 ---
    if (_fileFd >= 0 && _fileSentOffset == (off_t)_fileTotalSize) {
        // 文件全部发送完毕
        ::close(_fileFd); // 关闭文件描述符
        _fileFd = -1;
        _fileTotalSize = 0;
        _fileSentOffset = 0;

        // 如果是短连接，关闭客户端连接；否则，切换回 EPOLLIN 等待下一个请求
        if (_httpResponse.isCloseConnection()) {
            handleClose(); // 调用关闭回调，移除连接
        } else {
            // 长连接：切换回只监听读事件 (EPOLLIN)，等待下一个请求
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
            const std::string& te_str = _httpRequest.getHeader("transfer-encoding");
            
            // 1. 检查是否为分块传输
            if (to_lower(te_str) == "chunked") {
                // 调用新的分块解析函数
                ok = parseChunkedBody();
                
                if (ok && _httpRequest._chunkState == HttpRequest::kExpectChunkBodyDone) {
                    _httpRPS = HttpRequestParseState::kGotAll;
                } else if (!ok) {
                    _httpRPS = HttpRequestParseState::kParseError;
                } else {
                    hasMore = false; // 等待更多数据
                }
            }
            // 2. 检查是否为 Content-Length 传输 (定长)
            else {
                // 原有的 Content-Length 逻辑
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
                            // 注意：对于 Content-Length 消息体，我们应该使用 append 而非 setBody，
                            // 但由于我们目前假定请求Body较小，继续使用 setBody。
                            _httpRequest.setBody(body_content); 
                            _inBuffer.retrieve((size_t)content_length);
                            _httpRPS=HttpRequestParseState::kGotAll;
                        } else {
                            hasMore=false; // 数据不足，等待
                        }
                    } else {
                        // Content-Length 为 0 或缺失 (且不是 chunked)
                        _httpRPS=HttpRequestParseState::kGotAll;
                    }
                }
                if(_httpRPS==HttpRequestParseState::kGotAll||!ok){
                hasMore=false;
                }
            }
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
    _httpResponse.reset();
    const std::string& conn_value = _httpRequest.getHeader("connection");
    bool keepAlive = (conn_value == "keep-alive");
    _httpResponse.setCloseConnection(!keepAlive); 
    
    // 检查 GET 和 HEAD 方法
    if (_httpRequest.getMethod() == HttpMethod::kGet || _httpRequest.getMethod() == HttpMethod::kHead) {
        
        std::cout << "Received " 
                  << (_httpRequest.getMethod() == HttpMethod::kHead ? "HEAD" : "GET") 
                  << " request for path: " << _httpRequest.getPath() << std::endl;
        
        // 1. 运行 handleGetRequest：
        //    - 执行文件安全检查
        //    - 准备零拷贝状态 (_fileFd, Content-Length Header)
        handleGetRequest(); 
        
        // 2. 如果是 HEAD 请求，必须清除响应体 (但保留 Content-Length Header)
        if (_httpRequest.getMethod() == HttpMethod::kHead) {
            _httpResponse.setBody(""); 
        }

    } else {
        // 其他 Method，返回 405
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
                
                // 1. 迭代解析十六进制大小
                const char* current = begin_read;
                for (; current < crlf; ++current) {
                    int digit = hexToDecimal(*current); 
                    
                    if (digit != -1) { // 假设 hexToDecimal 返回 -1 表示不是十六进制数字
                        chunk_size_val = chunk_size_val * 16 + digit;
                        hex_found = true;
                    } else if (*current == ' ' || *current == ';') {
                        // 遇到空格或分号，表示十六进制数结束（后面是扩展名或空格）
                        break;
                    } else if (isxdigit(*current)) { 
                         // 如果 hexToDecimal 没有处理大小写，这里需要额外的检查，
                         // 但如果 digit == -1 已经处理了非十六进制，这里可以简化。
                         // 保持 robust，如果遇到非十六进制且不是空格或分号，则格式错误。
                         ok = false; 
                         break;
                    }
                }
                
                // 2. 检查解析结果和格式错误
                if (!ok || !hex_found) { // 如果解析过程中出错了或根本没有找到数字
                    _httpRequest._chunkSize = 0;
                    ok = false;
                    break;
                }
                
                // 3. 更新状态变量
                _httpRequest._chunkSize = chunk_size_val;
                
                // 4. 消耗块大小信息行 (从 begin_read 到 crlf + 2)
                _inBuffer.retrieve((crlf - begin_read) + 2);

                // 5. 状态机迁移
                if (_httpRequest._chunkSize > 0) {
                    _httpRequest._chunkState = HttpRequest::kExpectChunkData;
                } else {
                    // 块大小为 0，表示消息体结束 (需要处理 Footer)
                    _httpRequest._chunkState = HttpRequest::kExpectChunkBodyDone;
                }
                break;}
            case HttpRequest::kExpectChunkData:{
            size_t chunk_len = _httpRequest._chunkSize;
                
                // 修正: 发现数据不足时，必须返回 true 暂停解析
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
                if(line_length==0)return true;
                break;}
            default:{
                // 不应该到达这里
                ok = false;
                break;}
        }
    }while (ok && _httpRequest._chunkState != HttpRequest::kExpectChunkBodyDone);
    return ok;
}