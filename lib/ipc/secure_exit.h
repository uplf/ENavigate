#pragma once
 //注明：本部分的规划与编写均是ai完成，本人只是在其中调试&学习
/**
 * graceful_exit.h — 统一退出序列
 *
 * 所有进程在跳出主循环后调用 GracefulExit::run()，
 * 按固定顺序清理资源，避免写 SHM 的进程直接退出造成数据损坏。
 *
 * 退出序列（参考开发文档）：
 *   1. 停止接收新任务（关闭 MQ 读端）
 *   2. 等待当前任务完成或超时放弃
 *   3. 执行用户注册的清理回调（munmap SHM、mq_close 等）
 *   4. 通知 systemd：STOPPING=1
 *   5. exit(0)
 *
 * 用法：
 *
 *   agv::GracefulExit exit_seq("planner");
 *
 *   // 注册清理动作（LIFO 顺序执行，类似 atexit）
 *   exit_seq.add_cleanup("close mq", [&]{ mq_close(mq); });
 *   exit_seq.add_cleanup("detach shm", [&]{ munmap(shm_ptr, shm_size); });
 *
 *   // 主循环结束后
 *   exit_seq.run();  // 按注册逆序执行所有清理，再 sd_notify
 */
 
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>
 
// sd_notify 是可选的，若系统没有 libsystemd 可定义此宏跳过
#ifndef AGV_NO_SYSTEMD
#  include <systemd/sd-daemon.h>
#else
inline int sd_notify(int /*unset_environment*/, const char* /*state*/) { return 0; }
#endif
 
namespace agv {
 
class GracefulExit {
public:
    struct CleanupEntry {
        std::string            name;
        std::function<void()>  fn;
    };
 
    explicit GracefulExit(const char* proc_name) : proc_name_(proc_name) {}
 
    /**
     * 注册一个清理动作，名称仅用于日志。
     * 执行顺序为注册的逆序（后注册先执行，类似析构顺序）。
     */
    void add_cleanup(std::string name, std::function<void()> fn) {
        entries_.push_back({std::move(name), std::move(fn)});
    }
 
    /**
     * 执行完整退出序列，调用后进程结束。
     * @param wait_ms  等待当前任务完成的最长毫秒数（0 = 不等待）
     */
    [[ noreturn ]] void run(int wait_ms = 200) {
        log_info("shutdown initiated, starting exit sequence");
 
        // Step 1-2: 等待当前任务（由调用方在进入 run() 前负责停止新任务接收）
        if (wait_ms > 0) {
            log_info("waiting up to " + std::to_string(wait_ms) + "ms for in-flight work");
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
        }
 
        // Step 3: 逆序执行清理回调
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            log_info("cleanup: " + it->name);
            try {
                it->fn();
            } catch (const std::exception& e) {
                log_err("cleanup '" + it->name + "' threw: " + e.what());
            } catch (...) {
                log_err("cleanup '" + it->name + "' threw unknown exception");
            }
        }
 
        // Step 4: 告知 systemd 进程正在停止
        sd_notify(0, "STOPPING=1");
        log_info("exit(0)");
 
        // Step 5
        ::exit(0);
    }
 
private:
    void log_info(const std::string& msg) {
        // 直接写 stderr，不依赖日志模块（日志模块本身可能已关闭）
        fprintf(stderr, "[%s] INFO  exit_seq: %s\n", proc_name_, msg.c_str());
    }
    void log_err(const std::string& msg) {
        fprintf(stderr, "[%s] ERROR exit_seq: %s\n", proc_name_, msg.c_str());
    }
 
    const char*              proc_name_;
    std::vector<CleanupEntry> entries_;
};
 
} // namespace agv
 