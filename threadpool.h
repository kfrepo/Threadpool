#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <memory>
#include <future>

const int THREAD_THRESHOLD = 100;//线程池最大线程数量
const int TASK_THRESHOLD = 1024;//任务队列数量阈值
const int THREAD_IDLE_THRESHOLD = 60;//线程最大空闲时间 60s

/**
 * 线程池类型
 */
enum class PoolMode {
    MODE_FIX,
    MODE_CACHE
};


// 线程类型
class Thread {
public:
    // 线程函数对象类型
    using ThreadFunc = std::function<void(int)>;

    explicit Thread(ThreadFunc func) : func_(func), threadId_(generateId_++) {}

    ~Thread() = default;

    void start() {
        //创建线程执行线程函数
        std::thread t(func_, threadId_);
        //分离线程
        t.detach();
    }

    int getId() const {
        return threadId_;
    }

private:
    // 线程的执行函数。
    ThreadFunc func_;
    static int generateId_;
    int threadId_;
};

int Thread::generateId_ = 1;

// 线程池类型
class ThreadPool {
public:
    ThreadPool() :
            initThreadSize_(0),
            idleThreadSize_(0),
            currentThreadSize_(0),
            threadSizeThreshold_(THREAD_THRESHOLD),
            taskSize_(0),
            taskQueueThreshold_(TASK_THRESHOLD),
            poolMode_(PoolMode::MODE_FIX),
            isRunning_(false) {}

    ~ThreadPool() {
        std::cout << "~ThreadPool!" << std::endl;

        //回收资源
        isRunning_ = false;
        std::unique_lock<std::mutex> lock(queueMutex_);
        //等待所有线程结束
        notEmpty_cv.notify_all();
        exitCond_.wait(lock, [&]() -> bool { return threads_.empty(); });
        std::cout << "~ThreadPool!" << std::endl;
    }

    // 设置线程池工作模式
    void setMode(PoolMode mode) {
        if (checkRunningState()) {
            return;
        }
        poolMode_ = mode;
    }

    void setInitThreadSize(size_t size);

    // 设置线程数量阈值 cache模式
    void setThreadSizeThreshold(int threshold) {
        if (checkRunningState()) {
            return;
        }
        if (poolMode_ == PoolMode::MODE_CACHE) {
            threadSizeThreshold_ = threshold;
        }
    }

    //设置任务队列阈值
    void setTaskQueueThreshold(int threshold) {
        if (checkRunningState()) {
            return;
        }
        taskQueueThreshold_ = threshold;
    }

    void start(int size = std::thread::hardware_concurrency()) {
        isRunning_ = true;
        initThreadSize_ = size;
        currentThreadSize_ = size;

        std::cout << "ThreadPool start init ThreadSize:" << initThreadSize_ << std::endl;
        //std::vector<Thread*> threads;
        for (int i = 0; i < initThreadSize_; i++) {

            auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,
                                                          std::placeholders::_1));

            int id = ptr->getId();
            threads_.emplace(id, std::move(ptr));
        }

        //启动所有线程
        for (const auto &pair: threads_) {
            std::cout << "start Key: " << pair.first << ", Value: " << pair.second->getId() << std::endl;
            threads_[pair.first]->start();
            idleThreadSize_++;//记录空闲线程数量
        }
    }

    // 给线程池提交任务
    // 使用可变参模板编程，让submitTask可以接收任意任务函数和任意数量的参数
    template<typename Func, typename... Args>
    auto submitTask(Func &&func, Args &&... args) -> std::future<decltype(func(args...))> {

        //使用 decltype 获取函数对象 func 被调用时的返回类型;
        using RType = decltype(func(args...));

        //创建了一个 std::packaged_task 对象，模板参数 RType 是函数调用的返回类型。
        auto task = std::make_shared<std::packaged_task<RType()>>(
                std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        std::future<RType> result = task->get_future();

//        std::cout << "submitTask!" << std::endl;
        // 获取锁
        std::unique_lock<std::mutex> lock(queueMutex_);

        // 线程的通信等待任务队列有空余 最长阻塞5s  超时则wait_for返回false
        if (!notFull_cv.wait_for(lock,
                                 std::chrono::seconds(5),
                                 [&]() -> bool { return taskQueue_.size() < taskQueueThreshold_; })) {
            //超时处理逻辑 返回一个0
            std::cout << "submitTask failed,task queue full!" << std::endl;
            auto failTask = std::make_shared<std::packaged_task<RType()>>(
                    []()->RType { return RType(); });
            (*failTask)();
            return failTask->get_future();
        }

        // 当 lambda 函数被调用时，它会通过 (*task)() 执行 task 所指向的 std::packaged_task 对象，从而执行其中包装的函数。
        taskQueue_.emplace([task]() {(*task)();});
        taskSize_++;

        //通知线程有任务了
        notEmpty_cv.notify_all();

        //cache 模式
        if (PoolMode::MODE_CACHE == poolMode_ &&
            currentThreadSize_ < threadSizeThreshold_ &&
            taskSize_ > idleThreadSize_) {

            //新建线程
            auto thread = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,
                                                             std::placeholders::_1));
            int id = thread->getId();
            threads_.emplace(id, std::move(thread));
            std::cout << "submitTask,creat new Thread!" << id << std::endl;
            threads_[id]->start();
            currentThreadSize_++;
            idleThreadSize_++;
        }

        return result;
    }


    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;


