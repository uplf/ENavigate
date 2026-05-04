#include <fcgi_stdio.h>
#include <iostream>

int main() {
    // 循环接收请求
    while (FCGI_Accept() >= 0) {
        // 输出 HTTP 头部
        printf("Content-type: text/html\r\n\r\n");
        
        // 输出内容
        printf("<html>\n");
        printf("<head><title>FastCGI Hello World</title></head>\n");
        printf("<body>\n");
        printf("<h1>Hello from C++ FastCGI!</h1>\n");
        printf("<p>Request handled successfully.</p>\n");
        printf("</body>\n");
        printf("</html>\n");
    }
    return 0;
}