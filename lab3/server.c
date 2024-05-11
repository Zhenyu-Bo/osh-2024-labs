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
#define MAX_CONN 20
#define THREAD_POOL_SIZE 20

#define HTTP_STATUS_200 "200 OK"
#define HTTP_STATUS_404 "404 Not Found"
#define HTTP_STATUS_500 "500 Internal Server Error"

// 线程池结构体
typedef struct thread_pool {
    pthread_t threads[THREAD_POOL_SIZE]; // 线程数组
    int queue[MAX_CONN]; // 任务队列
    int head; // 任务队列的头部
    int tail; // 任务队列的尾部
    sem_t sem_queue; // 任务队列的信号量
    pthread_mutex_t mutex_queue; // 互斥锁，用于保护任务队列的并发访问
} thread_pool_t;

thread_pool_t pool;
int serv_sock;

// 去除首尾空格
void trim(char *s, ssize_t* path_len)
{
    char *ptr;
    while(isspace(*s)) s++;
    if(*s == 0) return ;
    ptr = s + strlen(s) - 1;
    while(ptr > s && isspace(*ptr)) ptr--;
    *(ptr + 1) = '\0';
    *path_len = ptr - s + 1;
    return ;
}

void divide_request(char *req, ssize_t req_len, char *method,char *url,char *version)
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
    strcpy(version,req+s2+1);
}

// 解析 HTTP 请求
int parse_request(char* request, ssize_t req_len, char* path, ssize_t* path_len, char *version)
{
    char* req = request;
    char *method = (char*) malloc(MAX_RECV_LEN * sizeof(char));
    char *url = (char*) malloc(MAX_RECV_LEN * sizeof(char));
    //char *version = (char*) malloc(MAX_RECV_LEN * sizeof(char));

    divide_request(req,req_len,method,url,version);
    ssize_t url_len = strlen(url);
    ssize_t ver_len = strlen(version);
    // 如果请求的方法不为GET/请求的URL不以'/'开头/请求的版本号不为HTTP/开头或不以\r\n结尾，则返回错误
    if(strcmp(method,"GET") != 0 || url[0] != '/' || ver_len < 2) {
        return -1;
    }
    else if(!(version[ver_len-2] == '\r' && version[ver_len-1] == '\n') || strncmp(version,"HTTP/1.0",8) != 0) {
        return -1;
    }
    memcpy(path,url,url_len+1);
    *path_len = url_len;
    return 0;
}

int get_content(char *path, long *file_size, char **content)
{
    char cur_dir[1024];
    if(getcwd(cur_dir,sizeof(cur_dir)) == NULL) {
        perror("getcwd error!\n");
        return -1;
    }
    char *file_path = (char*) malloc(MAX_PATH_LEN * sizeof(char));
    if(file_path == NULL) {
        //perror("malloc error!\n");
        return -1;
    }

    if(strlen(cur_dir) + strlen(path) + 2 > 2*MAX_PATH_LEN) {
        return -1;
    }
    snprintf(file_path, 2*MAX_PATH_LEN, "%s%s", cur_dir, path);

    struct stat path_stat;
    if(stat(file_path, &path_stat) == -1 || S_ISDIR(path_stat.st_mode)) {
        //perror("stat error!\n");
        return -1;
    }
    if(access(file_path, F_OK) == -1) {
        // 请求资源不存在时，返回-2
        //perror("access error!\n");
        return -2;
    }
    
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        //printf("file_path: %s\n", file_path);
        perror("fopen error!\n");
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

    fread(*content, 1, *file_size, file);
    (*content)[*file_size] = '\0';
    //printf("%s",content);

    fclose(file);

    free(file_path);

    return 0;
}

void handle_clnt(int clnt_sock)
{   
    // 处理任务时先休眠5秒，同时处理两个任务，观察两个任务是否能够同时响应来判断是否成功实现连接请求的多线程并发处理
    //sleep(5); 
    // 一个粗糙的读取方法，可能有 BUG！
    // 读取客户端发送来的数据，并解析
    char* req_buf = (char*) malloc(MAX_RECV_LEN * sizeof(char));
    char* version = (char*) malloc(MAX_RECV_LEN * sizeof(char));
    // 将 clnt_sock 作为一个文件描述符，读取最多 MAX_RECV_LEN 个字符
    // 但一次读取并不保证已经将整个请求读取完整
    ssize_t req_len = read(clnt_sock, req_buf, MAX_RECV_LEN);

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
    size_t response_len;
    
    if(ret == -1 || ret_2 == -1) {
        sprintf(response,
            "HTTP1.0 %s \r\nContent-Length: 0\r\n\r\n",
            HTTP_STATUS_500);
    }
    else if(ret_2 == -2) {
        sprintf(response,
            "HTTP1.0 %s \r\nContent-Length: 0\r\n\r\n",
            HTTP_STATUS_404);
    }
    else if(ret == 0 && ret_2 == 0) {
        sprintf(response,
            "HTTP/1.0 %s\r\nContent-Length: %zd\r\n\r\n%s",
            HTTP_STATUS_200, file_size, content);
    }
    response_len = strlen(response);
    // 通过 clnt_sock 向客户端发送信息
    // 将 clnt_sock 作为文件描述符写内容
    write(clnt_sock, response, response_len);

    // 关闭客户端套接字
    close(clnt_sock);

    // 释放内存
    free(req_buf);
    free(path);
    free(response);
    free(version);
    free(content);
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
        pthread_mutex_unlock(&pool.mutex_queue);
        // 处理任务
        handle_clnt(clnt_sock);
    }
    return NULL;
}

void handle_signal(int sig)
{
    if(sig == SIGINT)
    {
        for(int i = 0;i < THREAD_POOL_SIZE;i++)
        {
            pthread_cancel(pool.threads[i]);
            pthread_join(pool.threads[i],NULL);
        }

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
    //int serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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
    pool.head = 0;
    pool.tail = 0;
    for(int i = 0;i < THREAD_POOL_SIZE;i++)
    {
        pthread_create(&pool.threads[i], NULL, thread_func, NULL);
    }

    while (1) // 一直循环
    {
        // 当没有客户端连接时，accept() 会阻塞程序执行，直到有客户端连接进来
        int clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        // 处理客户端的请求
        //handle_clnt(clnt_sock);
        pthread_mutex_lock(&pool.mutex_queue);

        if((pool.tail + 1) % MAX_CONN == pool.head) {
            // 队列已满时拒绝新的连接
            close(clnt_sock);
        } else {
            pool.queue[pool.tail] = clnt_sock;
            pool.tail = (pool.tail + 1) % MAX_CONN;
            pthread_mutex_unlock(&pool.mutex_queue);
            sem_post(&pool.sem_queue);
        }   
    }

    // 实际上这里的代码不可到达，可以在 while 循环中收到 SIGINT 信号时主动 break
    // 关闭套接字
    //close(serv_sock);
    //return 0;
}