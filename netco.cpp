#include "netco.h"

#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <cassert>
#include <csignal>
#include <cstring>
#include <memory>
#include <vector>

#include "delayed_list.h"
#include "preprocessors.h"



enum class co_state { CREATED, RUNNABLE, SUSPENDED, TERMINATED };
#define SHARED_STACK_SIZE 16777216  // 1024*1024*16
#define EPOLLEVENT_BUF_SIZE 1024
enum class cosocket_event : unsigned char {
  NUL = 0,
  RECV,
  SEND,
  ACCEPT,
};
struct cosocket_delegate {
  cosocket_event e;
  int fd;
  ssize_t size;
  void* pbuf;
  int flag;
  ssize_t ret;
  int err;
};
struct coroutine_data {
  ucontext_t ctx;
  struct {
    void* p;
    int size;
  } stack;
  void* retval;
  co_state state;
  cosocket_delegate* pcsd;
};
struct co_wrapper_param {
  co_func f;
  void* param;
};
thread_local struct scheduler_data {
  char shared_stack[SHARED_STACK_SIZE];
  co_task cur_task;
  ucontext_t main_ctx;
  co_wrapper_param wrapper_param;
  delayed_list<coroutine_data> v_runnable;
  std::vector<std::pair<co_task, co_wrapper_param>> v_created;
  int fd_epoll;
  epoll_event event_buf[EPOLLEVENT_BUF_SIZE];
  int idle_sleep_us;
  int active_count, suspended_count;
} * scheduler;
// int xxx = sizeof(scheduler_data);

void save_stack(co_task t) {
  const size_t long_size = scheduler->shared_stack + SHARED_STACK_SIZE - reinterpret_cast<char*>(&t);
  ASSERT(long_size < SHARED_STACK_SIZE);
  const int size = static_cast<int>(long_size);
  if (size > t->stack.size) {
    if (t->stack.p != nullptr) free(t->stack.p);
    t->stack.p = malloc(size);
    t->stack.size = size;
  }
  // printf("save_current_stack(%p):(%p,%d)\n", scheduler->cur_task, scheduler->cur_task->stack.p, size);
  memcpy(scheduler->cur_task->stack.p, &t, size);
}
void co_destory(co_task t) {
  ASSERT(scheduler->cur_task == nullptr);
  ASSERT(t->state == co_state::TERMINATED);
  free(t->stack.p);
  delete t;
}
bool co_run(co_task t) {
  ASSERT(scheduler->cur_task == nullptr);
  ASSERT(t->state == co_state::RUNNABLE || t->state == co_state::CREATED);
  if (t->stack.p != nullptr) {
    // printf("load_stack(%p):(%p,%d)\n", t, t->stack.p, t->stack.size);
    memcpy(scheduler->shared_stack + SHARED_STACK_SIZE - t->stack.size, t->stack.p, t->stack.size);
  }
  scheduler->cur_task = t;

  if (t->state == co_state::CREATED) {
    swapcontext(&scheduler->main_ctx, &t->ctx);
    ASSERT(t->state == co_state::RUNNABLE);
    scheduler->active_count++;
  } else {
    swapcontext(&scheduler->main_ctx, &t->ctx);
    ASSERT(t->state == co_state::RUNNABLE);
  }

  scheduler->cur_task = nullptr;
  if (t->state == co_state::RUNNABLE) {
    return false;
  } else if (t->state == co_state::SUSPENDED) {
    scheduler->active_count--;
    scheduler->suspended_count++;
    return true;
  } else if (t->state == co_state::TERMINATED) {
    co_destory(t);
    return true;
  } else {
    VERIFY(0);
  }
}

