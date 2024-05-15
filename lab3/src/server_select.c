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
#include <sys/select.h>
#include <errno.h>

#define BIND_IP_ADDR "127.0.0.1"
#define BIND_PORT 8000
#define MAX_RECV_LEN 1048576
#define MAX_SEND_LEN 1048576
#define MAX_PATH_LEN 1024
#define MAX_HOST_LEN 1024
#define MAX_CONN 100
#define BUFFER_SIZE 4096

#define HTTP_STATUS_200 "200 OK"
#define HTTP_STATUS_404 "404 Not Found"
#define HTTP_STATUS_500 "500 Internal Server Error"

int serv_sock;

void print_perror(char *str) {
    perror(str);
    exit(1);
}

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
    ssize_t host_len = strlen(host);
    //ssize_t ver_len = strlen(version);
    // 如果请求的方法不为GET/请求的URL不以'/'开头/请求的版本号不为HTTP/开头或不以\r\n结尾，则返回错误
    if (strcmp(method,"GET") != 0 || url[0] != '/' ||
        (strncmp(version,"HTTP/1.0\r\n",10) != 0 && strncmp(version, "HTTP/1.1\r\n",10) != 0) ||
        strncmp(host, "Host: ", 6) != 0 || host[host_len-2] != '\r' || host[host_len-1] != '\n') {
        return -1;
    }
    memcpy(path,url,url_len+1);
    *path_len = url_len;
    return 0;
}

int get_content(char *path, long *file_size, FILE **file)
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
    
    *file = fopen(file_path, "r");
    if (*file == NULL) {
        perror("fopen error!\n");
        free(file_path);
        return -2;
    }

    fseek(*file, 0, SEEK_END);
    *file_size = ftell(*file);
    fseek(*file, 0, SEEK_SET);

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
    // 构造要返回的数据
    // 注意，响应头部后需要有一个多余换行（\r\n\r\n），然后才是响应内容
    char* response = (char*) malloc(MAX_SEND_LEN * sizeof(char));
    // 根据 HTTP 请求的内容，解析资源路径和 Host 头
    char* path = (char*) malloc(MAX_PATH_LEN * sizeof(char));

    ssize_t req_len = 0;
    ssize_t n = 0;
    ssize_t max_recv_len = MAX_RECV_LEN;
    // 读取请求，直到遇到 "\r\n\r\n"
    while (1) {
        n = read(clnt_sock, req_buf + req_len, max_recv_len - req_len);
        if(n < 0) {
            if(errno == EINTR) continue;
            perror("read error!\n");
            goto end;
        }
        req_len += n;
        if(strlen(req_buf) >= 3 && strncmp(req_buf, "GET", 3) != 0) {
            write_response(response, -1, -1, 0, clnt_sock);
            goto end;
        }
        if (req_len >= 4 && strcmp(req_buf + req_len - 4, "\r\n\r\n") == 0) {
            break;
        }
        if(req_len >= max_recv_len) {
            max_recv_len *= 2;
            char *new_req_buf = (char *)realloc(req_buf, max_recv_len * sizeof(char));
            if(!new_req_buf) {
                perror("realloc error!\n");
                goto end;
            }
            req_buf = new_req_buf;
        }   
    }

    
    ssize_t path_len;
    int ret_1 = parse_request(req_buf, req_len, path, &path_len, version);
    long file_size;
    //char* content = NULL;
    FILE *file = NULL;
    int ret_2 = get_content(path, &file_size, &file);
    
    if(write_response(response, ret_1, ret_2, file_size, clnt_sock) == -1){
        goto end;
    }
    if(ret_2 == 0) {
        char buffer[BUFFER_SIZE];
        while (!feof(file)) {
            size_t n = fread(buffer, 1, BUFFER_SIZE, file);
            if (n < 0) {
                perror("read error!\n");
                break;
            }
            ssize_t write_len = 0;
            while (write_len < n) {
                ssize_t ret = write(clnt_sock, buffer + write_len, n - write_len);
                if (ret < 0) {
                    if(errno == EINTR) continue;
                    perror("write error!\n");
                    break;
                }
                write_len += ret;
            }
        }
    }
    //fclose(file);
    // 关闭客户端套接字
end:
    close(clnt_sock);
    // 释放内存
    free(req_buf);
    free(path);
    free(response);
    free(version);
    if(file != NULL)
        free(file);
    return;
}

int main(){
    // 注册信号处理函数
    //signal(SIGINT, handle_signal);
    // 创建套接字，参数说明：
    //   AF_INET: 使用 IPv4
    //   SOCK_STREAM: 面向连接的数据传输方式
    //   IPPROTO_TCP: 使用 TCP 协议
    //int serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if((serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
        print_perror("socket error!\n");

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
    if(bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        print_perror("bind error!\n");

    // 使得 serv_sock 套接字进入监听状态，开始等待客户端发起请求
    if(listen(serv_sock, MAX_CONN) == -1)
        print_perror("listen error!\n");
    
    fd_set reads, temps;
    FD_ZERO(&reads);
    FD_SET(serv_sock, &reads);
    int fd_max = serv_sock;

    while (1) {
        temps = reads;
        if (select(fd_max + 1, &temps, 0, 0, NULL) == -1) {
            perror("select error");
            break;
        }

        for (int i = 0; i <= fd_max; i++) {
            if (FD_ISSET(i, &temps)) {
                if (i == serv_sock) { // new connection
                    struct sockaddr_in clnt_addr;
                    socklen_t clnt_addr_size = sizeof(clnt_addr);
                    int clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);

                    FD_SET(clnt_sock, &reads);
                    if (clnt_sock > fd_max) {
                        fd_max = clnt_sock;
                    }
                    //printf("new client: %d\n", clnt_sock);
                } else { // read from client
                    handle_clnt(i);

                    FD_CLR(i, &reads);
                    close(i);
                    //printf("client closed: %d\n", i);
                }
            }
        }
    }

    // 实际上这里的代码不可到达，可以在 while 循环中收到 SIGINT 信号时主动 break
    // 关闭套接字
    close(serv_sock);
    return 0;
}