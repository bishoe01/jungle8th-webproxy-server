#include <stdio.h>

#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
int parse_uri(char *uri, char *hostname, char *port, char *path);  // HTTP 요청의 URI를 파싱하는 함수입니다. URI에서 파일 경로, CGI 인자 등을 추출합니다.
void read_requesthdrs(rio_t *rp);

void doit(int clientfd) {
    int is_static;
    int serverfd;
    struct stat sbuf;

    char port[MAXLINE], buf[MAXLINE], method[MAXLINE], uri[MAXLINE], path[MAXLINE], hostname[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, clientfd);
    Rio_readlineb(&rio, buf, MAXLINE);  // NOTE - GET 요청의 경우에는 이렇게 method, uri , port정도로 충분히 요청이 가능
    printf("Request headers:\n %s\n", buf);
    sscanf(buf, "%s %s", method, uri);
    printf("URI received: %s\n", uri);

    parse_uri(uri, hostname, port, path);
    printf("____________________\n");
    printf("Parsed method: %s, URI: %s, Port: %s, Path: %s\n", method, uri, port, path);

    sprintf(buf, "%s %s %s\r\n", method, path, "HTTP/1.0");
    strcat(buf, "Proxy-Connection: close\r\n");
    strcat(buf, "Connection: close\r\n");
    strcat(buf, user_agent_hdr);
    strcat(buf, "\r\n");

    if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
        printf("Unsupported HTTP method: %s\n", method);
        return;
    }

    serverfd = Open_clientfd(hostname, port);
    printf("Server file descriptor: %d\n", serverfd);

    if (serverfd == -1) {
        printf("Failed to connect to the server. ERROR\n");
        return;
    }

    printf("Final request buffer being sent to the server:\n%s\n", buf);
    Rio_writen(serverfd, buf, strlen(buf));  // Send the modified request to the server

    Rio_readinitb(&rio, serverfd);  // Reinitialize buffer for the server response
    while (Rio_readlineb(&rio, buf, MAXLINE) > 0) {
        printf("Received from server: %s", buf);
        Rio_writen(clientfd, buf, strlen(buf));  // Send server response back to the client
    }

    Close(serverfd);
}

// int parse_uri(char *uri, char *hostname, char *port, char *path) {
//     char *hostname_ptr = strstr(uri, "//");
//     if (!hostname_ptr) {
//         hostname_ptr = uri;
//     } else {
//         hostname_ptr += 2;
//     }

//     char *port_ptr = strchr(hostname_ptr, ':');
//     char *path_ptr = strchr(hostname_ptr, '/');
//     if (!path_ptr) {
//         path_ptr = hostname_ptr + strlen(hostname_ptr);  // URI 끝 설정
//         strcpy(path, "/");
//     } else {
//         strcpy(path, path_ptr);
//     }

//     if (port_ptr && port_ptr < path_ptr) {
//         strncpy(port, port_ptr + 1, path_ptr - port_ptr - 1);
//         port[path_ptr - port_ptr - 1] = '\0';
//         strncpy(hostname, hostname_ptr, port_ptr - hostname_ptr);
//         hostname[port_ptr - hostname_ptr] = '\0';
//     } else {
//         strcpy(port, "80");
//         if (path_ptr > hostname_ptr) {
//             strncpy(hostname, hostname_ptr, path_ptr - hostname_ptr);
//             hostname[path_ptr - hostname_ptr] = '\0';
//         } else {
//             strcpy(hostname, hostname_ptr);  // 경로나 포트 없이 호스트네임만 있는 경우
//         }
//     }

//     printf("Parsed hostname: %s\n", hostname);
//     printf("Parsed port: %s\n", port);
//     printf("Parsed path: %s\n", path);

//     return 0;
// }
int parse_uri(char *uri, char *hostname, char *port, char *path) {
    char *hostname_ptr = uri;  // `//`는 생략하고 바로 uri 포인터를 사용

    // 경로를 기본값으로 설정
    strcpy(path, "/");

    // ':'와 '/' 찾기
    char *port_ptr = strchr(hostname_ptr, ':');
    char *path_ptr = strchr(hostname_ptr, '/');

    if (path_ptr) {
        *path_ptr = '\0';  // 경로 부분을 일단 끊기
    }

    if (port_ptr) {
        *port_ptr = '\0';                                          // 포트 부분을 끊기
        strcpy(port, port_ptr + 1);                                // 포트 번호 복사
        strncpy(hostname, hostname_ptr, port_ptr - hostname_ptr);  // 호스트네임 복사
        hostname[port_ptr - hostname_ptr] = '\0';
    } else {
        strcpy(port, "80");              // 기본 포트 설정
        strcpy(hostname, hostname_ptr);  // 호스트네임 복사
    }

    // 경로와 포트 부분을 원래대로 복구 (다른 처리를 위해)
    if (port_ptr) {
        *port_ptr = ':';
    }
    if (path_ptr) {
        *path_ptr = '/';
    }

    printf("Parsed hostname: %s\n", hostname);
    printf("Parsed port: %s\n", port);
    printf("Parsed path: %s\n", path);

    return 0;
}

int main(int argc, char **argv) {
    int listenfd, clientfd;
    char client_hostname[MAXLINE], client_port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);  // 전달받은 포트 번호를 사용해 수신 소켓 생성
    while (1) {
        clientlen = sizeof(clientaddr);
        clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // 클라이언트 연결 요청 수신
        Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", client_hostname, client_port);
        doit(clientfd);
        Close(clientfd);
    }
}

void read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("First line: %s", buf);  // 첫 줄 내용 확인
    while (strcmp(buf, "\r\n")) {
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}
