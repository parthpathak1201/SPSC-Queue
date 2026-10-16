#include <iostream>
#include <algorithm>
#include <functional>
#include <chrono>
#include <memory>
#include <new>
#include <thread>

//evaluates to 256 bytes on my machine (weird, i know)
constexpr auto CacheLineSize = std::hardware_destructive_interference_size;


namespace {
    /**
 * @tparam T datatype of the SPSC Queue
 * @tparam Capacity Capacity of the Queue - to be known at compile time
 */
    template<typename T, size_t Capacity>
    class alignas(CacheLineSize) SPSCQueue {
        static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

        /**
     * The contiguous store for the SPSC Queue. Favors cache locality.
     * Uses std::array<T> because it is simpler than a std::vector<T>, no hidden overhead
     */
        std::array<T, Capacity> store{};

        /**
     * Head of the queue - this is where the pop/read operations are performed
     * Aligned as CacheLineSize to avoid thrashing
     */
        alignas(CacheLineSize) std::atomic<size_t> head_{0}; //consumer reads

        /**
     * Tail of the queue - this is where the push/write operations are performed
     * Aligned as CacheLineSize to avoid thrashing
     */
        alignas(CacheLineSize) std::atomic<size_t> tail_{0}; //producer writes


        static constexpr size_t Mask = Capacity - 1;

    public:
        bool push(const T &item) {
            const auto head = head_.load(std::memory_order_acquire);
            const auto tail = tail_.load(std::memory_order_relaxed);

            // CHANGED: Using bitwise AND instead of modulo
            if (((tail + 1) & Mask) == (head & Mask)) {
                return false;
            }
            store[tail & Mask] = item;

            // CHANGED: Using bitwise AND instead of modulo
            tail_.store((tail + 1) & Mask, std::memory_order_release);
            return true;
        }

        bool pop(T &item) {
            const auto head = head_.load(std::memory_order_relaxed);
            const auto tail = tail_.load(std::memory_order_acquire);

            if (tail == head) {
                return false;
            }

            item = store[head & Mask];

            // CHANGED: Using bitwise AND instead of modulo
            head_.store((head + 1) & Mask, std::memory_order_release);
            return true;
        }

        [[nodiscard]] size_t size() const {
            const auto head = head_.load(std::memory_order_acquire);
            const auto tail = tail_.load(std::memory_order_relaxed);
            return (tail >= head) ? (tail - head) : (Capacity - (head - tail));
        }

        [[nodiscard]] bool empty() const {
            return size() == 0;
        }

        [[nodiscard]] bool full() const {
            return size() == Capacity - 1;
        }
    };
}

static auto benchmark() {

    constexpr size_t QueueCapacity = 1024 * 1024;

    // Allocate the queue on the heap to avoid stack overflow
    auto queue_ptr = std::make_unique<SPSCQueue<uint64_t, QueueCapacity> >();
    auto &queue = *queue_ptr;

    constexpr uint64_t TotalItems = 10'000'000;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::thread producer([&queue]() {
        for (uint64_t i = 0; i < TotalItems; ++i) {
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&queue]() {
        uint64_t val = 0;
        uint64_t popped_count = 0;
        while (popped_count < TotalItems) {
            if (queue.pop(val)) {
                ++popped_count;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();

    std::cout << "Average latency: " << static_cast<double>(elapsed_ns) / TotalItems << " ns/op\n";

}


int main() {
    benchmark();
    return 0;
}
