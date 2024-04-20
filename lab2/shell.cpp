// IO
#include <iostream>
// std::string
#include <string>
// std::vector
#include <vector>
// std::string 转 int
#include <sstream>
// PATH_MAX 等常量
#include <climits>
// POSIX API
#include <unistd.h>
// wait
#include <sys/wait.h>

#include <sys/fcntl.h>
#include <unistd.h>
// 信号处理
#include <signal.h>

#include <limits>

std::vector<std::string> split(std::string s, const std::string &delimiter);
std::string trim(std::string s);
void handleRedirection(std::vector<std::string>& args);
void handle_sigint(int sig);
void hide_inout();
void wait(std::vector<pid_t> &bg_pids);
void process_bgs(std::vector<pid_t> &bg_pids);

int main() {

    // 信号处理
    struct sigaction shell, child;// shell进程需要忽略SIGINT,子进程需要处理SIGINT

    shell.sa_flags = 0;
    shell.sa_handler = handle_sigint;
    
    // 在程序的一开始就忽略Ctr+C，否则此时输入Ctr+C会终止shell程序
    sigaction(SIGINT,&shell,&child);
    signal(SIGTTOU, SIG_IGN); // shell进程忽略Ctrl+C

	// 不同步 iostream 和 cstdio 的 buffer
	std::ios::sync_with_stdio(false);

    std::vector<pid_t> bg_pids; // 用于存储后台进程的pid
	// 用来存储读入的一行命令
	std::string cmd;
	while (true) {
        process_bgs(bg_pids);

	    // 打印提示符
        std::cout << "$ ";

        // 读入一行。std::getline 结果不包含换行符。
        std::getline(std::cin, cmd);

        bool is_background = false;
        if (!cmd.empty() && cmd.back() == '&') {
            // 将命令作为后台进程运行
            is_background = true;
            // 移除'&'
            cmd.pop_back();
        }

        // 按空格分割命令为单词
        std::vector<std::string> args = split(cmd, " ");
        // 按" | "分割命令为单词
        std::vector<std::string> cmds = split(trim(cmd)," | ");
        int fd[cmds.size()-1][2]; // 用于pipe函数


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

        if (args[0] == "pwd") {
        	//std::cout << "To be done!\n";
        	char cwd[PATH_MAX]; // 存储当前工作目录
        	if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        		std::cout << cwd << std::endl;// 输出当前工作目录
        	} else {
        		std::cout << "getcwd failed\n";// 输出错误信息
        	}
        	continue;
        }

        if (args[0] == "cd") {
    	    //std::cout << "To be done!\n";
    	    if (args.size() <= 1 || args[1].find_first_not_of(' ') == std::string::npos) {
    	    	// cd 没有参数时，切换到家目录
            	chdir("/home");
    	    }
    	    else if(args.size() > 2)
    	    {
            	// cd 有多个参数时报错
            	std::cout << "Too many arguments!\n" << std::endl;
    	    }
    	    else {
            	// cd 有参数时，切换到参数指定的目录，参数为 args[1]
            	// chdir(args[1].c_str());
            	// 若切换失败，输出错误信息
            	if (chdir(args[1].c_str()) != 0) {
            		std::cout << "cd failed\n";
            	}
    	    }
    	    continue;
        }

        if (args[0] == "wait") {
            if(args.size() > 1) {
                std::cout << "Too many arguments!\n";
                continue;
            }
            wait(bg_pids);
            continue;
        }

        // 处理外部命令
        pid_t pid = fork();

        if (pid < 0)
        {
            std::cout << "fork failed\n";
            continue;
        }
        else if (pid == 0) {
            // 这里只有子进程才会进入

            // 子进程不能忽略SIG_IGN，故需要恢复默认行为
            sigaction(SIGINT,&child,nullptr);
            signal(SIGTTOU,SIG_DFL);

            if (is_background)
            {
                hide_inout();
            }

            if(cmds.size() > 1)
            {
                for(size_t i = 0;i < cmds.size()-1;i++)
                {
                    pipe(fd[i]);// 建立管道
                }
            }

            for(size_t i = 0;i < cmds.size();i++)
            {
            
                args = split(cmds[i]," "); // 注意此时args由cmds[i]得到而不是cmd
                pid_t pid_1 = fork();

                if(pid_1 == 0)
                {
                    // 处理外部命令

                    //handleRedirection(args);
                    if(cmds.size() > 1)
                    {
                        if(i != 0)
                        { 
                            // 若不是第一个命令，则需要从上一个命令的管道读取数据
                            dup2(fd[i-1][0], 0); // 将当前进程的标准输入重定向到上一个命令的管道读端
                            close(fd[i-1][0]); // 关闭上一个命令的读端
                            close(fd[i-1][1]); // 关闭上一个命令的写端
                        }
                        if(i != cmds.size()-1)
                        {
                            // 若不是最后一个命令，则需要将输出重定向到下一个命令的管道写端
                            // 这里的写端是在描述数据流的方向。当前进程将数据写入 fd[i][1]
                            // 虽然 fd[i][1] 是由当前进程创建和写入的，但从数据流的角度来看，它可以被视为下一个命令的写端
                            dup2(fd[i][1], 1); // 将当前进程的标准输出重定向到下一个命令的管道写端
                            close(fd[i][0]); // 关闭当前进程的读端
                            close(fd[i][1]); // 关闭当前进程的写端
                        }
                    }
                    handleRedirection(args);

                    // std::vector<std::string> 转 char **
                    int j = 0;
                    char *arg_ptrs[args.size() + 1];
                    for (size_t i = 0; i < args.size(); i++) {
                        if(args[i] != " ") // 在处理重定向时，将args[i] == ">",">>","<"的项都转换成了空格,所以这里要将这些空格忽略
                            arg_ptrs[j++] = &args[i][0];
                    }
                    //std::cout<< "j = " << j << ", args.size() = " << args.size() << std::endl;
                    // exec p 系列的 argv 需要以 nullptr 结尾
                    //arg_ptrs[args.size()] = nullptr;
                    arg_ptrs[j] = nullptr;
                    // execvp 会完全更换子进程接下来的代码，所以正常情况下 execvp 之后这里的代码就没意义了
                    // 如果 execvp 之后的代码被运行了，那就是 execvp 出问题了
                    execvp(args[0].c_str(), arg_ptrs);

                    // 所以这里直接报错
                    exit(255);
                }
                else
                {
                    if(i != 0)
                    {
                        // 关闭上一个命令的读端和写端，防止阻塞
                        close(fd[i-1][0]); // 关闭读端
                        close(fd[i-1][1]); // 关闭写端
                    }
                }
            }
            if(cmds.size() > 1)
            {
                close(fd[cmds.size()-2][1]); // 关闭最后一个管道的写端
            }
            while(wait(NULL) > 0); // 等待所有子进程结束
            return 0; // 结束父进程
        }	
        else {
            // 这里只有父进程（原进程）才会进入
            if(is_background)
            {
                bg_pids.push_back(pid);
                continue;
            }
            setpgid(pid,pid);// 将进程组id设置为子进程id
            tcsetpgrp(0,pid);// 将前台进程组设置为子进程的进程组
            int status;// 表示子进程是否结束
            // 等待子进程pid结束
            int ret = waitpid(pid, &status, 0);// 这里需要修改为waitpid，以知道子程序是正常结束还是因为接收到信号而结束
            tcsetpgrp(0,getpgrp()); // 将前台进程组设置为shell进程的进程组
            //int ret = wait(&over);
            if (ret < 0) {
                std::cout << "wait failed";
            }
            else if(WIFSIGNALED(status)) {
                // 子程序因接收到信号而结束时，需要输出换行符（正常结束时不需要做任何处理）
                std::cout << std::endl;
            }
        }

        /*// 这里只有父进程（原进程）才会进入
        setpgid(pid,pid);// 将进程组id设置为子进程id
        tcsetpgrp(0,pid);// 将前台进程组设置为子进程的进程组
        int over;// 表示子进程是否结束
        // 等待子进程pid结束
        int ret = waitpid(pid, &over, 0);// 这里需要修改为waitpid，以知道子程序是正常结束还是因为接收到信号而结束
        tcsetpgrp(0,getpgrp()); // 将前台进程组设置为shell进程的进程组
        //int ret = wait(&over);
        if (ret < 0) {
      	    std::cout << "wait failed";
        }
        else if(WIFSIGNALED(over)) {
            // 子程序因接收到信号而结束时，需要输出换行符（正常结束时不需要做任何处理）
            std::cout << std::endl;
        }*/
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

std::string trim(std::string s)
{
  	if (s.empty())
    	return "";
  	size_t first = s.find_first_not_of(' ');
  	if (first == std::string::npos)
    	return "";
  	size_t last = s.find_last_not_of(' ');
  	return s.substr(first, (last - first + 1));
}

void handleRedirection(std::vector<std::string>& args) {
    for (size_t i = 0; i < args.size(); i++) 
    {
        if (args[i] == ">>") 
        {
            //std::cout << args[i] << " " << args[i+1] << std::endl;
            int fd = open(args[i+1].c_str(), O_CREAT | O_APPEND | O_WRONLY, 0777);
            if(fd < 0)  
                std::cout << "Redirection Error\n";
            else
            {
                dup2(fd, 1);
                close(fd);
            }
            args[i] = " "; // 防止重定向文件名被当作参数
            args[i+1] = " ";
        } 
        if (args[i] == ">") 
        {
            //std::cout << args[i] << " " << args[i+1] << std::endl;
            int fd = open(args[i+1].c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0777);
            if(fd < 0)  
                std::cout << "Redirection Error\n";
            else
            {
                dup2(fd, 1);
                close(fd);
            }
            args[i] = " "; // 防止重定向文件名被当作参数
            args[i+1] = " ";
        } 
        if (args[i].find(">") != std::string::npos && args[i] != ">")
        {
            // 处理数字文件重定向
            int fd_num = std::stoi(args[i].substr(0, args[i].find(">")));
            int fd = open(args[i+1].c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0777);
            if(fd < 0)  
                std::cout << "Redirection Error\n";
            else
            {
                dup2(fd, fd_num);
                close(fd);
            }
            args[i] = " "; // 防止重定向文件名被当作参数
            args[i+1] = " ";
        }
        if (args[i] == "<") 
        {
            //std::cout << args[i] << " " << args[i+1] << std::endl;
            int fd = open(args[i+1].c_str(),  O_RDONLY);
            if(fd < 0)  
                std::cout << "Redirection Error\n";
            else
            {
                dup2(fd, 0);
                close(fd);
            }
            args[i] = " "; // 防止重定向文件名被当作参数
            args[i+1] = " ";
        }
        if(args[i] == "<<<")
        {
            // 文本重定向
            //std::cout << args[i] << " " << args[i+1] << std::endl;
            char tmpfile[] = "/tmp/fileXXXXXX";// 创建临时文件
            int fd = mkstemp(tmpfile);// 返回临时文件的文件描述符
            if(fd < 0)
            {
                std::cout << "Redirection Error\n";
            }
            else
            {
                std::string text = args[i+1] + "\n";
                write(fd, text.c_str(), text.size());
                lseek(fd, 0, SEEK_SET); // 重置文件指针到文件开始
                dup2(fd, 0);
                close(fd);
            }
            args[i] = " "; // 防止重定向文本被当作参数
            args[i+1] = " ";
        }
        if(args[i] == "<<")
        {
            // EOF重定向
            if(args[i+1] != "EOF")
            {
                std::cout << "EOF redirection Error\n";
                continue;
            }
            char tmpfile[] = "/tmp/fileXXXXXX";
            int fd = mkstemp(tmpfile);
            if(fd < 0)
            {
                std::cout << "Redirection Error\n";
            }
            else
            {
                std::string text;
                for(size_t j = i+2; j < args.size(); j++)
                {
                    if(args[j] == "EOF")
                    {
                        break;
                    }
                    text += args[j];
                }
                write(fd, text.c_str(), text.size());
                lseek(fd, 0, SEEK_SET); // 重置文件指针到文件开始
                dup2(fd, 0);
                close(fd);
            }
            args[i] = " "; // 防止重定向文本被当作参数
            args[i+1] = " ";
            // 清除下一个EOF之前的所有参数
            for(size_t j = i+2; j < args.size(); j++)
            {
                if(args[j] == "EOF")
                {
                    args[j] = " ";
                    break;
                }
                args[j] = " ";
            }
        }
    }
}

void handle_sigint(int sig)
{
    // 清空输入流中的残留数据并重置输入流状态
    std::cin.ignore(std::cin.rdbuf()->in_avail());
    // 开始下一行
    std::cout << "\n$ ";
    // 清空输出缓冲区
    std::cout.flush();
}

void hide_inout()
{
    int null_fd = open("/dev/null", O_RDWR);
    if(null_fd < 0)
    {
        std::cout << "open /dev/null failed\n";
        return;
    }
    dup2(null_fd, 0);
    dup2(null_fd, 1);
    dup2(null_fd, 2);
    close(null_fd);
}

void wait(std::vector<pid_t> &bg_pids)
{
    for(size_t i = 0; i < bg_pids.size(); i++)
    {
        int status;
        waitpid(bg_pids[i], &status, 0);
        if(WIFEXITED(status))
        {
            std::cout << "Process " << bg_pids[i] << " exited with status " << WEXITSTATUS(status) << std::endl;
        }
        else if(WIFSIGNALED(status))
        {
            std::cout << "Process " << bg_pids[i] << " terminated by signal " << WTERMSIG(status) << std::endl;
        }
    }
}

// 处理后台进程，删除bg_pids中已经结束的命令，防止出现僵尸进程
void process_bgs(std::vector<pid_t> &bg_pids)
{
    for(int i = bg_pids.size() - 1; i >= 0; i--)
    {
        int status;
        if(waitpid(bg_pids[i], &status, WNOHANG) > 0)
        {
            bg_pids.erase(bg_pids.begin() + i);
        }
    }
}