
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
typedef struct cache {
    char uri[MAXLINE];
    char *object;  // char 배열 대신 char 포인터를 사용
    struct cache *next;
    struct cache *prev;
    int size;
} cache;

//  cache_size = 0;  // NOTE 상수 int
long cache_size = 0;
cache *head = NULL;
cache *tail = NULL;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
int parse_uri(char *uri, char *hostname, char *port, char *path);  // HTTP 요청의 URI를 파싱하는 함수입니다. URI에서 파일 경로, CGI 인자 등을 추출합니다.
void normalize_uri(char *uri);
void read_requesthdrs(rio_t *rp);
void doit(int clientfd);
void *thread(void *vargp);
long parse_content_length(const char *str);
cache *cache_find(const char *uri);
void cache_insert(const char *uri, char *port, char *path, char *object, int size);
void cache_remove(cache *cache_item);

// cache structure

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char **argv) {
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

    normalize_uri(uri);
    cache *cache_item = cache_find(uri);
    // printf cache_item
    // printf uri
    printf("uri: %s\n", uri);
    printf("Cache item: %s\n", cache_item);

    if (cache_item != NULL) {
        Rio_writen(clientfd, cache_item->object, cache_item->size);
        printf("Cache hit for %s\n", uri);
        Free(temp_cache);
        return;
    }
    printf("Cache miss for %s\n", uri);
    parse_uri(uri, hostname, port, path);
    sprintf(buf, "%s %s %s\r\nConnection: close\r\nProxy-Connection: close\r\n%s\r\n", method, path, "HTTP/1.0", user_agent_hdr);
    int serverfd = Open_clientfd(hostname, port);
    Rio_writen(serverfd, buf, strlen(buf));
    Rio_readinitb(&rio, serverfd);
    ssize_t n;
    while ((n = Rio_readlineb(&rio, buf, MAX_OBJECT_SIZE)) > 0) {
        Rio_writen(clientfd, buf, n);
        if (temp_cache_size + n <= MAX_OBJECT_SIZE) {
            memcpy(temp_cache, buf, n);
            temp_cache_size += n;
        }
    }
    if (temp_cache_size > 0 && temp_cache_size <= MAX_OBJECT_SIZE) {
        // print port
        printf("Port: %s\n", port);
        cache_insert(uri, port, path, temp_cache, temp_cache_size);
    } else {
        Free(temp_cache);
    }
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

// cache find
cache *cache_find(const char *uri) {
    pthread_mutex_lock(&mutex);
    printf("Finding cache for %s\n", uri);
    cache *current = head;
    while (current != NULL) {
        if (strcasecmp(current->uri, uri) == 0) {
            pthread_mutex_unlock(&mutex);
            printf("Cache hit!\n");
            return current;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&mutex);
    printf("Cache miss!\n");
    return NULL;
}

// void cache_insert(const char *uri, char *port, char *path, char *object, int size) {
//     pthread_mutex_lock(&mutex);
//     if (cache_size + size > MAX_CACHE_SIZE) {
//         while (cache_size + size > MAX_CACHE_SIZE) {
//             cache_remove(tail);
//         }
//     }

//     cache *new_cache = Malloc(sizeof(cache));
//     if (new_cache == NULL) {
//         fprintf(stderr, "Memory allocation failed\n");
//         pthread_mutex_unlock(&mutex);
//         return;
//     }

//     strcpy(new_cache->uri, uri);
//     new_cache->object = object;
//     new_cache->size = size;
//     new_cache->next = head;
//     new_cache->prev = NULL;

//     // printf cache uri
//     printf("Cache uri: %s\n", new_cache->uri);
//     if (head != NULL) {
//         head->prev = new_cache;
//     }
//     head = new_cache;
//     if (tail == NULL) {
//         tail = new_cache;
//     }
//     cache_size += size;

//     pthread_mutex_unlock(&mutex);
// }
void cache_insert(const char *hostname, char *port, char *path, char *object, int size) {
    pthread_mutex_lock(&mutex);

    // 완전한 URI를 생성
    char full_uri[MAXLINE];
    snprintf(full_uri, MAXLINE, "%s:%s%s", hostname, port, path);

    if (cache_size + size > MAX_CACHE_SIZE) {
        // 필요한 경우 오래된 캐시 항목 제거
        while (cache_size + size > MAX_CACHE_SIZE) {
            cache_remove(tail);
        }
    }

    // 새로운 캐시 항목 메모리 할당 및 초기화
    cache *new_cache = Malloc(sizeof(cache));
    if (new_cache == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        pthread_mutex_unlock(&mutex);
        return;
    }

    // 캐시 항목 설정
    strcpy(new_cache->uri, full_uri);
    new_cache->object = object;
    new_cache->size = size;
    new_cache->next = head;
    new_cache->prev = NULL;

    if (head != NULL) {
        head->prev = new_cache;
    }
    head = new_cache;
    if (tail == NULL) {
        tail = new_cache;
    }

    cache_size += size;

    printf("Cache inserted: %s, size: %d\n", full_uri, size);

    pthread_mutex_unlock(&mutex);
}

// cache remove
void cache_remove(cache *cache_item) {
    if (cache_item->prev) {                         // 이전 노드가 있으면
        cache_item->prev->next = cache_item->next;  // 이전 노드의 next를 현재 노드의 next로 설정
    } else {                                        // 이전 노드가 없으면
        head = cache_item->next;                    // head를 현재 노드의 next로 설정
    }
    if (cache_item->next) {                         // 다음 노드가 있으면
        cache_item->next->prev = cache_item->prev;  // 다음 노드의 prev를 현재 노드의 prev로 설정
    } else {                                        // 다음 노드가 없으면
        tail = cache_item->prev;                    // tail을 현재 노드의 prev로 설정
    }
    cache_size -= cache_item->size;  // 캐시 사이즈 감소
    Free(cache_item->object);        // 캐시 아이템의 object 메모리 풀어주기
    Free(cache_item);                // 캐시 아이템 메모리 풀어주기
}

void normalize_uri(char *uri) {
    char *end = uri + strlen(uri) - 1;
    while (end > uri && *end == '/') {
        *end = '\0';  // Remove trailing slashes
        end--;
    }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body,
            "%s<body bgcolor="
            "ffffff"
            ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}
