#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>


void syscall_test(int pid, char *buf, int buf_len)
{
	short res = syscall(pid, buf, buf_len);
	if(res == -1)
	{
		//printf("Succeed! The content is %s\n",buf);
		printf("Failed! The len %d is too short\n", buf_len);
	}
	else
	{
		//printf("Failed! The len %d is too short\n",buf_len);
		printf("Success! The content is %s\n",buf);
	}

}


int main() {
    //printf("Hello! PB22081571\n"); // Your Student ID
    char buf20[20], buf50[50];
    syscall_test(548,buf20,20);
    syscall_test(548,buf50,50);
    while(1) {}
}