private:
    void threadFunc(int threadId) {
        auto lastTime = std::chrono::high_resolution_clock::now();
        for (;;) {
            Task task;
            {
                // 先获取锁
                std::unique_lock<std::mutex> lock(queueMutex_);
                std::cout << threadId << " wait task!" << std::endl;

                while (taskQueue_.empty()) {
                    if (!isRunning_) {
                        std::cout << "taskQueue is empty,recycle thread:" << threadId << std::endl;
                        threads_.erase(threadId);
                        exitCond_.notify_all();
                        return;
                    }

                    if (PoolMode::MODE_CACHE == poolMode_) {

                        // 回收cache模式下 空闲超过60s的线程
                        if (std::cv_status::timeout == notEmpty_cv.wait_for(lock, std::chrono::seconds(1))) {
                            auto now = std::chrono::high_resolution_clock::now();
                            auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                            if (dur.count() >= THREAD_IDLE_THRESHOLD
                                && currentThreadSize_ > initThreadSize_) {
                                //回收线程 把线程对象从线程列表容器中删除
                                threads_.erase(threadId);
                                currentThreadSize_--;
                                idleThreadSize_--;

                                std::cout << "recycle excess thread:" << threadId << std::endl;
                                return;
                            }
                        }
                    } else {
                        // fix 模式
                        //等待notEmpty条件
                        notEmpty_cv.wait(lock);
                    }
                }


                idleThreadSize_--;

                //从任务队列种取一个任务出来
                task = taskQueue_.front();
                taskQueue_.pop();
                taskSize_--;
                std::cout << threadId << " get Task success!" << std::endl;

                //有剩余任务 通知其它线程执行
                if (!taskQueue_.empty()) {
                    notEmpty_cv.notify_all();
                }

                //取出一个任务，进行通知，通知可以继续提交生产任务
                notFull_cv.notify_all();
            } //释放锁

            if (task != nullptr) {
                //当前线程负责执行这个任务
                task();
            }

            idleThreadSize_++;
            lastTime = std::chrono::high_resolution_clock::now();
        }
    }

    bool checkRunningState() const {
        return isRunning_;
    }

private:
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;//线程列表
    int initThreadSize_;                // 初始线程数量
    int threadSizeThreshold_;            // 线程数量阈值
    std::atomic_int currentThreadSize_; // 当前线程数量
    std::atomic_int idleThreadSize_;    // 空闲线程数量

    using Task = std::function<void()>;
    std::queue<Task> taskQueue_; // 任务队列
    std::atomic_uint taskSize_;                   // 任务数量
    int taskQueueThreshold_;                      // 任务队列数量阈值

    std::mutex queueMutex_;
    std::condition_variable notFull_cv;// 等待任务队列有空闲空间
    std::condition_variable notEmpty_cv;// 等待任务队列有任务
    std::condition_variable exitCond_;

    PoolMode poolMode_;      // 当前工作模式
    std::atomic_bool isRunning_;
};

#endif
