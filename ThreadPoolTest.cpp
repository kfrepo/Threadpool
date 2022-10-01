#include <iostream>
#include <thread>
#include <future>
#include <chrono>
#include <random>
#include "threadpool.h"

using namespace std;

int sum1(int a, int b)
{
    // 模拟耗时
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(5, 10);
    int wait_seconds = dis(gen);
    std::this_thread::sleep_for(std::chrono::seconds (wait_seconds));
    cout << "sum1 wait_seconds:" << wait_seconds << endl;

    return a + b;
}

int sum2(int a, int b, int c)
{
    // 模拟耗时
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(5, 10);
    int wait_seconds = dis(gen);
    std::this_thread::sleep_for(std::chrono::seconds (wait_seconds));
    cout << "sum2 wait_seconds:" << wait_seconds << endl;

    return a + b + c;
}

int main()
{
    ThreadPool pool;
    pool.setMode(PoolMode::MODE_CACHE);
    pool.start(4);

    future<int> r1 = pool.submitTask(sum1, 1, 2);
    future<int> r2 = pool.submitTask(sum2, 1, 2, 3);

    future<int> r3 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
            sum += i;
        return sum;
    }, 1, 100);

    for (int n = 0; n < 20; n++) {

        pool.submitTask(sum1, 1, 10);
    }

    cout << "r1:" << r1.get() << endl;
    cout << "r2:" << r2.get() << endl;
    cout << "r3:" << r3.get() << endl;

}