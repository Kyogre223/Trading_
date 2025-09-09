// doublebuf_bench.cpp
#include <benchmark/benchmark.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

using clock_t = std::chrono::steady_clock;
using ns      = std::chrono::nanoseconds;
using us      = std::chrono::microseconds;

template <typename T>
class DoubleBufferMutex {
    std::vector<T> buffer0_;
    std::vector<T> buffer1_;
    T* front_;
    T* back_;

    std::mutex mut_;
    std::condition_variable cv_;
    uint64_t seq_num_{0};

    // instrumentation
    std::atomic<uint64_t> writer_lock_wait_ns_{0};
    std::atomic<uint64_t> reader_wait_ns_{0};
    std::atomic<uint64_t> swaps_{0};

public:
    explicit DoubleBufferMutex(std::size_t N)
        : buffer0_(N), buffer1_(N),
          front_{buffer0_.data()}, back_{buffer1_.data()} {}

    std::size_t size() const noexcept { return buffer0_.size(); }

    // Writer: copy then swap (instrument lock acquisition wait)
    void write(const T* data) {
        std::copy(data, data + size(), back_);

        auto t0 = clock_t::now();
        {
            std::unique_lock<std::mutex> lk(mut_);
            // time spent competing for the lock is approximated by the elapsed from t0
            auto t1 = clock_t::now();
            writer_lock_wait_ns_.fetch_add(
                static_cast<uint64_t>(std::chrono::duration_cast<ns>(t1 - t0).count()),
                std::memory_order_relaxed);

            std::swap(back_, front_);
            ++seq_num_;
            ++swaps_;
        }
        cv_.notify_one();
    }

    // Reader: wait for next seq; return new seq and snapshot pointer (by ref)
    uint64_t read(uint64_t local_seq, const T*& out_ptr) {
        std::unique_lock<std::mutex> lk(mut_);
        auto t0 = clock_t::now();
        cv_.wait(lk, [&] { return local_seq != seq_num_; });
        auto t1 = clock_t::now();
        reader_wait_ns_.fetch_add(
            static_cast<uint64_t>(std::chrono::duration_cast<ns>(t1 - t0).count()),
            std::memory_order_relaxed);

        out_ptr = front_;
        return seq_num_;
    }

    // Stats accessors
    void reset_stats() {
        writer_lock_wait_ns_.store(0, std::memory_order_relaxed);
        reader_wait_ns_.store(0, std::memory_order_relaxed);
        swaps_.store(0, std::memory_order_relaxed);
    }
    uint64_t writer_wait_ns() const { return writer_lock_wait_ns_.load(std::memory_order_relaxed); }
    uint64_t reader_wait_ns() const { return reader_wait_ns_.load(std::memory_order_relaxed); }
    uint64_t swaps()        const { return swaps_.load(std::memory_order_relaxed); }
};

// Runs a single producer/consumer session and returns stats
struct RunStats {
    uint64_t swaps = 0;
    uint64_t reader_wait_ns = 0;
    uint64_t writer_wait_ns = 0;
    double   wall_seconds = 0.0;
};

template <typename T>
RunStats run_session(std::size_t N,
                     uint64_t total_frames,
                     int writer_sleep_us /* e.g., 20000 for 20ms */) {
    DoubleBufferMutex<T> buf(N);
    buf.reset_stats();

    std::atomic<bool> run{true};
    std::atomic<uint64_t> frames_written{0};

    auto t_start = clock_t::now();

    std::thread writer([&] {
        std::vector<T> tmp(N);
        T base = 0;
        while (run.load(std::memory_order_relaxed)) {
            for (std::size_t i = 0; i < N; ++i) tmp[i] = base + static_cast<T>(i);
            buf.write(tmp.data());
            base += static_cast<T>(10);
            frames_written.fetch_add(1, std::memory_order_relaxed);
            if (writer_sleep_us > 0) std::this_thread::sleep_for(us(writer_sleep_us));
            if (frames_written.load(std::memory_order_relaxed) >= total_frames) {
                run.store(false, std::memory_order_relaxed);
            }
        }
    });

    std::thread reader([&] {
        uint64_t key = 0;
        const T* ptr = nullptr;
        uint64_t frames_read = 0;
        while (frames_read < total_frames) {
            key = buf.read(key, ptr);
            // simulate using ptr quickly; don't retain across iterations
            // (double-buffer contract)
            ++frames_read;
        }
        run.store(false, std::memory_order_relaxed);
    });

    writer.join();
    reader.join();

    auto t_end = clock_t::now();

    RunStats rs;
    rs.swaps = buf.swaps();
    rs.reader_wait_ns = buf.reader_wait_ns();
    rs.writer_wait_ns = buf.writer_wait_ns();
    rs.wall_seconds = std::chrono::duration<double>(t_end - t_start).count();
    return rs;
}

// Google Benchmark harness
// Args: { total_frames, writer_sleep_us }
static void BM_DoubleBuffer_SPSC(benchmark::State& state) {
    const uint64_t total_frames   = static_cast<uint64_t>(state.range(0));
    const int      writer_sleep_us= static_cast<int>(state.range(1));
    const std::size_t N = 1000; // elements per frame

    for (auto _ : state) {
        auto t0 = clock_t::now();
        RunStats rs = run_session<int>(N, total_frames, writer_sleep_us);
        auto t1 = clock_t::now();

        // Use manual timing to account for wall time across threads
        state.SetIterationTime(std::chrono::duration<double>(t1 - t0).count());

        // Report counters
        state.counters["frames"] = static_cast<double>(rs.swaps);
        state.counters["reader_wait_ns_total"] = static_cast<double>(rs.reader_wait_ns);
        state.counters["writer_wait_ns_total"] = static_cast<double>(rs.writer_wait_ns);
        state.counters["reader_wait_ns_per_frame"] =
            rs.swaps ? static_cast<double>(rs.reader_wait_ns) / rs.swaps : 0.0;
        state.counters["writer_wait_ns_per_frame"] =
            rs.swaps ? static_cast<double>(rs.writer_wait_ns) / rs.swaps : 0.0;
        state.counters["throughput_frames_per_s"] =
            rs.wall_seconds > 0.0 ? static_cast<double>(rs.swaps) / rs.wall_seconds : 0.0;
    }
}
BENCHMARK(BM_DoubleBuffer_SPSC)
    // {frames, writer_sleep_us}
    ->Args({500, 20000})     // ~20 ms producer delay (like your demo)
    ->Args({5000, 0})        // no producer delay (max throughput)
    ->Args({5000, 1000})     // light producer delay
    ->UseManualTime();

BENCHMARK_MAIN();
