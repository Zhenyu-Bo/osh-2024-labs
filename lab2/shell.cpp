#include <climits>    // PATH_MAX 等常量
#include <fcntl.h>    // 重定向中open
#include <iostream>   // IO
#include <pwd.h>      // 用于获取用户
#include <signal.h>   // 处理信号
#include <sstream>    // std::string 转 int
#include <stdlib.h>   // 文本重定向临时文件
#include <string>     // std::string
#include <sys/wait.h> // wait
#include <unistd.h>   // POSIX API
#include <vector>     // std::vector

#define READ_END 0
#define WRITE_END 1
#define HERE_STR -1

class job {
public:
    int id;
    std::vector<pid_t> pids;
    std::string cmd;

    job(int id, std::string cmd) : id(id), cmd(cmd) {}
};

std::vector<std::string> split(std::string s, const std::string &delimiter);
void trim(std::string &line);
void format(std::string &line);
void extend(std::vector<std::string> &args, char *home);
void printcwd(void);
void pwd(const std::vector<std::string> &args);
void wait(const std::vector<std::string> &args, std::vector<job> &bg_jobs);
void kill_bg(std::vector<job> &bg_jobs);
void cd(std::vector<std::string> &args, const char *home);
void exec(std::string &cmd, char *home);
void redirect(std::string &cmd, const std::string &redir, const int &originfd, const int &flag, const mode_t &mode = 0);
void redirect_parse(std::string &cmd, const std::size_t &pos, std::string &filename, int &redirfd, int len);
void handler(int signum);

