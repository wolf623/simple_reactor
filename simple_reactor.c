
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <mqueue.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <asm/unistd.h>  /* here we mean the one from the tools directory */
#include <time.h>

#include "simple_reactor.h"

void event_loop_init(event_loop_t *eloop)
{
	memset(eloop, 0, sizeof(*eloop));
}

int event_loop_register_msq_actor(event_loop_t *eloop,  
	                          mqd_t mq,
	                          int (*recv_func)(void *user_arg, mqd_t mq),
	                          int (*send_func)(void *user_arg, mqd_t mq),
	                          unsigned int events,
	                          void *user_arg)
{
	int i;

	if(eloop->msq_num >= MAX_MSQ_NUM)
	{
		printf("Too many message queue actor\n");
		return 1;
	}

	for(i = 0; i < eloop->msq_num; ++i)
	{
		if(mq == eloop->msqs[i].mq)
		{
			printf("message queue %d have already register in this event loop \n",
			       mq);
			return 1;       
		}
	}

	msq_actor_t *msq_actor = &(eloop->msqs[eloop->msq_num]);
	msq_actor->recv_func = recv_func;
	msq_actor->send_func = send_func;
	msq_actor->events = events;

	if((events & ACTOR_EVENT_READ) && recv_func == NULL)
	{
		printf("register Read event, but don't provide a read callback\n");
		return 1;
	}

	if((events & ACTOR_EVENT_WRITE) && send_func == NULL)
	{
		printf("Register send event, but don't provide a send callback\n");
		return 1;
	}

	if(!(events & (ACTOR_EVENT_READ | ACTOR_EVENT_WRITE)))
	{
		printf("Event don't turn on events(read and write)\n");
		return 1;
	}

	msq_actor->mq = mq;
	msq_actor->user_arg = user_arg;

	eloop->msq_num++;

	return 0;
}

int event_loop_unregister_msq_actor(event_loop_t *eloop, mqd_t mq)
{
	int i;

	for(i = 0; i < eloop->msq_num; ++i)
	{
		if(eloop->msqs[i].mq == mq)
			break;
	}

	if(i == eloop->msq_num)
	{
		printf("message queue %d have not been registered to event loop\n", mq);
		return 1;
	}

	memcpy(&eloop->msqs[i], &eloop->msqs[eloop->msq_num - 1], sizeof(eloop->msqs[i]));
	eloop->msq_num--;

	return 0;
}

int event_loop_register_sock_actor(event_loop_t *eloop,
	                           int sd,
				   int (*recv_func)(void *user_arg, int sd),
				   int (*send_func)(void *user_arg, int sd),
				   unsigned int events,
				   void *user_arg)
{	int i;

	if(eloop->sock_num >= MAX_MSQ_NUM)
	{
		printf("Too many socket actor\n");
		return 1;
	}

	for(i = 0; i < eloop->sock_num; ++i)
	{
		if(sd == eloop->socks[i].sd)
		{
			printf("socket %d have already register in this event loop \n",
			       sd);
			return 1;       
		}
	}

	sock_actor_t *sock_actor = &(eloop->socks[eloop->sock_num]);
	sock_actor->recv_func = recv_func;
	sock_actor->send_func = send_func;
	sock_actor->events = events;

	if((events & ACTOR_EVENT_READ) && recv_func == NULL)
	{
		printf("register Read event, but don't provide a read callback\n");
		return 1;
	}

	if((events & ACTOR_EVENT_WRITE) && send_func == NULL)
	{
		printf("Register send event, but don't provide a send callback\n");
		return 1;
	}

	if(events &(ACTOR_EVENT_READ | ACTOR_EVENT_WRITE))
	{
		printf("Event don't turn on events(read and write)\n");
		return 1;
	}

	sock_actor->sd = sd;
	sock_actor->user_arg = user_arg;

	eloop->sock_num++;

	return 0;

}


int event_loop_unregister_sock_actor(event_loop_t *eloop, int sd)
{
        int i;

        for(i = 0; i < eloop->sock_num; ++i)
        {
                if(eloop->socks[i].sd == sd)
                        break;
        }

        if(i == eloop->sock_num)
        {
                printf("socket %d have not been registered to event loop\n", sd);
                return 1;
        }

        memcpy(&eloop->socks[i], &eloop->socks[eloop->sock_num - 1], sizeof(eloop->socks[i]));
        eloop->sock_num--;

        return 0;
}

