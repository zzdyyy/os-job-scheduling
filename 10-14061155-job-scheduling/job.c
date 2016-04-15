#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include "job.h"
#define tick 500 //neal: add macro

int jobid=0;
int siginfo=1;
int fifo;
//int globalfd; neal: delete unused variable

struct waitqueue *head[3]={NULL,NULL,NULL};//neal: change it into an array
struct waitqueue *next=NULL,*current =NULL;

struct waitqueue *findtail(int i)
{
	struct waitqueue *p=NULL;
	p = head[i];
	while(p!=NULL && p->next != head[i])
		p = p->next;
	return p;
}

/* 调度程序 */
void scheduler()
{
	struct jobinfo *newjob=NULL;
	struct jobcmd cmd;
	int  count = 0;
	static int timeslice=0;
	bzero(&cmd,DATALEN);
	if((count=read(fifo,&cmd,DATALEN))<0)
		error_sys("read fifo failed");
#ifdef DEBUG

	if(count){
		printf("cmd cmdtype\t%d\ncmd defpri\t%d\ncmd data\t%s\n",cmd.type,cmd.defpri,cmd.data);
	}
	else
		printf("no data read\n");
#endif

	/* 更新等待队列中的作业 */
#ifdef RT_DEBUG
	debug_stat("before updateall()");
#endif
	updateall();
#ifdef RT_DEBUG
	debug_stat("after updateall()");
#endif

	switch(cmd.type){
	case ENQ:
		newjob=do_enq(cmd);
		break;
	case DEQ:
		do_deq(cmd);
		break;
/*neal: delete this case
	case STAT:
		do_stat(cmd);
		break;*/
	default:
		break;
	}
#ifdef RT_DEBUG
	debug_stat("after do_enq and do_deq");
#endif

	if(current==NULL)
		timeslice=0;
	else
		--timeslice;

	//if the newjob is prior, reset the timeslice
	if(newjob!=NULL && current!=NULL && newjob->curpri > current->job->curpri)
		timeslice=0;

	if(timeslice<=0){
	    //next=NULL;jobswitch();
		/* 选择高优先级作业 */
#ifdef RT_DEBUG
		debug_stat("before jobselect()");
#endif
		next=jobselect();
		/* 作业切换 */
#ifdef RT_DEBUG
		debug_stat("after jobselect(), before jobswitch()");
#endif
		jobswitch();
#ifdef RT_DEBUG
		debug_stat("after jobswitch()");
#endif

		timeslice=(	current==NULL? 0:
		            current->job->curpri==2? 1000/tick:
					current->job->curpri==1? 2000/tick:
					current->job->curpri==0? 5000/tick: 0 );
	}
}

int allocjid()
{
	return ++jobid;
}

void updateall()
{	//printf("begin updateall.\n");fflush(stdout);
	struct waitqueue *p,*tail[3]={NULL,NULL,NULL};
	int i;
	/* 更新作业运行时间 */
	if(current)
		current->job->run_time += tick; 
	//printf("time update begin.\n");fflush(stdout);
	for(i=0;i<=2;i++)		//所有队列更新等待时间
	{
		p=head[i];
		if(head[i]==NULL)
			continue;
		for(;p!=NULL && p->next!=head[i];p=p->next) //不包括队尾
			p->job->wait_time += tick;
		p->job->wait_time += tick;//队尾更新
	}
	//printf("time update finish.\n");fflush(stdout);
	for(i=0;i<2;i++)			
	{
		struct waitqueue *pre=NULL;
		int flag = 1;//默认flag为真
		tail[i] = findtail(i);
		tail[i+1] = findtail(i+1);
		pre = findtail(i); 	//pre为当前所指的前一个（初始值为tail[i]）
		/* 更新作业等待时间及优先级 */
		p = head[i];
		while(flag==1)
		{
			if(head[i]==NULL)
				break;
			if(head[i]->next!=head[i]) //至少有两个
			{
				if(p->next==head[i])
					flag=0;
				if(p->job->wait_time >= 10000)
				{       
					if(p==head[i]){//若头部需改变优先级
						head[i]=head[i]->next;
						tail[i]->next=head[i];
					}
					p->job->curpri++;
					p->job->wait_time = 0;
					pre->next = p->next; //断p尾
					if(head[i+1]==NULL)  //优先级高的队列为空
					{
						head[i+1]=p;
						p->next=head[i+1];
						tail[i+1]=p;
					}
					else			//优先级高的队列不空
					{
						p->next = head[i+1]; //连p头
						tail[i+1]->next = p; //连p尾
						tail[i+1] = p; 	     //更新i+1的尾
					}
					p = pre;	     //重置p
				}
				pre = p;	//更新pre
				p = p->next;
			}
			else//队列中只有一个
			{
				flag=0;
				if(p->job->wait_time >= 10000)
				{
					p->job->curpri++;
					p->job->wait_time = 0;
					head[i]->next=NULL;
					head[i] = NULL;
					if(head[i+1]==NULL)  //优先级高的队列为空
					{
						head[i+1]=p;
						p->next=head[i+1];
						tail[i+1]=p;
					}
					else			//优先级高的队列不空
					{
						p->next = head[i+1];
						tail[i+1]->next = p;
						tail[i+1] = p;
					}
				}
			}
		}
	}
						
}

