#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"
#include <sys/uio.h>

class HTTPConn
{
public:
    static const int FILENAME_LEN = 200;        // 文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048;   // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;  // 写缓冲区的大小
    
    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    HTTPConn(){}
    ~HTTPConn(){}
public:
    // 初始化新接受的连接
    void init(int sockfd, const sockaddr_in& addr); 

    // 关闭连接
    void closeConn();  

    // 处理客户端请求
    void process(); 

    // 非阻塞读
    bool read();

    // 非阻塞写
    bool write();

private:
    // 初始化连接
    void init();    

    // 解析HTTP请求
    HTTP_CODE processRead();    

    // 填充HTTP应答
    bool processWrite(HTTP_CODE ret);    

    // 下面这一组函数被processRead调用以分析HTTP请求
    HTTP_CODE parseRequestLine(char* text);
    HTTP_CODE parseHeaders(char* text);
    HTTP_CODE parseContent(char* text);
    HTTP_CODE doRequest();
    char* getLine() {return readBuffer + startLine;}
    LINE_STATUS parseLine();

    // 这一组函数被process_write调用以填充HTTP应答。
    void unmap();
    bool addResponse(const char* format, ...);
    bool addContent(const char* content);
    bool addContentType();
    bool addStatusLine(int status, const char* title);
    bool addHeaders(int content_length);
    bool addContentLength(int content_length);
    bool addLinger();
    bool addBlankLine();

public:
    // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
    static int epollFd;

    // 统计用户的数量
    static int userCount;    

private:
    // 该HTTP连接的socket和对方的socket地址
    int socketFd;
    sockaddr_in address;

    // 读缓冲区
    char readBuffer[READ_BUFFER_SIZE];

    // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    int readIndex; 

    // 当前正在分析的字符在读缓冲区中的位置                    
    int checkedIndex;   

    // 当前正在解析的行的起始位置
    int startLine;    

    // 主状态机当前所处的状态
    CHECK_STATE checkState;  

    // 请求方法            
    METHOD httpMethod;

    // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    char realFile[FILENAME_LEN];    

    // 客户请求的目标文件的文件名   
    char* url;       

    // HTTP协议版本号，目前仅支持HTTP1.1
    char* httpVersion;                  

    // 主机名       
    char* hostName;   

    // HTTP请求的消息总长度                        
    int contentLength;   

    // HTTP请求是否要求保持连接
    bool linger;                          

    // 写缓冲区
    char writeBuffer[WRITE_BUFFER_SIZE];  

    // 写缓冲区中待发送的字节数
    int writeIndex;

    // 客户请求的目标文件被mmap到内存中的起始位置                        
    char* fileAddress; 

    // 目标文件的状态。通过它可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct stat fileStat;  

    // 采用writev来执行写操作，所以定义下面两个成员，其中ivCount表示被写内存块的数量。              
    struct iovec iv[2];                   
    int ivCount;
};

#endif
