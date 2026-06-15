# ================= 导入必要的库 Import necessary libraries =================
import os
import sys
import gc
import time
import utime
import ujson
import image
import random
import ulab.numpy as np
import nncase_runtime as nn
import aidemo

# 网络及SSL支持
import network
import socket
import ssl

# 导入K230与媒体相关模块
from media.sensor import *
from media.display import *
from media.media import *

# 导入AI处理及工具类
from libs.PipeLine import PipeLine, ScopedTiming
from libs.AIBase import AIBase
from libs.AI2D import Ai2d
from libs.Utils import *
from libs.PlatTasks import DetectionApp  # 引入官方的检测类，替代自定义类

# 导入串口资源与协议
from libs.YbProtocol import YbProtocol
from ybUtils.YbUart import YbUart

# ================= 网络及业务配置参数区 =================
MASTER_HOST = "---.com"
MASTER_PORT = 443
WIFI_SSID = "---"
WIFI_PASS = "---"

'''WIFI_SSID = "---"
WIFI_PASS = "---"'''

SAVE_PATH = "/data/photo/"

# ======= 业务状态机常量 =======
NORMAL_MODE = 0
CAPTURE_MODE = 1
K230_WAIT_NAME_STR = 1
K230_WAIT_CAPTURE = 2

# ================= 硬件及算法参数区 =================
# 1. 硬件外设初始化
uart = YbUart(baudrate=115200)
pto = YbProtocol()

# 2. 图像分辨率配置
PICTURE_WIDTH = 320
PICTURE_HEIGHT = 240
FRAME_CENTER_X = PICTURE_WIDTH // 2
DISPLAY_WIDTH = 640
DISPLAY_HEIGHT = 480
SCALE_X = DISPLAY_WIDTH / PICTURE_WIDTH
SCALE_Y = DISPLAY_HEIGHT / PICTURE_HEIGHT

# 3. 循迹算法配置参数
TRACKING_ROI = [40, 190, 240, 50] # 循迹 ROI
TRACKING_ROI_SCALED = (
    int(TRACKING_ROI[0] * SCALE_X),
    int(TRACKING_ROI[1] * SCALE_Y),
    int(TRACKING_ROI[2] * SCALE_X),
    int(TRACKING_ROI[3] * SCALE_Y),
)

BLACK_THRESHOLD = [(0, 35, -128, 127, -128, 127)]
NORMAL_LINE_WIDTH_MAX = 50   # 正常线宽
SOME_LARGE_WIDTH = 115       # 超宽交叉判定
MIN_PIXELS_THRESHOLD = 300   # 色素块像素过滤
blob_density = 0.4
NODE_PIXELS_MIN = 1500       # 节点认定像素峰值
MAX_OVERLAP_CHECK_BLOBS = 8
GC_INTERVAL = 10

# ================= 助手及网络、上传函数 =================
def scale_rect(rect_tuple):
    x, y, w, h = rect_tuple
    return (int(x * SCALE_X), int(y * SCALE_Y), int(w * SCALE_X), int(h * SCALE_Y))

def scale_point(cx, cy):
    return (int(cx * SCALE_X), int(cy * SCALE_Y))

def Connect_WIFI(ssid, key):
    """连接到指定的 WIFI 网络，阻塞直到连接成功"""
    print("\n[WIFI] 正在连接网络...")
    sta = network.WLAN(network.STA_IF)
    sta.connect(ssid, key)
    while sta.ifconfig()[0] == '0.0.0.0':
        os.exitpoint()
        time.sleep(0.5)
    ip_addr = sta.ifconfig()[0]
    print(f"[WIFI] 连接成功, 当前IP地址: {ip_addr}\n")
    return ip_addr

def ensure_dir(directory):
    if not directory or directory == '/': return
    directory = directory.rstrip('/')
    try:
        os.stat(directory)
    except OSError:
        if '/' in directory:
            parent = directory[:directory.rindex('/')]
            if parent and parent != directory:
                ensure_dir(parent)
        try:
            os.mkdir(directory)
        except OSError:
            pass

# 全局缓存 IP 地址，避免每次 getaddrinfo 消耗底层的 UDP 句柄和网络解析时间
global_server_addr = None

def get_server_addr(host, port):
    global global_server_addr
    if global_server_addr is None:
        ai = socket.getaddrinfo(host, port)
        global_server_addr = ai[0][-1]
    return global_server_addr