int main() {
    // 不同步 iostream 和 cstdio 的 buffer
    std::ios::sync_with_stdio(false);

    // 用来存储读入的一行命令
    std::string cmd;

    // 用来存储后台进程
    std::vector<job> bg_jobs;

    // 用于替换路径中的'~'
    char *home = getenv("HOME");
    if (home == nullptr) {
        home = getpwuid(getuid())->pw_dir;
    }

    struct sigaction ignore, handle, dfl;
    ignore.sa_flags = 0;
    ignore.sa_handler = SIG_IGN;
    handle.sa_flags = 0;
    handle.sa_handler = handler;
    dfl.sa_flags = 0;
    dfl.sa_handler = SIG_DFL;

    sigaction(SIGINT, &handle, nullptr);
    while (true) {
        printcwd();
        // 打印提示符
        std::cout << " # ";

        // 读入一行。std::getline 结果不包含换行符。
        std::getline(std::cin, cmd);

        if (std::cin.eof()) {
            std::cout << std::endl
                      << "exit" << std::endl;
            return 0;
        }

        // 命令格式化
        format(cmd);

        // 按空格分割命令为单词
        std::vector<std::string> args = split(cmd, " ");

        // 没有可处理的命令
        if (args.empty()) {
            continue;
        }

        // 退出
        if (args[0] == "exit") {
            if (args.size() <= 1) {
                return 0;
            }

            // std::string 转 int
            std::stringstream code_stream(args[1]);
            int code = 0;
            code_stream >> code;

            // 转换失败
            if (!code_stream.eof() || code_stream.fail()) {
                std::cout << "Invalid exit code\n";
                continue;
            }

            return code;
        }

        // 处理pwd命令
        if (args[0] == "pwd") {
            pwd(args);
            continue;
        }

        if (args[0] == "wait") {
            wait(args, bg_jobs);
            continue;
        }

        extend(args, home);

        // 处理cd命令
        if (args[0] == "cd") {
            cd(args, home);
            continue;
        }

        // 处理&符号
        bool is_bg = cmd.back() == '&';
        if (is_bg) {
            cmd.pop_back();
            trim(cmd);
        }
        // 根据管道分割命令
        std::vector<std::string> cmds = split(cmd, "|");
        // 管道数量
        int pipe_num = cmds.size() - 1;

        // 处理外部命令
        if (pipe_num > 0) {
            // 存在管道
            int pipefd[pipe_num][2];
            // 管道进程组ID
            pid_t pipegrpid;
            job jobs = job(bg_jobs.size() + 1, cmd);
            for (int i = 0; i < pipe_num; i++) {
                if (pipe(pipefd[i]) == -1) {
                    std::cout << "pipe failed" << std::endl;
                    exit(255);
                }
            }
            for (int i = 0; i < pipe_num + 1; i++) {
                pid_t cpid = fork();
                if (cpid < 0) {
                    std::cout << "fork failed" << std::endl;
                    exit(255);
                } else if (cpid == 0) {
                    // 输出重定向
                    if (i == 0) {
                        // 管道进程组ID为第一个子进程ID
                        pipegrpid = getpid();
                        // 第一个子进程输入来自标准输入
                        dup2(pipefd[i][WRITE_END], STDOUT_FILENO);
                    } else if (i == pipe_num) {
                        // 最后一个子进程输出到标准输出
                        dup2(pipefd[i - 1][READ_END], STDIN_FILENO);
                    } else {
                        // 其余进程输入输出都是管道
                        dup2(pipefd[i - 1][READ_END], STDIN_FILENO);
                        dup2(pipefd[i][WRITE_END], STDOUT_FILENO);
                    }
                    // 进程组分离
                    setpgid(getpid(), pipegrpid);

                    if (is_bg) {
                        sigaction(SIGINT, &ignore, nullptr);
                        sigaction(SIGTTOU, &ignore, nullptr);
                        tcsetpgrp(0, getppid());
                    } else {
                        sigaction(SIGINT, &dfl, nullptr);
                        sigaction(SIGTTOU, &dfl, nullptr);
                    }

                    exec(cmds[i], home);
                }

                if (i == 0) {
                    // 第一个子进程ID为管道进程组ID
                    pipegrpid = cpid;
                    // 第一个进程关闭原本的标准输出
                    close(pipefd[i][WRITE_END]);
                } else if (i == pipe_num) {
                    // 最后一个进程关闭原本的标准输入
                    close(pipefd[i - 1][READ_END]);
                } else {
                    // 其余进程关闭原本的标准输入和输出
                    close(pipefd[i - 1][READ_END]);
                    close(pipefd[i][WRITE_END]);
                }

                jobs.pids.push_back(cpid);

                // 进程组分离
                setpgid(cpid, pipegrpid);
            }

            if (is_bg) {
                tcsetpgrp(0, getpgrp());
                std::cout << "[" << bg_jobs.size() + 1 << "] " << pipegrpid << std::endl;
                bg_jobs.push_back(jobs);
            } else {
                // 将管道进程组调到前台
                tcsetpgrp(0, pipegrpid);
                sigaction(SIGTTOU, &ignore, nullptr);

                // 等待所有子进程结束
                int status;
                for (int i = 0; i <= pipe_num; i++) {
                    int ret = waitpid(jobs.pids[i], &status, 0);
                    if (ret < 0) {
                        std::cout << "waitpid failed" << std::endl;
                        exit(255);
                    }
                }

                tcsetpgrp(0, getpgrp());

                if (!WIFEXITED(status)) {
                    std::cout << std::endl;
                }
            }
        } else {
            // 不存在管道
            pid_t pid = fork();

            if (pid < 0) {
                std::cout << "fork failed" << std::endl;
            } else if (pid == 0) {
                // 这里只有子进程才会进入

                // 子进程与父进程分离
                setpgrp();

                if (is_bg) {
                    sigaction(SIGINT, &ignore, nullptr);
                    sigaction(SIGTTOU, &ignore, nullptr);
                    tcsetpgrp(0, getppid());
                } else {
                    sigaction(SIGINT, &dfl, nullptr);
                    sigaction(SIGTTOU, &dfl, nullptr);
                }

                exec(cmds[0], home);
            }
            // 这里只有父进程（原进程）才会进入

            if (is_bg) {
                // 后台运行
                tcsetpgrp(0, getpgrp());
                std::cout << "[" << bg_jobs.size() + 1 << "] " << pid << std::endl;
                job bg_job = job(bg_jobs.size() + 1, cmd);
                bg_job.pids.push_back(pid);
                bg_jobs.push_back(bg_job);
            } else {
                // 防止先走父进程，子进程与父进程分离
                setpgid(pid, pid);

                // 将子进程调到前台
                tcsetpgrp(0, pid);
                sigaction(SIGTTOU, &ignore, nullptr);

                int status;
                int ret = waitpid(pid, &status, 0);
                if (ret < 0) {
                    std::cout << "wait failed" << std::endl;
                    exit(255);
                }
                tcsetpgrp(0, getpgrp());
                if (!WIFEXITED(status)) {
                    std::cout << std::endl;
                }
            }
        }
        // 处理后台进程
        kill_bg(bg_jobs);
    }
}