void co_init() {
  VERIFYX(scheduler == nullptr, "multiple initialization");
  scheduler = new scheduler_data();
  scheduler->fd_epoll = -1;
}
void co_yield() {
  save_stack(scheduler->cur_task);
  swapcontext(&scheduler->cur_task->ctx, &scheduler->main_ctx);
}
void co_suspend() {
  scheduler->cur_task->state = co_state::SUSPENDED;
  co_yield();
}
void co_resume(co_task t) {
  ASSERT(t->state == co_state::SUSPENDED);
  t->state = co_state::RUNNABLE;
  scheduler->v_runnable.push(t);
  scheduler->suspended_count--;
  scheduler->active_count++;
}
void cotaskfunc_wrapper() {
  co_func f = scheduler->wrapper_param.f;
  void* param = scheduler->wrapper_param.param;
  scheduler->cur_task->state = co_state::RUNNABLE;
  scheduler->v_runnable.push(scheduler->cur_task);
  co_yield();
  f(param);
  scheduler->cur_task->state = co_state::TERMINATED;
  co_yield();
}
co_task co_create(co_func f, void* param) {
  co_task t = new coroutine_data();
  t->stack.p = nullptr;
  t->stack.size = 0;
  t->state = co_state::CREATED;

  // printf("co_create: %p , cur_task:%p\n", t, scheduler->cur_task);

  getcontext(&t->ctx);
  t->ctx.uc_link = &scheduler->main_ctx;
  t->ctx.uc_stack.ss_sp = scheduler->shared_stack;
  t->ctx.uc_stack.ss_size = SHARED_STACK_SIZE;
  t->ctx.uc_stack.ss_flags = 0;
  makecontext(&t->ctx, cotaskfunc_wrapper, 0);

  scheduler->v_created.push_back(std::make_pair(t, co_wrapper_param{f, param}));
  return t;
}

void set_idle_sleep(int ms) { scheduler->idle_sleep_us = ms * 1000; }

void cosocket_service_process();

void co_main(bool flag_once /* = false*/) {
  do {
    for (auto& pair : scheduler->v_created) {
      scheduler->wrapper_param.param = pair.second.param;
      scheduler->wrapper_param.f = pair.second.f;
      co_run(pair.first);
    }
    scheduler->v_created.clear();

    if (co_has_socket_support()) cosocket_service_process();

    if (scheduler->v_runnable.size() != 0) {
      scheduler->v_runnable.for_each(co_run);
    } else {
      //若没有co_socket支持，在这休眠
      //否则应在cosocket_service_process的epoll中设置time_out
      if (!co_has_socket_support() && scheduler->v_created.size() == 0 && scheduler->idle_sleep_us != 0) {
        usleep(scheduler->idle_sleep_us);
      }
    }

  } while (!flag_once);
}

//---------------------------------------------------------
void co_add_socket_support(int epoll_max_cnt) { scheduler->fd_epoll = epoll_create(epoll_max_cnt); }

bool co_has_socket_support() { return scheduler->fd_epoll != -1; }


void epoll_add(int fd, epoll_event* e) {
  int ret = epoll_ctl(scheduler->fd_epoll, EPOLL_CTL_ADD, fd, e);
  VERIFYX(ret == 0, "add failed!");
}
void epoll_remove(int fd) {
  int ret = epoll_ctl(scheduler->fd_epoll, EPOLL_CTL_DEL, fd, NULL);
  VERIFYX(ret == 0, "remove failed!");
}
void epoll_mod(int fd, epoll_event* e) {
  int ret = epoll_ctl(scheduler->fd_epoll, EPOLL_CTL_MOD, fd, e);
  VERIFYX(ret == 0, "mod failed!");
}
void epoll_mod(int fd, uint32_t events, void* ptr) {
  epoll_event e;
  e.events = events;
  e.data.ptr = ptr;
  epoll_mod(fd, &e);
}


void socket_register(int fd) {
  epoll_event e;
  e.events = 0;
  e.data.ptr = nullptr;
  epoll_add(fd, &e);
}
void socket_remove(int fd) { epoll_remove(fd); }
int co_socket(int domain, int type, int protocol) {
  int fd = socket(domain, type, protocol);
  bool ret = setnonblocking(fd);
  if (!ret) return -1;
  ret = setreuseaddr(fd);
  if (!ret) return -1;
  socket_register(fd);
  return fd;
}
void co_close(int fd) {
  socket_remove(fd);
  close(fd);
}

