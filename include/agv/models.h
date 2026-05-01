#pragma once

#include "../config/config.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace models{
    class car{
        public:
        uint8_t id;
    };
    class union_id{
        public:
        uint8_t uid;
        static uint8_t s_id;

    };
    class node:public union_id{
        public:
        uint8_t id;
    };
    class edge:public  union_id{
        public:
        uint8_t id;
    };
}