// 经典的 cpp string split 实现
// https://stackoverflow.com/a/14266139/11691878
std::vector<std::string> split(std::string s, const std::string &delimiter) {
    std::vector<std::string> res;
    size_t pos = 0;
    std::string token;
    while ((pos = s.find(delimiter)) != std::string::npos) {
        token = s.substr(0, pos);
        res.push_back(token);
        s = s.substr(pos + delimiter.length());
    }
    res.push_back(s);
    return res;
}

// 去除line前后空格
void trim(std::string &line) {
    int lPos = line.find_first_not_of(' ');
    if (lPos == -1) {
        // 整行均为' '
        line.replace(0, line.length(), "");
        return;
    }
    int rPos = line.find_last_not_of(' ');
    line.replace(0, line.length(), line.substr(lPos, rPos - lPos + 1));
    return;
}

// 命令行格式化
void format(std::string &line) {
    std::string replace_list = {'\t', '\n', '\r', '\f', '\v'};
    std::string::size_type pos;
    if (line.find('#') != std::string::npos) {
        line = line.substr(0, line.find('#'));
    }
    for (auto i : replace_list) {
        while ((pos = line.find(i)) != std::string::npos) {
            line.replace(pos, 1, " ");
        }
    }
    while ((pos = line.find("  ")) != std::string::npos) {
        line.replace(pos, 2, " ");
    }
    trim(line);
    return;
}

// 符号扩展
void extend(std::vector<std::string> &args, char *home) {
    for (std::size_t i = 0; i < args.size(); i++) {
        if (args[i].substr(0, 1) == "$") {
            // 替换环境变量
            size_t pos = args[i].find_first_of('/');
            if (pos == std::string::npos) {
                char *param = getenv(&(args[i].substr(1, args[i].length() - 1)[0]));
                if (param == nullptr) {
                    args[i].replace(0, args[i].length(), " ");
                } else {
                    args[i].replace(0, args[i].length(), param);
                }
            } else {
                char *param = getenv(getenv(&(args[i].substr(1, pos - 1)[0])));
                if (param == nullptr) {
                    args[i].replace(0, pos, " ");
                } else {
                    args[i].replace(0, pos, param);
                }
            }
        } else if (args[i].substr(0, 2) == "~/" || args[i] == "~") {
            // 替换 ~
            args[i].replace(0, 1, std::string(home));
        } else if (args[i].substr(0, 1) == "~") {
            // ~拓展
            size_t pos = args[i].find_first_of('/');
            if (pos == std::string::npos) {
                args[i].replace(0, args[i].length(), getpwnam(&(args[i].substr(1, args[i].length() - 1))[0])->pw_dir);
            } else {
                args[i].replace(0, pos, getpwnam(&(args[i].substr(1, pos - 1))[0])->pw_dir);
            }
        }
    }
    return;
}

// 输出当前路径
void printcwd(void) {
    char *buf = new char[PATH_MAX + 1];
    int size = PATH_MAX;
    while (getcwd(buf, size) == nullptr) {
        delete[] buf;
        size += PATH_MAX;
        buf = new char[size];
    }
    std::cout << buf;
    delete[] buf;
    return;
}

// pwd 命令
void pwd(const std::vector<std::string> &args) {
    if (args.size() > 1) {
        std::cout << "pwd: invalid argument number" << std::endl;
        return;
    }
    printcwd();
    std::cout << std::endl;
    return;
}

// wait 命令
void wait(const std::vector<std::string> &args, std::vector<job> &bg_jobs) {
    if (args.size() > 1) {
        std::cout << "wait: invalid argument number" << std::endl;
        return;
    }
    if (!bg_jobs.empty()) {
        for (auto &i : bg_jobs) {
            for (size_t j = 0; j < i.pids.size(); j++) {
                waitpid(i.pids[j], nullptr, 0);
            }
            std::cout << "[" << i.id << "]"
                      << " Done    " << i.cmd << std::endl;
        }
        bg_jobs.clear();
    }
    return;
}

