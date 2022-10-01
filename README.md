# Threadpool
基于 C++ 实现的线程池库。
支持两种工作模式：固定线程数量模式（MODE_FIX）和缓存线程模式（MODE_CACHE）。

使用可变参模板编程，让submitTask可以接收任意任务函数和任意数量的参数。

使用了 C++11 中的 `std::packaged_task` 和 `std::future` 技术来实现异步任务的执行和结果获取。
### 提交任务获取结果
```
//创建线程池
ThreadPool pool; 
pool.start();

// 定义一个任务函数
void myTask(int arg) {
    // 任务逻辑
}

// 提交任务给线程池执行
auto future = pool.submitTask(myTask, 42);

// 获取任务执行结果
int result = future.get();
```




