// server.c
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <ctype.h>
#include <sys/stat.h>

#define BIND_IP_ADDR "127.0.0.1"
#define BIND_PORT 8000
#define MAX_RECV_LEN 1048576
#define MAX_SEND_LEN 1048576
#define MAX_PATH_LEN 1024
#define MAX_HOST_LEN 1024
#define MAX_CONN 150
#define THREAD_POOL_SIZE 150

#define HTTP_STATUS_200 "200 OK"
#define HTTP_STATUS_404 "404 Not Found"
#define HTTP_STATUS_500 "500 Internal Server Error"

// 线程池结构体
typedef struct thread_pool {
    pthread_t threads[THREAD_POOL_SIZE]; // 线程数组
    int queue[MAX_CONN]; // 任务队列
    int head; // 任务队列的头部
    int tail; // 任务队列的尾部
    int clnt_socks[MAX_CONN];
    int clnt_cnt;
    sem_t sem_queue; // 任务队列的信号量
    pthread_mutex_t mutex_queue; // 互斥锁，用于保护任务队列的并发访问
    pthread_cond_t queue_not_full; // 条件变量，用于判断任务队列是否已满
    pthread_cond_t queue_not_empty; // 条件变量，用于判断任务队列是否为空
    int shutdown;
} thread_pool_t;

thread_pool_t pool;

int serv_sock;
int clnt_sock;

void divide_request(char *req, ssize_t req_len, char *method,char *url,char *version,char *host)
{
    ssize_t s1 = 0;
    while(s1 < req_len && req[s1] != ' ') {
        method[s1] = req[s1];
        s1++;
    }
    method[s1] = '\0';
    ssize_t s2 = s1 + 1;
    while(s2 < req_len && req[s2] != ' ') {
        url[s2 - s1 - 1] = req[s2];
        s2++;
    }
    url[s2 - s1 - 1] = '\0';
    ssize_t s3 = s2 + 1;
    while(s3 < req_len && req[s3] != '\n') {
        version[s3 - s2 - 1] = req[s3];
        s3++;
    }
    version[s3 - s2 - 1] = '\n';
    version[s3- s2] = '\0';
    ssize_t s4 = s3 + 1;
    while(s4 < req_len && req[s4] != '\n') {
        host[s4 - s3 - 1] = req[s4];
        s4++;
    }
    host[s4 - s3 - 1] = '\n';
    host[s4 - s3] = '\0';
}

// 解析 HTTP 请求
int parse_request(char* request, ssize_t req_len, char* path, ssize_t* path_len, char *version)
{
    char* req = request;
    char *method = (char*) malloc(MAX_RECV_LEN * sizeof(char));
    char *url = (char*) malloc(MAX_RECV_LEN * sizeof(char));
    char *host = (char *)malloc(MAX_RECV_LEN * sizeof(char));
    //char *version = (char*) malloc(MAX_RECV_LEN * sizeof(char));

    divide_request(req, req_len, method, url, version, host);
    ssize_t url_len = strlen(url);
    //ssize_t ver_len = strlen(version);
    // 如果请求的方法不为GET/请求的URL不以'/'开头/请求的版本号不为HTTP/开头或不以\r\n结尾，则返回错误
    if (strcmp(method,"GET") != 0 || url[0] != '/' ||
        (strncmp(version,"HTTP/1.0\r\n",10) != 0 && strncmp(version, "HTTP/1.1\r\n",10) != 0) ||
        strncmp(host, "Host: 127.0.0.1:8000\r\n", 22) != 0) {
        return -1;
    }
    /*else if(!(version[ver_len-2] == '\r' && version[ver_len-1] == '\n')) {
        return -1;
    }*/
    memcpy(path,url,url_len+1);
    *path_len = url_len;
    return 0;
}

