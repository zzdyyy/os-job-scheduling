#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include "job.h"

/* 
 * 命令语法格式
 *     stat
 */
void usage()
{
	printf("Usage: stat\n");		
}

int main(int argc,char *argv[])
{
	if(argc!=1)
	{
		usage();
		return 1;
	}
	
	int pidFile;
	pid_t jobPid;
	if((pidFile=open("/tmp/os_job_pid",O_RDONLY))<0)
		error_sys("stat get pid failed");
	read(pidFile,&jobPid,sizeof(jobPid));
	close(pidFile);   
	kill(jobPid,SIGUSR1);
	int readFile;
	readFile=open("/tmp/os_job_stat",O_RDONLY);
	char ch;
	while(read(readFile,&ch,1)>0){
		 printf("%c",ch);
	}
	close(readFile);
        
	return 0;
}