struct waitqueue* jobselect()
{
	struct waitqueue *pre,*select;
	select = NULL;
	for(int i = 2; i >= 0; i--)
	{
		if(head[i]!=NULL)	//队列不为空
		{
			select = head[i];  //取出头
			pre = findtail(i);
			if(pre != head[i]) //循环队列有两个以上
			{
				head[i] = head[i]->next;	//移动更新head
				pre->next = head[i];		//将pre（即尾）指向新head
			}
			else		  //循环队列只有一项
				head[i] = NULL;		//取出后置空
			select->next=NULL;//neal: set next at NULL
			break;  //有取出后，则退出循环
		}
	}
	return select;
}

void jobswitch()
{
	struct waitqueue *p;
	int i;

	if(current && current->job->state == DONE){ /* 当前作业完成 */
		/* 作业完成，删除它 */
		for(i = 0;(current->job->cmdarg)[i] != NULL; i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i] = NULL;
		}
		/* 释放空间 */
		free(current->job->cmdarg);
		free(current->job);
		free(current);

		current = NULL;
	}

	if(next == NULL && current == NULL) /* 没有作业要运行 */
		return;
	else if (next != NULL && current == NULL){ /* 开始新的作业 */

		printf("begin start new job\n");//neal: TODO
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		kill(current->job->pid,SIGCONT);
		return;
	}
	else if (next != NULL && current != NULL){ /* 切换作业 */

		printf("switch to Pid: %d\n",next->job->pid);
		kill(current->job->pid,SIGSTOP);
		current->job->curpri = current->job->defpri;
		current->job->wait_time = 0;
		current->job->state = READY;
		//////////////////////////////////////////////////////////////// 4.15 00:30
		int j = current->job->defpri;

		/* 放回等待队列 */
		if(head[j]){
			p = findtail(j);
			p->next = current;
			current->next = head[j];

		}else{
			head[j] = current;
			current->next = head[j];
		}
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		current->job->wait_time = 0;
		kill(current->job->pid,SIGCONT);
		return;
	}else{ /* next == NULL且current != NULL，不切换 */
		return;
	}
}

void sig_handler(int sig,siginfo_t *info,void *notused)
{
	int status;
	int ret;

	switch (sig) {
case SIGVTALRM: /* 到达计时器所设置的计时间隔 */
	scheduler();
	return;
case SIGCHLD: /* 子进程结束时传送给父进程的信号 */
	ret = waitpid(-1,&status,WNOHANG);
	if (ret == 0)
		return;
	if(WIFEXITED(status)){
		current->job->state = DONE;
		printf("normal termation, exit status = %d\n",WEXITSTATUS(status));
	}else if (WIFSIGNALED(status)){
		printf("abnormal termation, signal number = %d\n",WTERMSIG(status));
	}else if (WIFSTOPPED(status)){
		printf("child stopped, signal number = %d\n",WSTOPSIG(status));
	}
	return;
case SIGUSR1: /* 子进程发送STAT命令时传送给父进程的信号 *///TONY
        do_stat();//TONY
        return;//TONY
	default:
		return;
	}
}

struct jobinfo *do_enq(struct jobcmd enqcmd)//neal: change the prototype
{
	struct jobinfo *newjob;//neal:add new declaration
	struct waitqueue *newnode;//neal:delete p
	struct waitqueue *thehead, *thetail;//neal: new declaration
	int i=0,pid;
	char *offset,*argvec,*q;
	char **arglist;
	sigset_t zeromask;

	sigemptyset(&zeromask);

	/* 封装jobinfo数据结构 */
	newjob = (struct jobinfo *)malloc(sizeof(struct jobinfo));
	newjob->jid = allocjid();
	newjob->defpri = enqcmd.defpri;
	newjob->curpri = enqcmd.defpri;
	newjob->ownerid = enqcmd.owner;
	newjob->state = READY;
	newjob->create_time = time(NULL);
	newjob->wait_time = 0;
	newjob->run_time = 0;
	arglist = (char**)malloc(sizeof(char*)*(enqcmd.argnum+1));
	newjob->cmdarg = arglist;
	offset = enqcmd.data;
	argvec = enqcmd.data;
	while (i < enqcmd.argnum){
		if(*offset == ':'){
			*offset++ = '\0';
			q = (char*)malloc(offset - argvec);
			strcpy(q,argvec);
			arglist[i++] = q;
			argvec = offset;
		}else
			offset++;
	}

