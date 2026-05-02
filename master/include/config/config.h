/*基础规则
前面加_的可以注释掉，前面不加_的不可以
*/


//****调试区 */
//是否打印注释，若不直接打印，请注释
#define _AGV_PRINT_DEBUG

//****常量区-边界常量 */
//最大车数-已经按照该定义作对齐
#define AGV_MAX_CARS 2
//最大的节点数-已经按照该定义作对齐
#define AGV_MAX_NODES 32
//最大路径数-已经按照该定义作对齐
#define AGV_MAX_EDGES 128
//最大的规划所包含的路径长度-已经按照该定义作对齐
#define AGV_MAX_PATHLEN 100
//节点引出最大边数-已经按照该定义作对齐
#define AGV_MAX_NEIGHBORS 8

//节点名称最大长度-已经按照该定义作对齐
#define AGC_MAX_NAME 16
//节点标签最大长度-已经按照该定义作对齐
#define AGC_MAX_LABEL 5



//#define DO_NOT_USE _Static_assert(0, "This macro is not allowed")