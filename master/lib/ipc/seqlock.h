#pragma once

/**
 * seqlock.h — Seqlock（顺序锁）实现
 *
 * 适用场景：读多写少、数据可重读（写者偶尔写，读者不介意重试）。
 * 你的 MapData / CarData 都满足这个特征。
 *
 * 原理：
 *   - 写者：先将 seq 加 1（变为奇数，表示"写中"），
 *           写完数据后再加 1（变为偶数，表示"写完"）。
 *   - 读者：记录读前 seq（必须为偶数才开始读），
 *           读完后再次检查 seq，若与读前相同则读取有效，
 *           否则说明期间有写者介入，重试。
 *
 * 局限：
 *   - 不适合含指针的数据（跨进程指针无效）。
 *   - 写者频繁时读者会持续重试，但 AGV 场景写操作稀少，不是问题。
 *   - 单写者。若将来需要多写者，升级为带 mutex 的写侧即可。
 *
 * SHM 使用注意：
 *   Seqlock 本身是 POD，sizeof 固定，可以直接嵌入 SHM 结构体。
 *   atomic 成员在 SHM 中要求所有进程用相同的内存模型，
 *   Linux x86/ARM 上均满足。
 */

#include <atomic>
#include <cstdint>

namespace agv {

class Seqlock {
public:
    Seqlock() = default;

    // ── 写者接口 ─────────────────────────────────────────────────────────────

    /// 写前调用：seq 奇数化，内存屏障防止写操作被提前
    void write_begin() {
        uint32_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_release);
    }

    void write_end() {
        uint32_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_release);
    }

    // ── 读者接口 
    uint32_t read_begin() const {
        uint32_t s;
        do {
            s = seq_.load(std::memory_order_acquire);
        } while (s & 1u);
        return s;
    }

    /// 读后调用：返回 true 表示读取期间无写者介入，数据有效
    bool read_end(uint32_t snapshot) const {
        std::atomic_thread_fence(std::memory_order_acquire);
        return seq_.load(std::memory_order_relaxed) == snapshot;
    }

private:
    // 对齐到 cacheline，避免 false sharing（SHM 中多个 Seqlock 相邻时）
    alignas(64) std::atomic<uint32_t> seq_ {0};
    // 填充到 64 字节，防止相邻字段与 seq_ 共享 cacheline
    char _pad[60];
};

static_assert(sizeof(Seqlock) == 64, "Seqlock must be exactly one cacheline");

// ── RAII 写者守卫 ─────────────────────────────────────────────────────────────

/**
 * 用法：
 *   {
 *       SeqlockWriteGuard g(shm->map_lock);
 *       shm->map.edges_[i].status = EdgeStatus::BLOCKED;
 *   }  // 析构时自动 write_end
 */
class SeqlockWriteGuard {
public:
    explicit SeqlockWriteGuard(Seqlock& lock) : lock_(lock) {
        lock_.write_begin();
    }
    ~SeqlockWriteGuard() {
        lock_.write_end();
    }
    SeqlockWriteGuard(const SeqlockWriteGuard&)            = delete;
    SeqlockWriteGuard& operator=(const SeqlockWriteGuard&) = delete;

private:
    Seqlock& lock_;
};

// ── 读者重试辅助宏 ────────────────────────────────────────────────────────────

/**
 * 用法：
 *   MapData snap;
 *   SEQLOCK_READ(shm->map_lock, {
 *       snap = shm->map;    // 读取整块数据的快照
 *   });
 *   // 之后使用 snap，不持有任何锁
 */
#define SEQLOCK_READ(lock, body)            \
    do {                                    \
        uint32_t _seq;                      \
        do {                                \
            _seq = (lock).read_begin();     \
            { body }                        \
        } while (!(lock).read_end(_seq));   \
    } while (0)

} // namespace agv