	arglist[i] = NULL;

#ifdef DEBUG

	printf("enqcmd argnum %d\n",enqcmd.argnum);
	for(i = 0;i < enqcmd.argnum; i++)
		printf("parse enqcmd:%s\n",arglist[i]);

#endif

	/*向等待队列中增加新的作业*/
	newnode = (struct waitqueue*)malloc(sizeof(struct waitqueue));
	newnode->next =NULL;
	newnode->job=newjob;

	//neal: renew the adding precedure
	thehead=head[enqcmd.defpri];
	thetail=findtail(enqcmd.defpri);
	if(thehead != NULL){
		thetail->next=newnode;
		newnode->next=thehead;
	}else{
		newnode->next=newnode;
		head[enqcmd.defpri]=newnode;
	}

	/*为作业创建进程*/
	if((pid=fork())<0)
		error_sys("enq fork failed");

	if(pid==0){
		newjob->pid =getpid();
		/*阻塞子进程,等等执行*/
		raise(SIGSTOP);
#ifdef DEBUG

		printf("begin running\n");
		for(i=0;arglist[i]!=NULL;i++)
			printf("arglist %s\n",arglist[i]);
#endif

		/*复制文件描述符到标准输出*/
		//dup2(globalfd,1); neal:delete unused variable

		/* 执行命令 */
		if(execv(arglist[0],arglist)<0)
			printf("exec failed\n");
		exit(1);
	}else{
		int ret;
		ret=waitpid(pid,NULL,WUNTRACED);
#ifdef DEBUG
		if(ret<0)
			perror("waitpid:");
#endif
		newjob->pid=pid;
		return newjob;
	}
}

void do_deq(struct jobcmd deqcmd)
{
	int deqid,i;
	struct waitqueue *p,*prev,*select,*selectprev;
	deqid=atoi(deqcmd.data);

#ifdef DEBUG
	printf("deq jid %d\n",deqid);
#endif

	/*current jodid==deqid,终止当前作业*/
	if (current && current->job->jid ==deqid){
		printf("teminate current job\n");
		kill(current->job->pid,SIGKILL);
		for(i=0;(current->job->cmdarg)[i]!=NULL;i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i]=NULL;
		}
		free(current->job->cmdarg);
		free(current->job);
		free(current);
		current=NULL;
	}
	else{ /* 或者在等待队列中查找deqid */
		select=NULL;
		selectprev=NULL;
		//neal: rearrange the search
		for(i=0;i<3 && select==NULL;++i)
			if(head[i]){
				for(prev=head[i],p=head[i];p!=NULL && p->next!=head[i];prev=p,p=p->next)
					if(p->job->jid==deqid){
						select=p;
						selectprev=prev;
						break;
					}
				if(p->job->jid==deqid){//for the un-reached tail
					select=p;
					selectprev=prev;
				}
				if(select!=NULL && select!=head[i]){
					selectprev->next=select->next;
					select->next=NULL;
				}else if(select!=NULL && select==head[i]){
					if(select==select->next)//if there is only one node
						head[i]=NULL;
					else{//if there is more than one nodes
						struct waitqueue *tail=findtail(i);
						head[i]=head[i]->next;
						tail->next=head[i];
					}
					select->next=NULL;
				}
			}
		if(select){
			for(i=0;(select->job->cmdarg)[i]!=NULL;i++){
				free((select->job->cmdarg)[i]);
				(select->job->cmdarg)[i]=NULL;
			}
			free(select->job->cmdarg);
			free(select->job);
			free(select);
			select=NULL;
		}
	}
}

