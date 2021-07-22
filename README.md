# netco
简单的linux协程库，主要用于服务器开发，使开发人员能以同步的方式编写服务器。

目前支持recv,send和accept的协程版本:co_recv,co_send和co_accept.调用时，他们将挂起当前协程，并启动一个网络操作代理。集中地使用epoll监控所有套接字。

其中，co_recv和co_send为对应系统api的增强版本：
+ co_recv发送完所有数据才会返回。
+ co_send接收完指定长度的数据才会返回（为避免过长时间阻塞，应设置keepalive）。
