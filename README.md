# MyTinyWebServer

## 项目简介

Linux下C++轻量级Web服务器，阅读了游双老师的——linux高性能服务器编程，但是不知道怎样将各个知识点联系起来，于是参考了——[qinguoyi](https://github.com/qinguoyi)/**[TinyWebServer](https://github.com/qinguoyi/TinyWebServer)**的原始版本，代码基本没改变过，只是添加了一些注释。



## 代码简介

关于代码，在网上已经有了很详细的讲述，这里就不做细节讨论，仅对代码大纲做个描述。

关于主线程，它负责处理client的消息的接收以及发送，当读取完毕后，将该任务添加进线程池的任务队列中，然后唤醒子进程

关于子线程，由子线程处理，读取的内容 以及 该写入什么内容给客户端,.........

关于定时器，可以实现更加高效的时间轮、最小堆.....

关于日志系统，循环队列+异步/同步.......



## 致谢

Linux高性能服务器编程，游双著.

[qinguoyi](https://github.com/qinguoyi)/**[TinyWebServer](https://github.com/qinguoyi/TinyWebServer)**