// 处理后台进程
void kill_bg(std::vector<job> &bg_jobs) {
    if (!bg_jobs.empty()) {
        for (size_t i = 0; i < bg_jobs.size(); i++) {
            for (size_t j = 0; j < bg_jobs[i].pids.size(); j++) {
                // 如果为僵尸进程，则删去
                if (waitpid(bg_jobs[i].pids[j], nullptr, WNOHANG)) {
                    bg_jobs[i].pids.erase(bg_jobs[i].pids.begin() + j);
                    j--;
                }
            }
            if (bg_jobs[i].pids.size() == 0) {
                std::cout << "[" << bg_jobs[i].id << "]"
                          << " Done    " << bg_jobs[i].cmd << std::endl;
                bg_jobs.erase(bg_jobs.begin() + i);
                i--;
            }
        }
    }
    return;
}

// cd 命令
void cd(std::vector<std::string> &args, const char *home) {
    if (args.size() > 2) {
        std::cout << "cd: invalid argument number" << std::endl;
        return;
    }
    if (args.size() == 1) {
        args.push_back(std::string(home));
    } else if (args[1].substr(0, 2) == "~/" || args[1] == "~") {
        args[1].replace(0, 1, std::string(home));
    }
    if (chdir(args[1].c_str()) == -1) {
        std::cout << "cd failed" << std::endl;
    }
    return;
}

// 执行外部命令
void exec(std::string &cmd, char *home) {
    format(cmd);

    redirect(cmd, "<<< ", STDIN_FILENO, HERE_STR);
    redirect(cmd, "< ", STDIN_FILENO, O_RDONLY);
    redirect(cmd, ">> ", STDOUT_FILENO, O_APPEND | O_WRONLY | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH);
    redirect(cmd, "> ", STDOUT_FILENO, O_TRUNC | O_WRONLY | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH);

    std::vector<std::string> args = split(cmd, " ");
    extend(args, home);
    char *arg_ptrs[args.size() + 1];
    for (std::size_t i = 0; i < args.size(); i++) {
        arg_ptrs[i] = &args[i][0];
    }
    // exec p 系列的 argv 需要以 nullptr 结尾
    arg_ptrs[args.size()] = nullptr;

    // execvp 会完全更换子进程接下来的代码，所以正常情况下 execvp 之后这里的代码就没意义了
    // 如果 execvp 之后的代码被运行了，那就是 execvp 出问题了
    execvp(args[0].c_str(), arg_ptrs);

    // 所以这里直接报错
    exit(255);
}

// 重定向
void redirect(std::string &cmd, const std::string &redir, const int &originfd, const int &flag, const mode_t &mode) {
    std::size_t pos;
    std::string filename;
    int redirfd;

    while ((pos = cmd.find(redir)) != std::string::npos) {
        int fd;
        redirfd = originfd;

        redirect_parse(cmd, pos, filename, redirfd, redir.length());

        if (flag < 0) {
            // 文本重定向
            filename.append("\n");
            // 临时文件
            char temp_file[] = "temp_XXXXXX";
            if ((fd = mkstemp(temp_file)) == -1) {
                std::cout << "redirect failed" << std::endl;
                exit(255);
            }
            // 删除文件入口，之后文件用完会自动删除
            unlink(temp_file);
            write(fd, &filename[0], filename.size());
            // 回到临时文件开头，用于之后读入文件内容
            lseek(fd, 0, SEEK_SET);
        } else {
            // 文件重定向
            if ((fd = open(&filename[0], flag, mode)) == -1) {
                std::cout << "redirect failed" << std::endl;
                exit(255);
            }
        }

        dup2(fd, redirfd);
    }
    return;
}

// 切分重定向
void redirect_parse(std::string &cmd, const std::size_t &pos, std::string &filename, int &redirfd, int len) {
    std::size_t rpos = cmd.find(" ", pos + len);
    std::size_t lpos = cmd.find_last_of(" ", pos);
    // 右侧无空格，则已到结尾
    if (rpos == std::string::npos) {
        rpos = cmd.length();
    }
    filename = &(cmd.substr(pos + len, rpos - pos - len)[0]);
    // 左侧无空格，则开头就是重定向
    if (lpos == std::string::npos) {
        lpos = 0;
    }
    // 判断有无数字文件描述符
    if (lpos < pos - 1) {
        redirfd = atoi(&(cmd.substr(lpos, pos - lpos)[0]));
    }
    cmd.replace(lpos, rpos - lpos, ""); // 保证前面的命令后无单独空格
    return;
}

// 信号处理
void handler(int signum) 
{
    std::cout << std::endl;
    printcwd();
    std::cout << " # ";
    std::cout.flush();
    return;
}

