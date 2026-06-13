// POST /api/upload_image?car=C1&node=N5&ts=20260613143022&seq=2
// Content-Type: application/octet-stream
// Body: raw JPEG binary (one image per request, 5 requests per capture session)
// Saves to: /var/agv/captures/<car>/<ts>_<node>_<seq>.jpg

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <fcgi_stdio.h>
#include "fcgi_utils.h"
using namespace agv::http;

const char* proc_name = "upload_api";

static constexpr size_t kMaxImageSize = 512 * 1024;   // 512 KB per image
static constexpr char   kSaveDir[]    = "/var/agv/captures";

// Extract value for key from a query string ("car=C1&node=N5&...")
static std::string qs_get(const char* qs, const char* key) {
    if (!qs || !key) return {};
    std::string needle = std::string(key) + "=";
    const char* p = strstr(qs, needle.c_str());
    if (!p) return {};
    p += needle.size();
    const char* end = strchr(p, '&');
    return end ? std::string(p, end) : std::string(p);
}

// mkdir -p equivalent; returns true on success or if dir already exists
static bool mkdir_p(const char* path) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

// Read raw binary body from FCGI stdin; returns empty vector on error
static std::vector<uint8_t> read_binary(size_t max_len) {
    const char* len_str = getenv("CONTENT_LENGTH");
    if (!len_str) return {};
    size_t n = static_cast<size_t>(atoi(len_str));
    if (n == 0 || n > max_len) return {};
    std::vector<uint8_t> buf(n);
    size_t got = FCGI_fread(buf.data(), 1, n, FCGI_stdin);
    buf.resize(got);
    return buf;
}

int main() {
    while (FCGI_Accept() >= 0) {
        if (handle_preflight()) continue;

        const char* method = getenv("REQUEST_METHOD");
        if (!method || strcmp(method, "POST") != 0) {
            reply_err(405, "只支持 POST 方法");
            continue;
        }

        const char* qs       = getenv("QUERY_STRING");
        std::string car_str  = qs_get(qs, "car");
        std::string node_str = qs_get(qs, "node");
        std::string ts_str   = qs_get(qs, "ts");
        std::string seq_str  = qs_get(qs, "seq");

        if (car_str.empty() || node_str.empty() || ts_str.empty() || seq_str.empty()) {
            reply_err(400, "缺少必要参数 car/node/ts/seq");
            continue;
        }

        int seq = atoi(seq_str.c_str());
        if (seq < 1 || seq > 5) {
            reply_err(400, "seq 必须为 1-5");
            continue;
        }

        auto buf = read_binary(kMaxImageSize);
        if (buf.empty()) {
            reply_err(400, "图片数据为空或超过大小限制");
            continue;
        }

        // Build directory and file path
        char dir_path[256];
        snprintf(dir_path, sizeof(dir_path), "%s/%s", kSaveDir, car_str.c_str());
        if (!mkdir_p(dir_path)) {
            reply_err(500, "无法创建存储目录");
            continue;
        }

        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/%s_%s_%d.jpg",
                 dir_path, ts_str.c_str(), node_str.c_str(), seq);

        // Use POSIX I/O to avoid fcgi_stdio.h macro conflicts
        int fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            dprintf(2, "[upload_api] open failed: %s\n", strerror(errno));
            reply_err(500, "保存图片失败");
            continue;
        }
        write(fd, buf.data(), buf.size());
        close(fd);

        dprintf(2, "[upload_api] saved %s (%zu B)\n", file_path, buf.size());

        char data[384];
        snprintf(data, sizeof(data),
                 "{\"car\":\"%s\",\"node\":\"%s\",\"seq\":%d,\"size\":%zu,\"path\":\"%s\"}",
                 car_str.c_str(), node_str.c_str(), seq, buf.size(), file_path);
        reply_ok("图片已保存", data);
    }
    return 0;
}
