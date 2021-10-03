#include "httpConn.h"

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char* docRoot = "/home/tinywebsever/resources";

// 设置文件描述符为非阻塞
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 向epoll中添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot) {
    // 创建一个epoll事件
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;

    // 注册epoll oneshot 事件
    if(one_shot) 
    {
        // 防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT;
    }

    // 向epollfd中添加fd
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    // 设置文件描述符非阻塞
    setnonblocking(fd);  
}

// 从epollfd中移除监听的fd
void removefd(int epollfd, int fd) {
    // 从epollfd中移除监听的fd
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);

    // 关闭fd
    close(fd);
}

// 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev) {
    // 重置EPOLLONESHOT事件
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;

    // 修改epollfd中监听的fd
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

// 所有的客户数
int HTTPConn::userCount = 0;

// 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
int HTTPConn::epollFd = -1;

// 关闭连接
void HTTPConn::closeConn() {
    if(socketFd != -1) {
        // 将socket从epoll中移除
        removefd(epollFd, socketFd);
        socketFd = -1;

        // 关闭一个连接，将客户总数量减一
        userCount--; 
    }
}

// 初始化连接,外部调用初始化套接字地址
void HTTPConn::init(int socketfd, const sockaddr_in& addr){
    socketFd = socketfd;
    address = addr;
    
    // 端口复用
    int reuse = 1;
    setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 将fd添加epoll监听
    addfd(epollFd, socketfd, true );

    // 用户数加一
    userCount++;

    init();
}

// 默认初始化连接
void HTTPConn::init()
{
    // 初始状态为检查请求行
    checkState = CHECK_STATE_REQUESTLINE;    

    // 默认不保持链接  Connection : keep-alive保持连接
    linger = false;       

    // 默认请求方式为GET
    httpMethod = GET;         
    url = 0;              
    httpVersion = 0;
    contentLength = 0;
    hostName = 0;
    startLine = 0;
    checkedIndex = 0;
    readIndex = 0;
    writeIndex = 0;

    // 缓冲区清零
    bzero(readBuffer, READ_BUFFER_SIZE);
    bzero(writeBuffer, READ_BUFFER_SIZE);
    bzero(realFile, FILENAME_LEN);
}

