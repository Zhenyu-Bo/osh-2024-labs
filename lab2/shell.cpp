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

std::vector<std::string> split(std::string s, const std::string &delimiter);
std::string trim(std::string s);

int main() {
	// 不同步 iostream 和 cstdio 的 buffer
	std::ios::sync_with_stdio(false);
	
	// 用来存储读入的一行命令
	std::string cmd;
	while (true) {
	// 打印提示符
    std::cout << "$ ";

    // 读入一行。std::getline 结果不包含换行符。
    std::getline(std::cin, cmd);

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

    // 处理外部命令
    pid_t pid = fork();

    /*// std::vector<std::string> 转 char **
    char *arg_ptrs[args.size() + 1];
    for (auto i = 0; i < args.size(); i++) {
    	arg_ptrs[i] = &args[i][0];
    }
    // exec p 系列的 argv 需要以 nullptr 结尾
    arg_ptrs[args.size()] = nullptr;*/

    if (pid == 0) {
    // 这里只有子进程才会进入
    if(cmds.size() > 1)
    {
        for(int i = 0;i < cmds.size()-1;i++)
        {
            pipe(fd[i]);// 建立管道
        }
    }

        for(int i = 0;i < cmds.size();i++)
        {
            args = split(cmds[i]," ");
            pid_t pid_1 = fork();

            if(pid_1 == 0)
            {
                // 处理外部命令

                // std::vector<std::string> 转 char **
                char *arg_ptrs[args.size() + 1];
                for (auto i = 0; i < args.size(); i++) {
                    arg_ptrs[i] = &args[i][0];
                }
                // exec p 系列的 argv 需要以 nullptr 结尾
                arg_ptrs[args.size()] = nullptr;

                if(cmds.size() > 1)
                {
                    if(i != 0)
                    {
                        dup2(fd[i-1][0], 0); // 
                        close(fd[i-1][0]);
                        close(fd[i-1][1]); // 关闭写端
                    }
                    if(i != cmds.size()-1)
                    {
                        dup2(fd[i][1], 1); // 
                        close(fd[i][0]); // 关闭读端
                        close(fd[i][1]);
                    }
                }
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
                    close(fd[i-1][0]);
                    close(fd[i-1][1]); // 关闭写端
                }
            }
        }
        if(cmds.size() > 1)
        {
            close(fd[cmds.size()-2][1]);
        }
        while(wait(NULL) > 0);
        exit(0);
    }
        	

    // 这里只有父进程（原进程）才会进入
    int ret = wait(nullptr);
    if (ret < 0) {
      	std::cout << "wait failed";
    }
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