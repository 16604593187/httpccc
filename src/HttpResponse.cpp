#include "HttpResponse.h"
static const std::unordered_map<int, const char*> s_status_messages = {
    {100, "Continue"},
    {200, "OK"},
    {204, "No Content"},
    {301, "Moved Permanently"},
    // ...
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {500, "Internal Server Error"},
    {501, "Not Implemented"}
};
const char* HttpResponse::getStatusMessage(int code){
    auto it = s_status_messages.find(code); 
    
    if (it != s_status_messages.end()) {
        return it->second; //返回消息字符串
    } else {
        return "Unknown Status"; //返回默认消息
    }
}
void HttpResponse::setStatusCode(int code,const std::string& message){
    _statusCode=code;
    _statusMessage=message;
}
void HttpResponse::setHeader(const std::string& field,const std::string& value){
    _headers[field]=value;
}
void HttpResponse::setBody(const std::string& body){
    _body=body;
}
void HttpResponse::reset(){
    _statusCode=0;
    _statusMessage.clear();
    _headers.clear();
    _body.clear();
    _closeConnection=false;
}
void HttpResponse::appendToBuffer(Buffer* outputBuffer){
    //动态处理Headers
    size_t body_len=_body.size();
    if(body_len>0){
        setHeader("Content-Length",std::to_string(body_len));
    }
    if(_closeConnection)setHeader("Connection","close");
    else setHeader("Connection","keep-alive");

    //组装状态行
    std::string activeline="HTTP/1.1 ";
    activeline+=std::to_string(_statusCode);
    activeline+=" ";
    activeline+=getStatusMessage(_statusCode);
    activeline+="\r\n";
    outputBuffer->append(activeline.data(),activeline.size());
    for(const auto& pair:_headers){
        std::string headerLine=pair.first;
        headerLine+=": ";
        headerLine+=pair.second;
        headerLine+="\r\n";
        outputBuffer->append(headerLine.data(),headerLine.size());
    }
    outputBuffer->append("\r\n",2);
    if(body_len>0)outputBuffer->append(_body.data(),_body.size());
    
}