// 循环读取客户数据，直到无数据可读或者对方关闭连接
bool HTTPConn::read() {
    if(readIndex >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytesRead = 0;

    // 循环读取数据
    while(true) {
        // 从readBuffer + readIndex索引处开始保存数据，大小是READ_BUFFER_SIZE - readIndex
        bytesRead = recv(socketFd, readBuffer + readIndex, READ_BUFFER_SIZE - readIndex, 0);
        if (bytesRead == -1) {
            if( errno == EAGAIN || errno == EWOULDBLOCK ) {
                // 没有数据
                break;
            }
            return false;   
        } else if (bytesRead == 0) {   
            // 对方关闭连接
            return false;
        }
        readIndex += bytesRead;
    }
    return true;
}

// 解析一行，判断依据\r\n
HTTPConn::LINE_STATUS HTTPConn::parseLine() {
    char temp;
    for (; checkedIndex < readIndex; checkedIndex++) {
        temp = readBuffer[checkedIndex];
        if (temp == '\r') {
            if ((checkedIndex + 1) == readIndex ) {
                return LINE_OPEN;
            } else if (readBuffer[checkedIndex + 1] == '\n' ) {
                readBuffer[checkedIndex++] = '\0';
                readBuffer[checkedIndex++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if( temp == '\n' )  {
            if((checkedIndex > 1) && (readBuffer[checkedIndex - 1] == '\r' ) ) {
                readBuffer[checkedIndex - 1] = '\0';
                readBuffer[checkedIndex++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 解析HTTP请求行，获得请求方法，目标URL,以及HTTP版本号
HTTPConn::HTTP_CODE HTTPConn::parseRequestLine(char* text) {
    // GET /index.html HTTP/1.1
    url = strpbrk(text, " \t"); // 判断第二个参数中的字符哪个在text中最先出现
    if (!url) { 
        return BAD_REQUEST;
    }

    // GET\0/index.html HTTP/1.1
    // 置位空字符，字符串结束符
    *url++ = '\0';    

    char* method = text;
    if (strcasecmp(method, "GET") == 0) { 
        // 忽略大小写比较
        httpMethod = GET;
    } else {
        return BAD_REQUEST;
    }

    // /index.html HTTP/1.1
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    httpVersion = strpbrk(url, " \t");
    if (!httpVersion) {
        return BAD_REQUEST;
    }
    *httpVersion++ = '\0';
    if (strcasecmp(httpVersion, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    if (strncasecmp(url, "http://", 7) == 0 ) {   
        url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        url = strchr(url, '/');
    }
    if (!url || url[0] != '/' ) {
        return BAD_REQUEST;
    }

    // 检查状态变成检查头
    checkState = CHECK_STATE_HEADER; 
    return NO_REQUEST;
}

// 解析HTTP请求的一个头部信息
HTTPConn::HTTP_CODE HTTPConn::parseHeaders(char* text) {   
    // 遇到空行，表示头部字段解析完毕
    if(text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if (contentLength != 0) {
            checkState = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            linger = true;
        }
    } else if (strncasecmp( text, "Content-Length:", 15) == 0) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        contentLength = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        hostName = text;
    } else {
        printf("oop! unknow header %s\n", text);
    }
    return NO_REQUEST;
}

// 没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
HTTPConn::HTTP_CODE HTTPConn::parseContent( char* text ) {
    if (readIndex >= (contentLength + checkedIndex))
    {
        text[contentLength] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 主状态机，解析请求
HTTPConn::HTTP_CODE HTTPConn::processRead() {
    LINE_STATUS lineStatus = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    while (((checkState == CHECK_STATE_CONTENT) && (lineStatus == LINE_OK)) || ((lineStatus = parseLine()) == LINE_OK)) {
        // 获取一行数据
        text = getLine();
        startLine = checkedIndex;
        printf("got 1 http line: %s\n", text);

        switch (checkState) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parseRequestLine(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parseHeaders(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {
                    return doRequest();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parseContent(text);
                if (ret == GET_REQUEST) {
                    return doRequest();
                }
                lineStatus = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}


// 对内存映射区执行munmap操作
void HTTPConn::unmap() {
    if(fileAddress)
    {
        munmap(fileAddress, fileStat.st_size );
        fileAddress = 0;
    }
}

// 写HTTP响应
bool HTTPConn::write()
{
    int temp = 0;
    int bytes_have_send = 0;    // 已经发送的字节
    int bytes_to_send = writeIndex;// 将要发送的字节writeIndex写缓冲区中待发送的字节数
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd(epollFd, socketFd, EPOLLIN); 
        init();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(socketFd, iv, ivCount);
        if (temp <= -1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd(epollFd, socketFd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if ( bytes_to_send <= bytes_have_send ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if(linger) {
                init();
                modfd(epollFd, socketFd, EPOLLIN );
                return true;
            } else {
                modfd(epollFd, socketFd, EPOLLIN );
                return false;
            } 
        }
    }
}

// 往写缓冲中写入待发送的数据
bool HTTPConn::addResponse(const char* format, ...) {
    if(writeIndex >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list argList;
    va_start(argList, format);
    int len = vsnprintf(writeBuffer + writeIndex, WRITE_BUFFER_SIZE - 1 - writeIndex, format, argList );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - writeIndex ) ) {
        return false;
    }
    writeIndex += len;
    va_end(argList);
    return true;
}

bool HTTPConn::addStatusLine(int status, const char* title) {
    return addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HTTPConn::addHeaders(int content_len) {
    return addContentLength(content_len) && addContentType() && addLinger() && addBlankLine();
}

bool HTTPConn::addContentLength(int content_len) {
    return addResponse("Content-Length: %d\r\n", content_len);
}

bool HTTPConn::addLinger()
{
    return addResponse( "Connection: %s\r\n", (linger == true ) ? "keep-alive" : "close" );
}

bool HTTPConn::addBlankLine()
{
    return addResponse("%s", "\r\n");
}

bool HTTPConn::addContent(const char* content)
{
    return addResponse("%s", content);
}

bool HTTPConn::addContentType() {
    return addResponse("Content-Type:%s\r\n", "text/html");
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool HTTPConn::processWrite(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            addStatusLine(500, error_500_title);
            addHeaders(strlen(error_500_form));
            if (!addContent(error_500_form)) {
                return false;
            }
            break;
        case BAD_REQUEST:
            addStatusLine(400, error_400_title);
            addHeaders(strlen(error_400_form));
            if (!addContent(error_400_form)) {
                return false;
            }
            break;
        case NO_RESOURCE:
            addStatusLine(404, error_404_title);
            addHeaders(strlen(error_404_form));
            if (!addContent(error_404_form)) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            addStatusLine(403, error_403_title);
            addHeaders(strlen(error_403_form));
            if (!addContent(error_403_form)) {
                return false;
            }
            break;
        case FILE_REQUEST:
            addStatusLine(200, ok_200_title);
            addHeaders(fileStat.st_size);
            iv[0].iov_base = writeBuffer;
            iv[0].iov_len = writeIndex;
            iv[1].iov_base = fileAddress;
            iv[1].iov_len = fileStat.st_size;
            ivCount = 2;
            return true;
        default:
            return false;
    }

    iv[0].iov_base = writeBuffer;
    iv[0].iov_len = writeIndex;
    ivCount = 1;
    return true;
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void HTTPConn::process() {
    // 解析HTTP请求
    HTTP_CODE read_ret = processRead();
    if (read_ret == NO_REQUEST) {
        modfd(epollFd, socketFd, EPOLLIN);
        return;
    }
    
    // 生成响应
    bool write_ret = processWrite(read_ret);
    if (!write_ret) {
        closeConn();
    }
    modfd(epollFd, socketFd, EPOLLOUT);
}

// 当得到一个完整、正确的HTTP请求时，就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其映射到内存地址fileAddress处，并告诉调用者获取文件成功
HTTPConn::HTTP_CODE HTTPConn::doRequest()
{
    strcpy(realFile, docRoot);
    int len = strlen(docRoot);
    strncpy(realFile + len, url, FILENAME_LEN - len - 1);

    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if (stat(realFile, &fileStat) < 0) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if (!(fileStat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(fileStat.st_mode)) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(realFile, O_RDONLY);
    // 创建内存映射

    fileAddress = (char*)mmap(0, fileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}