int get_content(char *path, long *file_size, char **content)
{
    char cur_dir[MAX_PATH_LEN];
    if(getcwd(cur_dir,sizeof(cur_dir)) == NULL) {
        perror("getcwd error!\n");
        return -1;
    }
    char *file_path = (char*) malloc(MAX_PATH_LEN * 2 * sizeof(char));
    if(file_path == NULL) {
        perror("malloc error!\n");
        return -1;
    }

    if(strlen(cur_dir) + strlen(path) + 2 > 2*MAX_PATH_LEN) {
        free(file_path);
        return -1;
    }
    snprintf(file_path, 2*MAX_PATH_LEN, "%s%s", cur_dir, path);

    *content = NULL;

    struct stat path_stat;
    if(stat(file_path, &path_stat) == -1) {
        return -2;
    }
    if(S_ISDIR(path_stat.st_mode)) {
        fprintf(stderr, "%s is a directory!\n", file_path);
        return -1;
    }
    if(access(file_path, F_OK) == -1) {
        // 请求资源不存在时，返回-2
        perror("access error!\n");
        return -2;
    }

    // 判断访问路径是否跳出当前路径
    char *real_path = realpath(file_path, NULL);
    if(real_path == NULL) {
        perror("realpath error!\n");
        free(file_path);
        return -1;
    }
    // 检查绝对路径是否以当前目录为前缀
    if (strncmp(real_path, cur_dir, strlen(cur_dir)) != 0) {
        fprintf(stderr, "Error: Path traversal attempt detected\n");
        free(real_path);
        free(file_path);
        return -1;
    }
    
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        perror("fopen error!\n");
        free(file_path);
        return -2;
    }

    fseek(file, 0, SEEK_END);
    *file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    *content = (char*) malloc((*file_size + 1) * sizeof(char));
    if(*content == NULL) {
        perror("malloc error!\n");
        fclose(file);
        return -1;
    }

    if(fread(*content, 1, *file_size, file) < 0) {
        perror("read error!\n");
    }
    (*content)[*file_size] = '\0';

    fclose(file);

    free(file_path);

    return 0;
}

int write_response(char *response, int ret, int ret_2, long file_size, int clnt_sock) 
{
    if(ret == -1 || ret_2 == -1) {
        sprintf(response,
            "HTTP/1.0 %s \r\nContent-Length: 0\r\n\r\n",
            HTTP_STATUS_500);
    }
    else if(ret_2 == -2) {
        sprintf(response,
            "HTTP/1.0 %s \r\nContent-Length: 0\r\n\r\n",
            HTTP_STATUS_404);
    }
    else if(ret == 0 && ret_2 == 0) {
        sprintf(response,
            "HTTP/1.0 %s\r\nContent-Length: %zd\r\n\r\n",
            HTTP_STATUS_200, file_size);
    }
    ssize_t response_len = strlen(response);

    // 写入响应，直到所有数据都被写入
    ssize_t write_len = 0;
    while(response_len > 0) {
        if((write_len = write(clnt_sock, response, response_len)) < 0) {
            perror("write error!\n");
            return -1;
        }
        response += write_len;
        response_len -= write_len;
    }
    return 0;
}

void handle_clnt(int clnt_sock)
{
    // 读取客户端发送来的数据，并解析
    char* req_buf = (char*) malloc(MAX_RECV_LEN * sizeof(char));
    char* version = (char*) malloc(MAX_RECV_LEN * sizeof(char));

    ssize_t req_len = 0;
    ssize_t n = 0;
    ssize_t max_recv_len = MAX_RECV_LEN;
    // 读取请求，直到遇到 "\r\n\r\n"
    while (1) {
        n = read(clnt_sock, req_buf + req_len, max_recv_len - req_len);
        if(n < 0) {
            perror("read error!\n");
            free(req_buf);
            return;
        }
        req_len += n;
        if (req_len >= 4 && strcmp(req_buf + req_len - 4, "\r\n\r\n") == 0) {
            break;
        }
        if(req_len >= max_recv_len) {
            max_recv_len *= 2;
            char *new_req_buf = (char *)realloc(req_buf, max_recv_len * sizeof(char));
            if(!new_req_buf) {
                perror("realloc error!\n");
                free(req_buf);
                return;
            }
            req_buf = new_req_buf;
        }   
    }

    // 根据 HTTP 请求的内容，解析资源路径和 Host 头
    char* path = (char*) malloc(MAX_PATH_LEN * sizeof(char));
    ssize_t path_len;
    int ret = parse_request(req_buf, req_len, path, &path_len, version);
    long file_size;
    char* content = NULL;
    int ret_2 = get_content(path, &file_size, &content);
    // 构造要返回的数据
    // 注意，响应头部后需要有一个多余换行（\r\n\r\n），然后才是响应内容
    char* response = (char*) malloc(MAX_SEND_LEN * sizeof(char)) ;
    //size_t response_len;
    if(write_response(response, ret, ret_2, file_size, clnt_sock) == -1){
        free(content);
        goto end;
    }
    if(ret_2 == 0) {
        ssize_t content_len = strlen(content);
        while(content_len > 0) {
            ssize_t n = write(clnt_sock, content, content_len);
            if(n < 0) {
                perror("write error!\n");
                goto end;
            }
            content += n;
            content_len -= n;
        }
    }
    // 关闭客户端套接字
    close(clnt_sock);

    // 释放内存
end:
    free(req_buf);
    free(path);
    free(response);
    free(version);
    //free(content);
    return;
}