int event_loop_register_timer_actor(event_loop_t *eloop,
	                            int timerfd,
                                    int (*expire)(void *user_arg, int timerfd, uint64_t count),
				    void *user_arg)
{
	int i;

	if(eloop->timer_num >= MAX_TIMER_NUM)
	{
		printf("Too many timer in event loop\n");
		return 1;
	}

	for(i = 0; i < eloop->timer_num; ++i)
	{
		if(eloop->timers[i].timerfd == timerfd)
		{
			printf("timerfd %d has been register before\n", timerfd);
			return 1;
		}
	}

	timer_actor_t *timer_actor = &(eloop->timers[eloop->timer_num]);
	timer_actor->expire = expire;
	timer_actor->timerfd = timerfd;
	timer_actor->user_arg = user_arg;

	eloop->timer_num++;

	return 0;
}

int event_loop_unregister_timer_actor(event_loop_t *eloop, int timerfd)
{
        int i;

        for(i = 0; i < eloop->timer_num; ++i)
        {
                if(eloop->timers[i].timerfd == timerfd)
                        break;
        }

        if(i == eloop->timer_num)
        {
                printf("timerfd %d have not been registered to event loop\n", timerfd);
                return 1;
        }

        memcpy(&eloop->timers[i], &eloop->timers[eloop->timer_num - 1], sizeof(eloop->timers[i]));
        eloop->timer_num--;

        return 0;
}

/*
 * The reactor pattern Main Loop function
 *
 * It use select(can use poll/epoll or similar function for the perfomance issue)
 * to wait all actors' events(readable or writable event). If event reach, it
 * call registered callback function to dispatch the event.
 *
 * In the callback function, can register new actor or unregister old actor according to
 * the design demand; also can change the attribute of actors(such as re-aram the timer)
 */
void event_loop_run(event_loop_t *eloop)
{
	fd_set rd;
	fd_set wd;

	int i;
	int maxfd = -1;
	size_t count = 0;

	while(1)
	{
		FD_ZERO(&rd);
		FD_ZERO(&wd);

		maxfd = -1;
		count = 0;

		for(i = 0; i < eloop->msq_num; ++i)
		{
			if(eloop->msqs[i].events & ACTOR_EVENT_READ)
			{
				FD_SET(eloop->msqs[i].mq, &rd);
				count++;
			}

			if(eloop->msqs[i].events & ACTOR_EVENT_WRITE)
			{
				FD_SET(eloop->msqs[i].mq, &wd);
				count++;
			}

			/* Assure
			 * each message queue at lest register Read or Write event */
			if(maxfd < eloop->msqs[i].mq)
			{
				maxfd = eloop->msqs[i].mq;
			}
		}

		for(i = 0; i < eloop->sock_num; ++i)
		{
			if(eloop->socks[i].events & ACTOR_EVENT_READ)
			{
				FD_SET(eloop->socks[i].sd, &rd);
				count++;
			}

			if(eloop->socks[i].events & ACTOR_EVENT_WRITE)
			{
				FD_SET(eloop->socks[i].sd, &wd);
				count++;
			}

			/* Assure
			 * each socket must at lest register Read or Write event */
			if(maxfd < eloop->socks[i].sd)
			{
				maxfd = eloop->socks[i].sd;
			}
		}

		for(i = 0; i < eloop->timer_num; ++i)
		{
			FD_SET(eloop->timers[i].timerfd, &rd);	
			count++;

			if(maxfd < eloop->timers[i].timerfd)
			{
				maxfd = eloop->timers[i].timerfd;
			}
		}

		if(count == 0)
		{
			// What Happen???
			// All actors was removed
			printf("This is a bug, All Actors was removed from this event loop,"
			       "shoule not call %s function\n", __func__);
			return;       
		}

		int rc;
		rc = select(maxfd + 1, &rd, &wd, NULL, NULL);

		if(rc == -1)
		{
			if(errno == EINTR)
			{
				continue;
			}
			else
			{
				printf("select error, errno %d(%m)\n", errno);
				continue;
			}
		}

		if(rc == 0)
		{
			// Impossible
			printf("select return 0, but we don't specific timeout arg.\n");
			continue;
		}

		// check all actors and dispatch to them
		
		// timer is the first class object, check it first
		for(i = 0; i < eloop->timer_num && rc > 0; ++i)
		{
			if(FD_ISSET(eloop->timers[i].timerfd, &rd))
			{
				uint64_t count;	
				int timer_rc;

				rc--;
				timer_rc = read(eloop->timers[i].timerfd, &count, sizeof(count));

				if(timer_rc == -1)
				{
					if(errno != EINTR)
					{
						printf("read timerfd %d error, errno = %d(%m)\n", 
						    eloop->timers[i].timerfd, errno);
					}

					continue;	
				}
				else if(timer_rc != sizeof(count))
				{
					printf("read timerfd %d size = %d\n", eloop->timers[i].timerfd, timer_rc);
				}

				eloop->timers[i].expire(eloop->timers[i].user_arg,
				                        eloop->timers[i].timerfd,
							count);
			}
		}

		// Check message queue
		for(i = 0; i < eloop->msq_num && rc > 0; ++i)
		{
			if((eloop->msqs[i].events & ACTOR_EVENT_READ)
			 && FD_ISSET(eloop->msqs[i].mq, &rd))
			{
				rc--;
				eloop->msqs[i].recv_func(eloop->msqs[i].user_arg,
				                         eloop->msqs[i].mq);
			}

			if((eloop->msqs[i].events & ACTOR_EVENT_WRITE)
			&& FD_ISSET(eloop->msqs[i].mq, &wd))
			{
				rc--;
				eloop->msqs[i].send_func(eloop->msqs[i].user_arg,
				                         eloop->msqs[i].mq);
			}
		}

		// Check socket
		for(i = 0; i < eloop->sock_num && rc > 0; ++i)
		{
			if((eloop->socks[i].events & ACTOR_EVENT_READ)
			&& FD_ISSET(eloop->socks[i].sd, &rd))
			{
				rc--;
				eloop->socks[i].recv_func(eloop->socks[i].user_arg,
				                          eloop->socks[i].sd);
			}

			if((eloop->socks[i].events & ACTOR_EVENT_WRITE)
			&& FD_ISSET(eloop->socks[i].sd, &wd))
			{
				rc--;
				eloop->socks[i].send_func(eloop->socks[i].user_arg,
				                          eloop->socks[i].sd);
			}

		}
	}
}

