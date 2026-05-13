/**
 * status.cpp — GET /api/status
 *
 * 读取 SHM 中的 MapData 和 CarData，序列化为 API 文档定义的 JSON 格式。
 *
 * 编译：
 *   g++ -std=c++17 \
 *       -I../../ -I../../lib \
 *       -o status status.cpp \
 *       -lfcgi -lrt -lpthread
 *
 * 启动（spawn-fcgi 绑定到 Unix socket）：
 *   spawn-fcgi -s /run/agv/status.sock -n ./status
 */



#include <ctime>
#include <cstdio>
#include <cstring>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include "shm_manager.h"
#include <fcgi_stdio.h>
#include "fcgi_utils.h"
using namespace agv::http;

// ── JSON 构建辅助 ─────────────────────────────────────────────────────────────

static void append(char* buf, size_t cap, size_t& pos, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + pos, cap - pos, fmt, ap);
    va_end(ap);
    if (n > 0) pos += static_cast<size_t>(n);
}

// ── 构建完整 status JSON ──────────────────────────────────────────────────────

static void build_status_json(const agv::MapData& map,
                               const agv::CarData& cars,
                               const agv::bipathData& bipaths,
                               char* out, size_t cap) {
    size_t pos = 0;

    // 统计在线小车数 和 有任务小车数
    int online = 0, tasks = 0;
    for (uint16_t i = 0; i < cars.car_count_; ++i) {
        auto s = static_cast<uint8_t>(cars.cars_[i].status);
        if (s != 3 /*OFFLINE*/) ++online;
        if (cars.cars_[i].current_task_id != 0) ++tasks;
    }

    std::string ts = now_str();

    append(out, cap, pos,
        "{"
        "\"timestamp\":\"%s\","
        "\"system_status\":\"正常运行\","
        "\"online_cars\":%d,"
        "\"task_count\":%d,",
        ts.c_str(), online, tasks);

    // ── broadcast ──────────────────────────────────────────────────
    append(out, cap, pos, "\"broadcast\":\"[%s] 系统运行中", ts.c_str() + 11);
    for (uint16_t i = 0; i < cars.car_count_; ++i) {
        const auto& c = cars.cars_[i];
        append(out, cap, pos, " · C%u@N%u(%s)",
               c.id, c.current_node_id,
               car_status_str(static_cast<uint8_t>(c.status)));
    }
    append(out, cap, pos, "\",");

    // ── alerts（边故障 / 封禁告警）────────────────────────────────
    append(out, cap, pos, "\"alerts\":[");
    bool first_alert = true;
    for (uint16_t i = 0; i < map.edge_count_; ++i) {
        const auto& e = map.edges_[i];
        auto s = static_cast<uint8_t>(e.status);
        if (s == 0 || s == 1) continue;  // idle / occupied 不告警
        const char* level = (s == 2) ? "warn" : "err";
        const char* desc  = edge_status_str(s);
        if (!first_alert) append(out, cap, pos, ",");
        append(out, cap, pos,
               "{\"level\":\"%s\",\"msg\":\"L%u %s\",\"time\":\"%s\"}",
               level, e.id, desc, ts.c_str() + 11);
        first_alert = false;
    }
    append(out, cap, pos, "],");

    // ── cars ──────────────────────────────────────────────────────
    append(out, cap, pos, "\"cars\":{");
    for (uint16_t i = 0; i < cars.car_count_; ++i) {
        const auto& c = cars.cars_[i];
        if (i > 0) append(out, cap, pos, ",");

        // path_stack → JSON array
        append(out, cap, pos,
               "\"C%u\":{"
               "\"id\":\"C%u\","
               "\"name\":\"小车%u\","
               "\"status\":\"%s\","
               "\"pos\":\"N%u\","
               "\"path\":[",
               c.id, c.id, c.id,
               car_status_str(static_cast<uint8_t>(c.status)),
               c.current_node_id);
        for (uint8_t p = 0; p < c.path_len; ++p) {
            if (p > 0) append(out, cap, pos, ",");
            append(out, cap, pos, "\"N%u\"", c.path_stack[p]);
        }
        append(out, cap, pos, "],");

        // task
        if (c.current_task_id != 0) {
            append(out, cap, pos,
                   "\"task\":{\"id\":\"T%03u\","
                   "\"from\":\"N%u\",\"to\":\"N%u\",\"priority\":1}",
                   c.current_task_id,
                   c.current_node_id, c.target_node_id);
        } else {
            append(out, cap, pos, "\"task\":null");
        }
        append(out, cap, pos, "}");
    }
    append(out, cap, pos, "},");

    // ── nodes ─────────────────────────────────────────────────────
    append(out, cap, pos, "\"nodes\":{");
    for (uint16_t i = 0; i < map.node_count_; ++i) {
        const auto& n = map.nodes_[i];
        if (i > 0) append(out, cap, pos, ",");
        append(out, cap, pos,
               "\"N%u\":{\"status\":\"%s\",\"name\":\"%s\"}",
               n.id,
               node_status_str(static_cast<uint8_t>(n.status)),
               n.name[0] ? n.name : "节点");
    }
    append(out, cap, pos, "},");

    // ── edges ─────────────────────────────────────────────────────
    append(out, cap, pos, "\"edges\":{");
    for (uint16_t i = 0; i < map.edge_count_; ++i) {
        const auto& e = map.edges_[i];
        if(e.id>bipaths.bipath_count_)continue;//双向边只显示一次，避免重复显示A-B和B-A两条边
        if (i > 0) append(out, cap, pos, ",");
        append(out, cap, pos,
               "\"L%d\":{\"status\":\"%s\",\"weight\":\"%d\",\"from\":\"N%u\",\"to\":\"N%u\"}",
               e.id,
               edge_status_str(static_cast<uint8_t>(e.status)),
               e.weight,
               e.from_node, e.to_node);
    }
    append(out, cap, pos, "},");

    // ── logs（占位，后续接入日志模块）────────────────────────────
    append(out, cap, pos,
           "\"logs\":[],"
           "\"meta\":{"
           "\"schema_version\":\"1.0\","
           "\"host\":\"agv-dispatcher\","
           "\"refresh_interval_ms\":2000"
           "}"
           "}");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    // attach SHM（进程启动时一次，之后复用）
    agv::ShmClient shm_client;
    bool shm_ok = false;
    try {
        shm_client.attach(1000);  // 等最多 1s
        shm_ok = true;
    } catch (const std::exception& e) {
        dprintf(2, "[status] SHM not available: %s\n", e.what());
    }

    static char json_buf[32768];

    while (FCGI_Accept() >= 0) {
        if (handle_preflight()) continue;

        if (!shm_ok) {
            // SHM 不可用时返回静态占位数据（调试用）
            reply_json(200,
                "{\"timestamp\":\"--\",\"system_status\":\"SHM未就绪\","
                "\"online_cars\":0,\"task_count\":0,"
                "\"broadcast\":\"等待 model_manager 启动\","
                "\"alerts\":[],\"cars\":{},\"nodes\":{},\"edges\":{},"
                "\"logs\":[],\"meta\":{\"schema_version\":\"1.0\","
                "\"refresh_interval_ms\":2000}}");
            continue;
        }

        // 读 SHM 快照（Seqlock 保护）
        agv::MapData map_snap = agv::shm_read_map(shm_client.ptr());
        agv::CarData car_snap = agv::shm_read_cars(shm_client.ptr());
        agv::bipathData bi_snap = agv::shm_read_bipaths(shm_client.ptr());

        build_status_json(map_snap, car_snap,bi_snap, json_buf, sizeof(json_buf));

        reply_json(200, json_buf);
    }
    return 0;
}
