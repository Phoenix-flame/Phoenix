#pragma once
#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Phoenix{

    // A fixed pool of threads for per-element passes (fluid particles, fire cells). Each
    // index is written by exactly one task and reads only the previous pass, so the
    // thread count never changes a result. Called from one thread at a time.
    class Workers{
    public:
        static Workers& Get() { static Workers pool; return pool; }

        // Runs body(begin, end) over [0, count) in up to one chunk per core, each at least
        // `grain` elements (fewer, bigger chunks for cheap elements).
        void For(int count, const std::function<void(int, int)>& body, int grain = 256){
            const int chunks = std::min((int)m_Threads.size() + 1, std::max(1, count / std::max(grain, 1)));
            if (chunks <= 1) { body(0, count); return; }
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_Body = &body;
                m_Count = count;
                m_Chunks = chunks;
                m_Next = 1;   // chunk 0 runs on the calling thread
                m_Done = 0;
                m_Generation++;
            }
            m_Wake.notify_all();
            RunChunk(0);
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Finished.wait(lock, [&]{ return m_Done == m_Chunks - 1; });
            m_Body = nullptr;
        }

    private:
        Workers(){
            int n = std::max(1u, std::thread::hardware_concurrency()) - 1;
            for (int t = 0; t < n; t++) { m_Threads.emplace_back([this]{ Work(); }); }
        }
        ~Workers(){
            { std::unique_lock<std::mutex> lock(m_Mutex); m_Quit = true; }
            m_Wake.notify_all();
            for (auto& t : m_Threads) { t.join(); }
        }
        void RunChunk(int chunk){
            int begin = (int)((long long)m_Count * chunk / m_Chunks), end = (int)((long long)m_Count * (chunk + 1) / m_Chunks);
            (*m_Body)(begin, end);
        }
        void Work(){
            long long seen = 0;
            std::unique_lock<std::mutex> lock(m_Mutex);
            while (true){
                m_Wake.wait(lock, [&]{ return m_Quit || (m_Generation != seen && m_Next < m_Chunks); });
                if (m_Quit) { return; }
                while (m_Next < m_Chunks){
                    int chunk = m_Next++;
                    lock.unlock();
                    RunChunk(chunk);
                    lock.lock();
                    if (++m_Done == m_Chunks - 1) { m_Finished.notify_one(); }
                }
                seen = m_Generation;
            }
        }

        std::vector<std::thread> m_Threads;
        std::mutex m_Mutex;
        std::condition_variable m_Wake, m_Finished;
        const std::function<void(int, int)>* m_Body = nullptr;
        int m_Count = 0, m_Chunks = 0, m_Next = 0, m_Done = 0;
        long long m_Generation = 0;
        bool m_Quit = false;
    };
}