def do_capture_and_upload(car, node, ts, seq, img):
    car_dir = SAVE_PATH + car + "/"
    ensure_dir(car_dir)

    filename = "{}_{}_{}.jpg".format(ts, node, seq)
    filepath = car_dir + filename
    img.save(filepath)

    # 强制等待，确保新固件的缓存落盘
    time.sleep_ms(200)
    file_size = os.stat(filepath)[6]
    print(f"[业务] {seq}/5 照片已保存: {filepath}, 实际大小: {file_size} bytes")

    if file_size == 0:
        print("       -> [致命错误] 文件大小为 0，避免发送空文件！")
        return

    raw_s = None
    ssl_s = None

    try:
        raw_s = socket.socket()
        # 加上超时机制
        raw_s.settimeout(5.0)

        # 开启 SO_REUSEADDR 强制复用底层处于 TIME_WAIT 状态的端口资源
        raw_s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        addr = get_server_addr(MASTER_HOST, MASTER_PORT)
        print(f"       -> 正在上传至 {MASTER_HOST} ({addr[0]}) ...")
        raw_s.connect(addr)

        try:
            ssl_s = ssl.wrap_socket(raw_s, server_hostname=MASTER_HOST)
        except TypeError:
            ssl_s = ssl.wrap_socket(raw_s)

        path = "/api/upload_image?car={}&node={}&ts={}&seq={}".format(car, node, ts, seq)
        request_header_str = (
            "POST " + path + " HTTP/1.0\r\n" +
            "Host: " + MASTER_HOST + "\r\n" +
            "Connection: close\r\n" +
            "Content-Type: application/octet-stream\r\n" +
            "Content-Length: " + str(file_size) + "\r\n\r\n"
        )
        ssl_s.write(request_header_str.encode('utf-8'))

        with open(filepath, "rb") as f:
            while True:
                chunk = f.read(2048)
                if not chunk: break
                ssl_s.write(chunk)

        print("       -> 测试位置2 - 数据已全部 write")

        response = ssl_s.readline()
        if response and b"200 OK" in response:
            print("       -> [OK] HTTP 200 上传成功!")
        else:
            print("       -> [WARN] 未收到有效的 HTTP 200 响应。")

    except Exception as e:

        print(f"       -> [ERROR] 上传失败: {e}")
    finally:
        if ssl_s is not None:
            try: ssl_s.close()
            except: pass
        if raw_s is not None:
            try: raw_s.close()
            except: pass

        del ssl_s
        del raw_s
        gc.collect()

        time.sleep_ms(150)