/*
 *  timerfd_XXX function wrapper
 *  our kernel has provide these timerfd_XXX system call,
 *  but glibc has not provide these wrpper function.
 *  Implement it!!!
 */

#ifndef __NR_timerfd_create

  #if _MIPS_SIM == _MIPS_SIM_ABI32
    #define __NR_timerfd_create              (__NR_Linux + 321)
  #endif

  #if _MIPS_SIM == _MIPS_SIM_ABI64	
    #define __NR_timerfd_create              (__NR_Linux + 280)
  #endif

  #ifndef __NR_timerfd_create
    #error "__NR_timerfd_create defined error"
  #endif
#else  /* why error if it's already defined in the system ???? */
//  #error "glibc has already defined __NR_timerfd_create, please remove your definitions"
#endif /* __NR_timerfd_create */	

#ifndef __NR_timerfd_settime

  #if _MIPS_SIM == _MIPS_SIM_ABI32
    #define __NR_timerfd_settime             (__NR_Linux + 323)
  #endif

  #if _MIPS_SIM == _MIPS_SIM_ABI64
    #define __NR_timerfd_settime             (__NR_Linux + 282)
  #endif

  #ifndef __NR_timerfd_settime
    #error"__NR_timerfd_settime defined error"
  #endif

#else
//  #error "glibc has already definied __NR_timerfd_settime, please remove your definitions"
#endif /* __NR_timerfd_settime */

#ifndef __NR_timerfd_gettime

  #if _MIPS_SIM == _MIPS_SIM_ABI32
    #define __NR_timerfd_gettime             (__NR_Linux + 322)
  #endif

  #if _MIPS_SIM == _MIPS_SIM_ABI64
    #define __NR_timerfd_gettime             (__NR_Linux + 281)
  #endif

  #ifndef __NR_timerfd_gettime
    #error "__NR_timerfd_gettime defined error"
  #endif

#else  
//  #error "glibc has already defined __NR_timerfd_gettime, please remove your defintions"
#endif /* __NR_timerfd_gettime */

#ifndef SYS_timerfd_create
  #define SYS_timerfd_create __NR_timerfd_create
#else
//  #error "glibc has already defined SYS_timerfd_create, please remove your definitions"
#endif

#ifndef SYS_timerfd_settime
  #define SYS_timerfd_settime  __NR_timerfd_settime
#else
//  #error "glibc has already defined SYS_timerfd_settime, please remove your definitions"
#endif

#ifndef SYS_timerfd_gettime
  #define SYS_timerfd_gettime  __NR_timerfd_gettime
#else
//  #error "glibc has already define SYS_timerfd_gettime, please remove your definitions"
#endif

int timerfd_create(int clockid, int flags)
{
	return syscall(SYS_timerfd_create, clockid, flags);
}

int timerfd_settime(int fd, int flags,
                    const struct itimerspec *new_value,
		    struct itimerspec *old_value)
{
	return syscall(SYS_timerfd_settime, fd, flags, new_value, old_value);
}

int timerfd_gettime(int fd, struct itimerspec *curr_value)
{
	return syscall(SYS_timerfd_gettime, fd, curr_value);
}
