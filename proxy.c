#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

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
void doit(int clientfd);
void *thread(void *vargp);
long parse_content_length(const char *str);
// cache structure
typedef struct cache {
    char uri[MAXLINE];
    char *object;  // char 배열 대신 char 포인터를 사용
    struct cache *next;
    int size;
} cache;

//  cache_size = 0;  // NOTE 상수 int
long cache_size = 0;
cache *head = NULL;
cache *tail = NULL;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char **argv) {
    volatile int cnt = 0;
    sem_t mutex, w;
    Sem_init(&mutex, 0, 1);

    int listenfd, *clientfd;
    char client_hostname[MAXLINE], client_port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    signal(SIGPIPE, SIG_IGN);           // broken pipe 에러 해결용 코드 -프로세스 전체에 대한 시그널 핸들러 설정
    listenfd = Open_listenfd(argv[1]);  // 전달받은 포트 번호를 사용해 수신 소켓 생성
    while (1) {
        clientlen = sizeof(clientaddr);
        clientfd = Malloc(sizeof(int));

        *clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // 클라이언트 연결 요청 수신

        Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);

        printf("Accepted connection from (%s, %s)\n", client_hostname, client_port);
        Pthread_create(&tid, NULL, thread, clientfd);
    }
}
void *thread(void *vargp) {
    int clientfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(clientfd);
    Close(clientfd);
    return NULL;
}

void doit(int clientfd) {
    char port[MAXLINE], buf[MAX_OBJECT_SIZE], method[MAXLINE], uri[MAXLINE], path[MAXLINE], hostname[MAXLINE];
    rio_t rio;
    int temp_cache_size = 0;
    char *temp_cache = Malloc(MAX_OBJECT_SIZE);
    if (temp_cache == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }

    Rio_readinitb(&rio, clientfd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) {
        Free(temp_cache);
        return;
    }

    sscanf(buf, "%s %s", method, uri);
    if (!strcasecmp(uri, "/favicon.ico")) {
        Free(temp_cache);
        return;
    }

    parse_uri(uri, hostname, port, path);
    sprintf(buf, "%s %s %s\r\nConnection: close\r\nProxy-Connection: close\r\n%s\r\n", method, path, "HTTP/1.0", user_agent_hdr);
    int serverfd = Open_clientfd(hostname, port);
    Rio_writen(serverfd, buf, strlen(buf));
    Rio_readinitb(&rio, serverfd);
    ssize_t n;
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) > 0) {
        Rio_writen(clientfd, buf, n);
        if (strcmp(buf, "\r\n") == 0) break;  // End of headers
    }
    // Cache the response body if possible
    while ((n = Rio_readnb(&rio, buf, MAX_OBJECT_SIZE)) > 0) {
        Rio_writen(clientfd, buf, n);
    }

    Free(temp_cache);
    Close(serverfd);
}

int parse_uri(char *uri, char *hostname, char *port, char *path) {
    // 프로토콜 제거 (http:// 또는 https://)

    char *protocol_end = strstr(uri, "://");
    // NOTE - uri가 /로 시작할때는 uri+1
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

long parse_content_length(const char *str) {
    long content_length = 0;
    int found_digit = 0;  // 숫자를 찾았는지 여부를 나타내는 플래그

    // 문자열을 처음부터 끝까지 반복
    for (; *str != '\0'; str++) {
        // 숫자인 경우
        if (isdigit(*str)) {
            found_digit = 1;  // 숫자를 찾았음을 표시
            // 현재 숫자를 누적하여 content_length에 추가
            content_length = content_length * 10 + (*str - '0');
        } else if (found_digit) {
            // 숫자를 찾았으나 숫자가 아닌 문자가 나타난 경우, 반복 종료
            break;
        }
    }

    return content_length;
}
