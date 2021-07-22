#pragma once
#include <sys/socket.h>
#include <ucontext.h>
#include <unistd.h>

typedef struct coroutine_data *co_task;
typedef void (*co_func)(void *);
//�ڷ�Э���������п���Э��
void co_init();
void co_main(bool flag_once = false);
void co_yield();
void co_suspend();
void co_resume(co_task t);
co_task co_create(co_func f, void *param);
void set_idle_sleep(int ms);
// void co_destory(co_task t);
// bool is_terminated(co_task t);



void co_add_socket_support(int epoll_max_cnt);
bool co_has_socket_support();

void socket_register(int fd);
void socket_remove(int fd);
//����socket��ע�ᡢ���÷�����
int co_socket(int domain, int type, int protocol);
//������socket�� �Ƴ�socket���ر�
void co_close(int fd);

bool co_recv(int fd, void *buf, ssize_t n, int flags = 0);
bool co_send(int fd, const void *buf, ssize_t n, int flags = 0);
int co_accept(int fd, __SOCKADDR_ARG addr, socklen_t *__restrict addr_len);
bool ignore_sigpipe();
bool setnonblocking(int sockfd);
bool setreuseaddr(int fd);
bool set_keepalive(int fd, int time /*���û����������,�����̽��*/, int cnt /*̽�����*/, int interval /*̽����*/);
size_t co_activetask_count();

size_t co_suspendedtask_count();

size_t co_task_count();
