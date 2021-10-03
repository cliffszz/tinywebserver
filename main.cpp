#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include "httpConn.h"

// 最大的文件描述符个数
#define MAX_FD 65536   

// 监听的最大的事件数量
#define MAX_EVENT_NUMBER 10000  

// 添加文件描述符
extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);

// 添加信号处理函数
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

int main(int argc, char* argv[]) { 
    if(argc <= 1) {
        printf( "usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    int port = atoi(argv[1]);
    addsig(SIGPIPE, SIG_IGN);

    // 初始化线程池
    ThreadPool<HTTPConn>* pool = nullptr;
    try {
        pool = new ThreadPool<HTTPConn>;
    } catch( ... ) {
        return 1;
    }

    // 初始化客户数组
    HTTPConn* users = new HTTPConn[MAX_FD];

    // 创建用于监听的socket
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // 端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));

    // 监听，最大连接请求数为5
    ret = listen(listenfd, 5);

    // 创建epoll对象和事件数组
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    // 添加到epoll对象中
    addfd(epollfd, listenfd, false);
    HTTPConn::epollFd = epollfd;

    while(true) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
  
        if ((number < 0) && (errno != EINTR)) {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            
            if(sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
                
                if (connfd < 0) {
                    printf("errno is: %d\n", errno);
                    continue;
                } 

                if(HTTPConn::userCount >= MAX_FD) {
                    close(connfd);
                    continue;
                }
                users[connfd].init(connfd, client_address);

            } else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                users[sockfd].closeConn();

            } else if(events[i].events & EPOLLIN) {
                if(users[sockfd].read()) {
                    pool->appendRequest(users + sockfd);

                } else {
                    users[sockfd].closeConn();
                }

            }  else if(events[i].events & EPOLLOUT) {
                if(!users[sockfd].write()) {
                    users[sockfd].closeConn();
                }
            }
        }
    }
    
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}