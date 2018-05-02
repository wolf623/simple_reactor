/* This is a unit test program for simple_reactor.c module */

#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <mqueue.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "simple_reactor.h"

/* Case 1:
 *         Two threads, they have their own posix message queue
 *         to communicate with the other, and use timer to whether
 *         there is timeout since last receving
 */         

#if 0
truct timespec {
	time_t tv_sec;                /* Seconds */
	long   tv_nsec;               /* Nanoseconds */
};

struct itimerspec {
	struct timespec it_interval;  /* Interval for periodic timer （定时间隔周期）*/
	struct timespec it_value;     /* Initial expiration (第一次超时时间)*/
};

int timerfd_settime(int fd, int flags, const struct itimerspec *new_value, struct itimerspec *old_value);
//fd: The value return from timerfd_create()
//flags: 1: absolute time, 0: relative time
//new_value: timeout value
//old_value: the old timeout value set before
#endif

const char *mq_name[] = {
	"/reactor_case1_msq0",
	"/reactor_case1_msq1",
};

event_loop_t case1_eloop[2];

int case1_msq_recv(void *user_arg, int mq);
int case1_send_expire(void *user_arg, int timerfd, uint64_t count);
int case1_recv_expire(void *user_arg, int timerfd, uint64_t count);

void *case1_thread_func(void *arg)
{
	int idx = (int)arg;
	mqd_t recvq;
	mqd_t sendq;

	int send_tfd;
	int recv_tfd;

	struct itimerspec send_time = 
	{
		.it_interval = {1, 0}, // periodic
		.it_value = {1, 0},   // timeout in 1 sec
	};

	struct itimerspec recv_time =
	{
		.it_interval = { 0, 0}, // not periodic
		.it_value = {3, 0}, // timeout in 3 sec
	};

	struct mq_attr qattr = {
		.mq_flags = 0,
		.mq_maxmsg = 10, //max msg size?
		.mq_msgsize = 4,
	};

	recvq = mq_open(mq_name[idx], O_RDWR | O_CREAT, 0644, &qattr);
	sendq = mq_open(mq_name[1 - idx], O_RDWR | O_CREAT, 0644, &qattr);

	if(recvq == -1)
		perror("mq_open");
	assert(recvq != -1);
	assert(sendq != -1);

	send_tfd = timerfd_create(CLOCK_MONOTONIC, 0);
	recv_tfd = timerfd_create(CLOCK_MONOTONIC, 0);

	assert(send_tfd != -1);
	assert(recv_tfd != -1);

	
	event_loop_init(&case1_eloop[idx]);

	event_loop_register_msq_actor(&case1_eloop[idx], recvq, case1_msq_recv, NULL, ACTOR_EVENT_READ, (void *)recv_tfd);

	timerfd_settime(send_tfd, 0, &send_time, NULL);
	timerfd_settime(recv_tfd, 0, &recv_time, NULL);

	event_loop_register_timer_actor(&case1_eloop[idx], send_tfd, case1_send_expire, (void *)sendq);
	event_loop_register_timer_actor(&case1_eloop[idx], recv_tfd, case1_recv_expire, (void *)idx);

	// all thing is ok
	event_loop_run(&case1_eloop[idx]);
	printf("event_loop_run return, error\n");
	abort();

	return (void *)NULL;
}

int case1_msq_recv(void *user_arg, int mq)
{
	int recv_tfd = (int)user_arg;
	
	long dummy[10];
	int rc;

	rc = mq_receive(mq, (char *)dummy, sizeof(dummy), NULL);

	if(rc == -1)
	{
		printf("mq_receive size error, errno = %d(%m)\n", errno);
	}
	else if(rc == 0)
	{
		printf("mq_receiv size == 0\n");
	}

	// re-alarm the recevie timer
    struct itimerspec recv_time =
   	{
    	.it_interval = { 0, 0}, // not periodic
        .it_value = {3, 0}, // timeout in 3 sec
    };	

	timerfd_settime(recv_tfd, 0, &recv_time, NULL);

	return 0;
}

int case1_send_expire(void *user_arg, int timerfd, uint64_t count)
{
	printf("count = %llu\n", count);
	assert(count == 1); // no timeout pending

	int sendq = (int)user_arg;
	//long dummy;
	char dummy;

	// send message to message queue
	if(mq_send(sendq, (char *)&dummy, sizeof(dummy), 0))
	{
		printf("send message error, errno = %d(%m)\n", errno);
		abort();
	}

	return 0;

}

int case1_recv_expire(void *user_arg, int timerfd, uint64_t count)
{
	printf("count = %llu\n", count);
	assert(count == 1);

	int idx = (int)user_arg;

	printf("thread %d doesn't receive message queue data with 3 sec\n",
	      idx);
	abort();

	return 0;
}

void test_case1()
{
	pthread_t thread[2];

	mq_unlink(mq_name[0]);
	mq_unlink(mq_name[1]);

	if(pthread_create(&thread[0], NULL, case1_thread_func, (void *)0))
	{
		printf("pthread_create error, errno = %d(%m)\n", errno);
		return;	
	}

	if(pthread_create(&thread[1], NULL, case1_thread_func, (void *)1))
	{
		printf("pthread_create error, errno = %d(%m)\n", errno);
		return;
	}

	sleep(60);
	pthread_cancel(thread[0]);
	pthread_cancel(thread[1]);

	pthread_join(thread[0], NULL);
	pthread_join(thread[1], NULL);
}

int main()
{
	test_case1();

	return 0;
}
