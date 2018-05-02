#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <stdint.h>
#include <time.h>

#define ACTOR_EVENT_READ  0x01
#define ACTOR_EVENT_WRITE 0x02

/* posix message queue tiplexer */
typedef struct msq_actor_s {
	mqd_t mq;
	int (*recv_func)(void *user_arg, mqd_t mq); 
	int (*send_func)(void *user_arg, mqd_t mq);
	unsigned int events;
	void *user_arg;
} msq_actor_t;

/* socket tiplexer */
typedef struct sock_actor_s {
	int sd;
	int (*recv_func)(void *user_arg, int sd);  
	int (*send_func)(void *user_arg, int sd);
	unsigned int events;
	void *user_arg;
} sock_actor_t;

/* timer actor */
/* timer actor is a readonly object,
 * there is not write functiion callback and events */
typedef struct timer_actor_s {
	int timerfd;
	int (*expire)(void *user_arg, int timerfd, uint64_t count);
	void *user_arg;
} timer_actor_t;

/* the signal Eventloop object, there is only one in each process/thread */
#define MAX_MSQ_NUM   32
#define MQX_SOCK_NUM  256
#define MAX_TIMER_NUM 32

typedef struct even_loop_s {
	msq_actor_t msqs[MAX_MSQ_NUM];
	size_t msq_num;

	sock_actor_t socks[MQX_SOCK_NUM];
	size_t sock_num;

	timer_actor_t timers[MAX_TIMER_NUM];
	size_t timer_num;
} event_loop_t;

/* function prototype */
void event_loop_init(event_loop_t *eloop);
int  event_loop_register_msq_actor(event_loop_t *eloop,
                                   mqd_t mq,
				   int (*recv_func)(void *user_arg, mqd_t mq),
				   int (*send_func)(void *user_arg, mqd_t mq),
				   unsigned int events,
				   void *user_arg);
int  event_loop_unregister_msq_actor(event_loop_t *eloop, mqd_t mq);

int  event_loop_register_sock_actor(event_loop_t *eloop,
                                    int sd,
				    int (*recv_func)(void *user_arg, int sd),
				    int (*send_func)(void *user_arg, int sd),
				    unsigned int events,
				    void *user_arg);
int  event_loop_unregister_sock_actor(event_loop_t *eloop, int sd);				    

int  event_loop_register_timer_actor(event_loop_t *eloop,
                                     int timerfd,
				     int (*expire)(void *user_arg, int timerfd, uint64_t count),
				     void *user_arg);
int  event_loop_unregister_timer_actor(event_loop_t *eloop, int timerfd);

void event_loop_run(event_loop_t *eloop);

/* timerfd_create, timerfd_settime, timerfd_gettime prototype */
int timerfd_create(int clockid, int flags);
int timerfd_settime(int fd, int flags,
                    const struct itimerspec *new_value,
                    struct itimerspec *old_value);
int timerfd_gettime(int fd, struct itimerspec *curr_value);