void *thread_func(void* arg)
{
    while (1)
    {
        // 等待信号量
        sem_wait(&pool.sem_queue);
        // 从任务队列中取出一个任务
        pthread_mutex_lock(&pool.mutex_queue);
        int clnt_sock = pool.queue[pool.head];
        pool.head = (pool.head + 1) % MAX_CONN;
        if((pool.tail + 1) % MAX_CONN != pool.head) {
            pthread_cond_signal(&pool.queue_not_full);
        }
        pthread_mutex_unlock(&pool.mutex_queue);
        if(pool.shutdown) {
            break;
        }
        // 处理任务
        handle_clnt(clnt_sock);
        close(clnt_sock);
    }
    return NULL;
}

void handle_signal(int sig)
{
    if(sig == SIGINT)
    {
        pthread_mutex_lock(&pool.mutex_queue);
        pool.shutdown = 1;
        for(int i = 0;i < THREAD_POOL_SIZE;i++)
        {
            pthread_cancel(pool.threads[i]);
            pthread_join(pool.threads[i],NULL);
        }
        for(int i = 0;i < pool.clnt_cnt;i++) {
            close(pool.clnt_socks[i]);
        }
        pthread_mutex_unlock(&pool.mutex_queue);
        // 销毁信号量和互斥锁
        sem_destroy(&pool.sem_queue);
        pthread_mutex_destroy(&pool.mutex_queue);

        // 关闭套接字
        close(serv_sock);
        
        // 打印换行符，使得输出更加美观
        printf("\n");
        //结束程序
        exit(0);
    }
}

int main(){
    // 注册信号处理函数
    signal(SIGINT, handle_signal);
    // 创建套接字，参数说明：
    //   AF_INET: 使用 IPv4
    //   SOCK_STREAM: 面向连接的数据传输方式
    //   IPPROTO_TCP: 使用 TCP 协议
    serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    // 将套接字和指定的 IP、端口绑定
    //   用 0 填充 serv_addr（它是一个 sockaddr_in 结构体）
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    //   设置 IPv4
    //   设置 IP 地址
    //   设置端口
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(BIND_IP_ADDR);
    serv_addr.sin_port = htons(BIND_PORT);
    //   绑定
    bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    // 使得 serv_sock 套接字进入监听状态，开始等待客户端发起请求
    listen(serv_sock, MAX_CONN);

    // 接收客户端请求，获得一个可以与客户端通信的新的生成的套接字 clnt_sock
    struct sockaddr_in clnt_addr;
    socklen_t clnt_addr_size = sizeof(clnt_addr);

    // 初始化线程池
    sem_init(&pool.sem_queue, 0, 0);
    pthread_mutex_init(&pool.mutex_queue, NULL);
    pthread_cond_init(&pool.queue_not_full, NULL);
    pthread_cond_init(&pool.queue_not_empty, NULL);
    pool.head = 0;
    pool.tail = 0;
    pool.clnt_cnt = 0;
    for(int i = 0;i < THREAD_POOL_SIZE;i++)
    {
        pthread_create(&pool.threads[i], NULL, thread_func, NULL);
    }

    while (1) // 一直循环
    {
        // 当没有客户端连接时，accept() 会阻塞程序执行，直到有客户端连接进来
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        // 处理客户端的请求
        //handle_clnt(clnt_sock);
        pthread_mutex_lock(&pool.mutex_queue);
        // 线程池已满时等待
        while((pool.tail + 1) % MAX_CONN == pool.head) {
            pthread_cond_wait(&pool.queue_not_full, &pool.mutex_queue);
        }
        pool.queue[pool.tail] = clnt_sock;
        pool.clnt_socks[pool.clnt_cnt++] = clnt_sock;
        pool.tail = (pool.tail + 1) % MAX_CONN;
        if(pool.head != pool.tail) {
            pthread_cond_signal(&pool.queue_not_empty);
        }
        pthread_mutex_unlock(&pool.mutex_queue);
        sem_post(&pool.sem_queue);
    }
}