void do_stat()//TONY
{
	struct waitqueue *p;
	char timebuf[BUFLEN];
	/*
	*打印所有作业的统计信息:
	*1.作业ID
	*2.进程ID
	*3.作业所有者
	*4.作业运行时间
	*5.作业等待时间
	*6.作业创建时间
	*7.作业状态
	*/

	/* 打印信息头部 */
        
        int statFifo;//TONY
        if((statFifo=open("/tmp/os_job_stat",O_WRONLY))<0)//TONY
		error_sys("open statFifo failed");
        
        char do_statBuff[200]={0};//TONY
 	sprintf(do_statBuff,"JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");//TONY
        write(statFifo,do_statBuff,strlen(do_statBuff));//TONY
        
	if(current){
		strcpy(timebuf,ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		sprintf(do_statBuff,"%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			current->job->jid,
			current->job->pid,
			current->job->ownerid,
			current->job->run_time,
			current->job->wait_time,
			timebuf,"RUNNING");//TONY
//                 printf("current\n");
                write(statFifo,do_statBuff,sizeof(do_statBuff));//TONY
                
	}

	//TONY
	int i=0;
	for(i=0;i<3;i++){
            if(head[i]==NULL){
                continue;
            }
            p=head[i];
            do{
                    strcpy(timebuf,ctime(&(p->job->create_time)));
                    timebuf[strlen(timebuf)-1]='\0';
                    sprintf(do_statBuff,"%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
                            p->job->jid,
                            p->job->pid,
                            p->job->ownerid,
                            p->job->run_time,
                            p->job->wait_time,
                            timebuf,
                            "READY");
                    write(statFifo,do_statBuff,strlen(do_statBuff));//输出到管道
                    p=p->next;
            }while(p!=head[i]);
            
        }
        //TONY
	close(statFifo);//TONY
	
}

int main()
{
	struct timeval interval;
	struct itimerval new,old;
	struct stat statbuf;
	struct sigaction newact,oldact1,oldact2,oldact3;//TONY

    //TONY: make fifo
    int pidFile=open("/tmp/os_job_pid",O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);//TONY
        pid_t job_pid = getpid();//TONY
        write(pidFile,&job_pid,sizeof(job_pid));//TONY
        close(pidFile);//TONY
	
        if(stat("/tmp/os_job_stat",&statbuf)==0){//TONY
		/* 如果FIFO文件存在,删掉 */
		if(remove("/tmp/os_job_stat")<0)
                    error_sys("remove failed");
	}
	if(mkfifo("/tmp/os_job_stat",0666)<0)//TONY
		error_sys("mkfifo failed");

	if(stat("/tmp/server",&statbuf)==0){
		/* 如果FIFO文件存在,删掉 */
		if(remove("/tmp/server")<0)
			error_sys("remove failed");
	}

	if(mkfifo("/tmp/server",0666)<0)
		error_sys("mkfifo failed");
	/* 在非阻塞模式下打开FIFO */
	if((fifo=open("/tmp/server",O_RDONLY|O_NONBLOCK))<0)
		error_sys("open fifo failed");

	/* 建立信号处理函数 */
	newact.sa_sigaction=sig_handler;
	sigemptyset(&newact.sa_mask);
	
	sigaddset(&newact.sa_mask,SIGCHLD);//TONY
    sigaddset(&newact.sa_mask,SIGVTALRM);//TONY
    sigaddset(&newact.sa_mask,SIGUSR1);//TONY
	
	newact.sa_flags=SA_SIGINFO;
	sigaction(SIGCHLD,&newact,&oldact1);
	sigaction(SIGVTALRM,&newact,&oldact2);
	sigaction(SIGUSR1,&newact,&oldact3);//TONY

	/* 设置时间间隔为tick毫秒 neal: change the time */
	interval.tv_sec=0;
	interval.tv_usec=tick*1000;

	new.it_interval=interval;
	new.it_value=interval;
	setitimer(ITIMER_VIRTUAL,&new,&old);

	while(siginfo==1);

	close(fifo);
    remove("/tmp/os_job_pid");//TONY
    unlink("/tmp/server");//TONY
    unlink("/tmp/os_job_stat");//TONY
	return 0;
}

void debug_stat(const char *msg)
{
	struct waitqueue *p;
	char timebuf[BUFLEN];
	/*
	*打印所有作业的统计信息:
	*1.作业ID
	*2.进程ID
	*3.作业所有者
	*4.作业运行时间
	*5.作业等待时间
	*6.作业创建时间
	*7.作业状态
	*/

	/* 打印信息头部 */
	fprintf(stderr,msg);
	fprintf(stderr,"\nJOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
	fprintf(stderr,"(current)\n");
	if(current){
		strcpy(timebuf,ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		fprintf(stderr,"%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			current->job->jid,
			current->job->pid,
			current->job->ownerid,
			current->job->run_time,
			current->job->wait_time,
			timebuf,"RUNNING");
	}
	fflush(stderr);
	for(int i=2;i>=0;--i){
		fprintf(stderr,"(queue %d)\n",i);
		for(p=head[i];p!=NULL && p->next!=head[i];p=p->next){
			strcpy(timebuf,ctime(&(p->job->create_time)));
			timebuf[strlen(timebuf)-1]='\0';
			fprintf(stderr,"%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
				p->job->jid,
				p->job->pid,
				p->job->ownerid,
				p->job->run_time,
				p->job->wait_time,
				timebuf,
				"READY");
		}
		if(p){
			strcpy(timebuf,ctime(&(p->job->create_time)));
			timebuf[strlen(timebuf)-1]='\0';
			fprintf(stderr,"%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
				p->job->jid,
				p->job->pid,
				p->job->ownerid,
				p->job->run_time,
				p->job->wait_time,
				timebuf,
				"READY");
		}
		fflush(stderr);
	}
	fprintf(stderr,"\n\n");
	fflush(stderr);
}