void cosocket_service_process() {
  ASSERT(scheduler->v_created.size() == 0);
  epoll_event* const event_buf = scheduler->event_buf;
l_begin:
  //有co_socket支持，在此休眠
  int timeout = 0;
  if (scheduler->v_runnable.size() == 0) timeout = -1;  //此时没有RUNNABLE或CREATED任务，永远等待
  int ecnt = epoll_wait(scheduler->fd_epoll, scheduler->event_buf, EPOLLEVENT_BUF_SIZE, timeout);
  ASSERT(scheduler->suspended_count >= ecnt);
  for (int i = 0; i < ecnt; i++) {
    co_task t = (co_task)event_buf[i].data.ptr;

    int fd = t->pcsd->fd;

    switch (t->pcsd->e) {
      case cosocket_event::RECV: {
        auto info = t->pcsd;
        if (event_buf[i].events & EPOLLHUP) {
          info->ret = -1;
          epoll_mod(fd, 0, t);
          co_resume(t);
          break;
        }
        ASSERT(event_buf[i].events & EPOLLIN);
        ssize_t ret = recv(fd, MOVP(info->pbuf, info->ret), info->size - info->ret, info->flag);
        // fprintf(stderr, "dele_recv:%ld\n", ret);
        if (ret > 0) {
          info->ret += ret;
          if (info->ret == info->size) {  //完毕
            epoll_mod(fd, 0, t);
            co_resume(t);
            break;
          }
        } else if (ret == 0) {  //对端断开链接
          info->ret = 0;
          epoll_mod(fd, 0, t);
          co_resume(t);
          break;
        } else {
          ASSERT(ret == -1);
          if (errno != EAGAIN) {  //出错
            epoll_mod(fd, 0, t);
            co_resume(t);
            info->ret = -1;
            info->err = errno;
            break;
          }
        }
        break;
      }
      case cosocket_event::SEND: {
        auto info = t->pcsd;
        if (event_buf[i].events & EPOLLHUP) {
          info->ret = -1;
          epoll_mod(fd, 0, t);
          co_resume(t);
          break;
        }
        ASSERT(event_buf[i].events & EPOLLOUT);
        ssize_t ret = send(fd, MOVP(info->pbuf, info->ret), info->size - info->ret, info->flag);
        if (ret > 0) {
          info->ret += ret;
          if (info->ret == info->size) {  //完毕
            epoll_mod(fd, 0, t);
            co_resume(t);
            break;
          }
        } else if (ret == 0) {  //对端断开链接
          info->ret = 0;
          epoll_mod(fd, 0, t);
          co_resume(t);
          break;
        } else {
          ASSERT(ret == -1);
          if (errno != EAGAIN) {  //出错
            epoll_mod(fd, 0, t);
            co_resume(t);
            info->ret = -1;
            info->err = errno;
            break;
          }
        }
        break;
      }
      case cosocket_event::ACCEPT: {
        co_resume(t);
        break;
      }
      default: {
        ASSERTX(0, "unknown delegate");
      }
    }
  }
  if (ecnt == EPOLLEVENT_BUF_SIZE)  //可能有未读取的事件
    goto l_begin;
}