# ================= 主程序入口 =================
if __name__=="__main__":
    #在启动模型前，先执行网络和目录初始化
    Connect_WIFI(WIFI_SSID, WIFI_PASS)
    ensure_dir(SAVE_PATH)

    # 动态加载部署配置
    root_path = "/sdcard/mp_deployment_source/"
    print(f"正在加载配置文件: {root_path}deploy_config.json")
    deploy_conf = read_json(root_path + "deploy_config.json")

    kmodel_path = root_path + deploy_conf["kmodel_path"]
    labels = deploy_conf["categories"]
    confidence_threshold = deploy_conf["confidence_threshold"]
    nms_threshold = deploy_conf["nms_threshold"]
    model_input_size = deploy_conf["img_size"]
    nms_option = deploy_conf["nms_option"]
    model_type = deploy_conf["model_type"]

    anchors = []
    if model_type == "AnchorBaseDet":
        anchors = deploy_conf["anchors"][0] + deploy_conf["anchors"][1] + deploy_conf["anchors"][2]

    display_mode="lcd"
    rgb888p_size=model_input_size
    display_size=[DISPLAY_WIDTH, DISPLAY_HEIGHT]

    print("初始化 Camera Pipeline...")
    pl = PipeLine(rgb888p_size=rgb888p_size, display_size=display_size, display_mode=display_mode)
    pl.create(ch1_frame_size=[PICTURE_WIDTH, PICTURE_HEIGHT])
    clock = time.clock()

    ob_det = DetectionApp(
        "video", kmodel_path, labels, model_input_size, anchors,
        model_type, confidence_threshold, nms_threshold,
        rgb888p_size, display_size, debug_mode=0
    )
    ob_det.config_preprocess()

    node_counter = 0
    node_in_view = False
    last_offset = 0
    gc_counter = 0
    frame_count = 0
    yolo_result = "none"
    yolo_boxes_cache = None

    HIT_THRESHOLD, MISS_THRESHOLD = 3, 3
    tracker = {label: {"hits": 0, "misses": 0, "active": False, "center_x": 0} for label in labels}
    last_ghost_check_box = None
    ghost_frame_counter = 0

    # ===== 新增：工作流全局业务变量 =====
    current_app_mode = NORMAL_MODE
    k230_state = K230_WAIT_NAME_STR
    seq = 1
    car, node, ts = "", "", ""
    uart_buffer = b""

    # ================= 主循环 =================
    while True:
        clock.tick()
        track_img = pl.sensor.snapshot(chn=CAM_CHN_ID_1)
        ai_img = pl.get_frame()
        pl.osd_img.clear()  # 每帧仅清空一次OSD

        # ================= A. 拦截串口并分发系统模式 =================
        data = uart.read()
        if data:
            uart_buffer += data

        # 取出完整的带换行命令进行切割
        while b'\n' in uart_buffer:
            line, uart_buffer = uart_buffer.split(b'\n', 1)
            try:
                msg = line.decode('utf-8').strip()
            except:
                continue

            if current_app_mode == NORMAL_MODE:
                if msg == '1':
                    current_app_mode = CAPTURE_MODE
                    k230_state = K230_WAIT_NAME_STR
                    seq = 1
                    uart.write(b"ACK\r\n")
                    print("\n[模式切入] 暂停 YOLO / 循迹，等待拍摄参数...")

            elif current_app_mode == CAPTURE_MODE:
                if k230_state == K230_WAIT_NAME_STR:
                    # 识别是否下发命名头规则，如: C1+N5+20260613143022
                    if '+' in msg:
                        parts = msg.split('+')
                        if len(parts) >= 3:
                            car, node, ts = parts[0], parts[1], parts[2]
                            print(f"[工作流] 解析参数: {car}, {node}, {ts}")
                            do_capture_and_upload(car, node, ts, seq, track_img)
                            uart.write(b"NEXT\r\n")
                            k230_state = K230_WAIT_CAPTURE

                elif k230_state == K230_WAIT_CAPTURE:
                    if msg == "CAPTURE":
                        seq += 1
                        do_capture_and_upload(car, node, ts, seq, track_img)
                        if seq >= 5:
                            uart.write(b"DONE\r\n")
                            current_app_mode = NORMAL_MODE # 5张拍完，互退还原
                            print("[模式退出] 工作流已全部完成，恢复 YOLO & 寻线系统\n")
                        else:
                            uart.write(b"NEXT\r\n")

        # ================= B. 拦截过滤：处于拍照业务时不跑占用极高的AI =================
        if current_app_mode == CAPTURE_MODE:
            # 屏幕打字做个极低占用的OSD提示，便于观察系统
            pl.osd_img.draw_string_advanced(10, 50, 32, f"CAPTURE MODE: {seq}/5", color=(0, 255, 0))
            if car:
                pl.osd_img.draw_string_advanced(10, 90, 32, f"{car} {node}", color=(255, 255, 0))
            pl.show_image()
            time.sleep_us(1)
            continue # [核心] 直接切回 while True，完全跳过下方的 YOLO 推理和寻线操作


        # =============== 以下均为正常模式(NORMAL_MODE)下的 YOLO及寻线操作 ===============

        # 1. YOLO推理模块 (每3帧跑1次)
        run_yolo_this_frame = (frame_count % 3 == 0)
        current_valid_objects = []

        if run_yolo_this_frame:
            res = ob_det.run(ai_img)

            # [新增优化] 活体抖动检测 (过滤底层脏数据)
            is_ghost = False
            if res and isinstance(res, dict) and 'boxes' in res and len(res['boxes']) > 0:
                current_first_box = list(res['boxes'][0])
                if last_ghost_check_box == current_first_box:
                    ghost_frame_counter += 1
                    if ghost_frame_counter >= 2:
                        is_ghost = True
                else:
                    ghost_frame_counter = 0
                    last_ghost_check_box = current_first_box
            else:
                ghost_frame_counter = 0
                last_ghost_check_box = None

            if is_ghost:
                res = None

            yolo_boxes_cache = res

            if res and isinstance(res, dict) and 'boxes' in res:
                try:
                    boxes, indices, scores = res['boxes'], res['idx'], res['scores']
                    for i in range(len(boxes)):
                        x1, y1, x2, y2 = boxes[i]
                        w, h = x2 - x1, y2 - y1
                        x, y = x1, y1
                        score = float(scores[i])
                        cls_id = int(indices[i])
                        class_name = labels[cls_id] if cls_id < len(labels) else "unknown"

                        area = w * h
                        bottom_y = y + h
                        center_x = x + w // 2

                        obj_info = {"class_name": class_name, "box": [x, y, w, h], "score": score,
                                    "area": area, "bottom_y": bottom_y, "center_x": center_x}
                        current_valid_objects.append(obj_info)
                except Exception as e:
                    print(f"解析字典数据出错: {e}")

        # 每帧都绘制YOLO检测框
        if yolo_boxes_cache:
            ob_det.draw_result(pl.osd_img, yolo_boxes_cache)

        # ====== 状态机更新与数据发送 ======
        if run_yolo_this_frame:
            current_candidates = {}
            for obj in current_valid_objects:
                if obj["class_name"] == "slave" or obj["bottom_y"] > 100:
                    if obj["class_name"] not in current_candidates or obj["bottom_y"] > current_candidates[obj["class_name"]]["bottom_y"]:
                        current_candidates[obj["class_name"]] = obj

            for label in labels:
                if label in current_candidates:
                    tracker[label]["hits"] += 1
                    tracker[label]["misses"] = 0
                    tracker[label]["center_x"] = current_candidates[label]["center_x"]
                    if tracker[label]["hits"] >= HIT_THRESHOLD:
                        tracker[label]["active"] = True
                else:
                    if tracker[label]["hits"] > 0 or tracker[label]["active"]:
                        tracker[label]["misses"] += 1
                        if tracker[label]["misses"] >= MISS_THRESHOLD:
                            tracker[label]["active"] = False
                            tracker[label]["hits"] = 0

        active_classes = [f"{label}:{tracker[label]['center_x']}" for label in labels if tracker[label]["active"]]
        yolo_result = "none" if len(active_classes) == 0 else "+".join(active_classes)

        # 2. 循迹逻辑模块 (每帧运行)
        best_offset = last_offset
        pl.osd_img.draw_rectangle(TRACKING_ROI_SCALED, color=(255, 255, 0, 0), thickness=2)
        blobs = track_img.find_blobs(BLACK_THRESHOLD, roi=TRACKING_ROI, pixels_threshold=MIN_PIXELS_THRESHOLD, area_threshold=MIN_PIXELS_THRESHOLD, merge=True, margin=5)

        is_node, valid_blobs = False, []
        if blobs:
            for b in blobs:
                valid_blobs.append(b)
                if (b.w() > NORMAL_LINE_WIDTH_MAX and b.pixels() > NODE_PIXELS_MIN and b.density() > blob_density) or b.w() > SOME_LARGE_WIDTH:
                    is_node = True

            if is_node:
                if not node_in_view:
                    node_counter += 1
                    node_in_view = True
                for b in valid_blobs:
                    pl.osd_img.draw_rectangle(scale_rect(b.rect()), color=(255, 0, 255, 0), thickness=4)
                pl.osd_img.draw_string_advanced(10, 80, 32, "NODE DETECTED", color=(255, 0, 255, 0), scale=2)
            else:
                if node_in_view: node_in_view = False
                best_blob = None
                if len(valid_blobs) == 1:
                    best_blob = valid_blobs[0]
                    best_offset = best_blob.cx() - FRAME_CENTER_X
                else:
                    min_diff = 9999
                    for b in valid_blobs:
                        current_offset = b.cx() - FRAME_CENTER_X
                        diff = abs(current_offset - last_offset)
                        if diff < min_diff:
                            min_diff = diff
                            best_blob = b
                            best_offset = current_offset

                last_offset = best_offset
                for b in valid_blobs:
                    pl.osd_img.draw_rectangle(scale_rect(b.rect()), color=(255, 255, 255, 255), thickness=2)
                if best_blob:
                    disp_cx, disp_cy = scale_point(best_blob.cx(), best_blob.cy())
                    pl.osd_img.draw_cross(disp_cx, disp_cy, color=(255, 255, 255, 255), size=15, thickness=3)
                pl.osd_img.draw_string_advanced(10, 80, 32, f"Offset: {best_offset}", color=(255, 255, 255, 255), scale=2)
        else:
            if node_in_view: node_in_view = False
            pl.osd_img.draw_string_advanced(10, 80, 32, "LOST LINE!", color=(255, 255, 0, 0))

        pl.osd_img.draw_string_advanced(10, 10, 32, f"FPS: {clock.fps():.1f}", color=(255, 255, 255, 0))
        pl.osd_img.draw_string_advanced(10, 45, 32, f"Nodes: {node_counter}", color=(255, 0, 255, 0))

        # ====== 数据发送与收尾处理 ======
        # (只有身处普通模式才会发送 YOLO/寻线偏移的数据发给串口)
        send_str = f"{best_offset},{int(is_node)},{yolo_result}\r\n"
        #print(f"发送串口结果: {yolo_result}")
        uart.write(send_str.encode('utf-8'))

        pl.show_image()

        gc_counter += 1
        if gc_counter >= GC_INTERVAL:
            gc.collect()
            gc_counter = 0

        frame_count += 1
        time.sleep_us(1)

    ob_det.deinit()
    pl.destroy()
