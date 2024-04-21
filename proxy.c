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
void doit(int clientfd) {
    char port[MAXLINE], buf[MAXLINE], method[MAXLINE], uri[MAXLINE], path[MAXLINE], hostname[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, clientfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    printf("Request line: %s\n", buf);  // 요청 라인 출력
    sscanf(buf, "%s %s", method, uri);
    printf("Method: %s, URI: %s\n", method, uri);  // 메소드와 URI 출력

    parse_uri(uri, hostname, port, path);
    printf("Parsed hostname: %s, Port: %s, Path: %s\n", hostname, port, path);  // 파싱 결과 출력

    sprintf(buf, "%s %s %s\r\n", method, path, "HTTP/1.0");
    printf("%s\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n", buf);
    sprintf(buf, "%s%s\r\n", buf, user_agent_hdr);

    int serverfd = Open_clientfd(hostname, port);
    printf("%s\n", buf);
    Rio_writen(serverfd, buf, strlen(buf));
    Rio_readinitb(&rio, serverfd);
    ssize_t n;
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) > 0) {
        printf("%s\n", buf);
        Rio_writen(clientfd, buf, n);
        if (strcmp(buf, "\r\n") == 0) {
            break;  // 응답 헤더 끝
        }
    }

    /* 응답 본문 전송 */
    while ((n = Rio_readlineb(&rio, buf, MAX_OBJECT_SIZE)) > 0) {
        Rio_writen(clientfd, buf, n);
    }
    Close(serverfd);
}
int parse_uri(char *uri, char *hostname, char *port, char *path) {
    // 프로토콜 제거 (http:// 또는 https://)

        char *protocol_end = strstr(uri, "://");
    char *host_start = (protocol_end) ? protocol_end + 3 : uri + 1;

    char *port_ptr = strchr(host_start + 1, ':');
    char *path_ptr = strchr(host_start + 1, '/');

    // 경로 설정
    if (path_ptr <= 0) {
        strcpy(path, "/");  // 경로가 없으면 기본 경로 "/"
    } else {
        strcpy(path, path_ptr);  // 경로 복사
        *path_ptr = '\0';        // 경로를 분리하기 위해 임시 종료
    }
    printf("%c\n", port_ptr);
    if (port_ptr > 0) {
        *port_ptr = '\0';
        strcpy(port, port_ptr + 1);
    }
    strcpy(hostname, host_start);

    printf("Parsed hostname: %s, port: %s, path: %s\n", hostname, port, path);
    return 0;
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