bool co_recv(int fd, void* buf, ssize_t n, int flags /* = 0*/) {
  ASSERTX(buf < scheduler->shared_stack || buf > scheduler->shared_stack + SHARED_STACK_SIZE,
          "Passing the address of variables in stack across corotines.");
  ssize_t ret = recv(fd, buf, n, flags);
  if (ret > 0) {
    ASSERT(ret <= n);
    if (ret == n) return true;
  } else if (ret == 0) {
    return false;
  } else {
    ASSERT(ret == -1);
    if (errno != EAGAIN) {
      return false;
    }
    ret = 0;
  }

  cosocket_delegate* csd = new cosocket_delegate();  //不能在栈上创建，因为挂起后数据位置改变
  csd->fd = fd;
  csd->e = cosocket_event::RECV;
  csd->ret = 0;
  csd->pbuf = MOVP(buf, ret);
  csd->size = n - ret;
  csd->flag = flags;
  scheduler->cur_task->pcsd = csd;
  epoll_mod(fd, EPOLLIN, scheduler->cur_task);
  co_suspend();
  if (csd->ret == -1) return false;
  ASSERT(ret + csd->ret == n);
  delete csd;
  scheduler->cur_task->pcsd = nullptr;
  return true;
}

bool co_send(int fd, const void* buf, ssize_t n, int flags /* = 0*/) {
  ssize_t ret = send(fd, buf, n, flags);
  if (ret > 0) {
    ASSERT(ret <= n);
    if (ret == n) return true;
  } else if (ret == 0) {
    return false;
  } else {
    ASSERT(ret == -1);
    if (errno != EAGAIN) {
      return false;
    }
    ret = 0;
  }

  cosocket_delegate* csd = new cosocket_delegate();  //不能在栈上创建，因为挂起后数据位置改变
  csd->fd = fd;
  csd->e = cosocket_event::SEND;
  csd->ret = 0;
  csd->pbuf = MOVP(buf, ret);
  csd->size = n - ret;
  csd->flag = flags;
  scheduler->cur_task->pcsd = csd;
  epoll_mod(fd, EPOLLOUT, scheduler->cur_task);
  co_suspend();
  if (csd->ret == -1) return false;
  ASSERT(ret + csd->ret == n);
  delete csd;
  scheduler->cur_task->pcsd = nullptr;
  return true;
}

int co_accept(int fd, __SOCKADDR_ARG addr, socklen_t* __restrict addr_len) {
  epoll_mod(fd, EPOLLONESHOT | EPOLLIN, scheduler->cur_task);
  cosocket_delegate* csd = new cosocket_delegate();  //不能在栈上创建，因为挂起后数据位置改变
  csd->e = cosocket_event::ACCEPT;
  scheduler->cur_task->pcsd = csd;
  co_suspend();
  delete csd;
  scheduler->cur_task->pcsd = nullptr;
  return accept(fd, addr, addr_len);
}


bool ignore_sigpipe() {
  sigset_t set;
  sigprocmask(SIG_SETMASK, NULL, &set);
  sigaddset(&set, SIGPIPE);
  return sigprocmask(SIG_SETMASK, &set, NULL) == 0;
}
bool setnonblocking(int sockfd) {
  int opts;
  opts = fcntl(sockfd, F_GETFL);
  if (opts == -1) return false;
  opts |= O_NONBLOCK;
  if (fcntl(sockfd, F_SETFL, opts) == -1) return false;
  return true;
}
bool setreuseaddr(int fd) {
  int opt;
  return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0;
}
bool set_keepalive(int fd, int time, int cnt, int interval) {
  int keepAlive = 1;  // 开启keepalive属性
  int ret;
  ret = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void*)&keepAlive, sizeof(keepAlive));
  if (ret != 0) return false;
  ret = setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, (void*)&time, sizeof(time));
  if (ret != 0) return false;
  ret = setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, (void*)&interval, sizeof(interval));
  if (ret != 0) return false;
  ret = setsockopt(fd, SOL_TCP, TCP_KEEPCNT, (void*)&cnt, sizeof(cnt));
  if (ret != 0) return false;
  return true;
}

size_t co_activetask_count() { return scheduler->active_count; }
size_t co_suspendedtask_count() { return scheduler->suspended_count; }
size_t co_createdtask_count() { return scheduler->v_created.size(); }
size_t co_task_count() { return scheduler->active_count + scheduler->suspended_count + scheduler->v_created.size(); }
