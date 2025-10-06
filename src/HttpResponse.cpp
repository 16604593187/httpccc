#include "HttpResponse.h"
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
    activeline+=_statusMessage;
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