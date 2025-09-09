#include <algorithm>
#include <cstdint>
#include <vector>
#include <cassert>
#include <list>
#include <mutex>
#include <condition_variable>
#include <array>
#include <atomic>
#include <thread>


template <typename T>
class DoubleBufferMutex {
    std::vector<T>buffer0_;
    std::vector<T>buffer1_;
    T* front_;
    T* back_;

    std::mutex mut_;
    std::condition_variable cv_;
    uint64_t seq_num_{};
    public:

    DoubleBufferMutex(std::size_t N):buffer0_(N), buffer1_(N),
    front_{buffer0_.data()},
    back_{buffer1_.data()} {

    }


    void write(T* data) {
        std::copy(data, data + buffer0_.size(), back_);
        {
            std::lock_guard<std::mutex>lk(this->mut_);
            
            std::swap(back_, front_);
            ++seq_num_;
        }
        this->cv_.notify_one();
    }

    uint64_t read(uint64_t local, const T* ptr) {

        {
            std::unique_lock<std::mutex>lk(this->mut_);

            cv_.wait(lk, [&](){ return local != seq_num_;});
            ptr = front_;
        }

        return seq_num_;
    }
};


int main() {

    constexpr std::size_t N = 1000;
    DoubleBufferMutex<int> buf(N);

    std::atomic<bool>run{true};
std::jthread writer([&]{ std::vector<int>tmp(N); int base = 0; while (run.load(std::memory_order_relaxed)) { for (std::size_t i = 0; i < N; ++i) { tmp[i] = base + i; } buf.write(tmp.data()); base += 10; std::this_thread::sleep_for(std::chrono::milliseconds(20)); } });


    std::jthread reader([&](){
        uint64_t key = 0;
        const int* ptr = nullptr;
        for (int i = 0; i < N; ++i) {
            key = buf.read(key, ptr);

            // do some operations with the read numbers;
        }
        run.store(false);
    });

    reader.join();
    writer.join();

    return 0;
}
