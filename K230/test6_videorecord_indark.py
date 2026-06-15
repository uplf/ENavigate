from media.mp4format import *
import time, math, os, gc, sys
import image
from media.sensor import *
from media.display import *
from media.media import *

# 导入串口资源
from ybUtils.YbUart import YbUart
uart = YbUart(baudrate=115200)

# 统一分辨率参数
WIDTH = 640
HEIGHT = 480
RECORD_TIME = 10  # 录制时长(秒)

PICTURE_WIDTH = 320
PICTURE_HEIGHT = 240
DISPLAY_WIDTH = 640
DISPLAY_HEIGHT = 480

class MP4Recorder:
    """
    MP4视频录制类 (使用定时阻塞写法)
    """
    def __init__(self, width=PICTURE_WIDTH, height=PICTURE_HEIGHT, max_record_time=10):
        self.width = width
        self.height = height
        self.max_record_time = max_record_time
        self.mp4_muxer = None
        self.frame_count = 0

    def start_recording(self, file_path):
        print(f"开始MP4录制... 保存路径: {file_path}")
        self.mp4_muxer = Mp4Container()
        mp4_cfg = Mp4CfgStr(self.mp4_muxer.MP4_CONFIG_TYPE_MUXER)

        if mp4_cfg.type == self.mp4_muxer.MP4_CONFIG_TYPE_MUXER:
            mp4_cfg.SetMuxerCfg(
                file_path,
                self.mp4_muxer.MP4_CODEC_ID_H265,
                self.width,
                self.height,
                self.mp4_muxer.MP4_CODEC_ID_G711U
            )

        self.mp4_muxer.Create(mp4_cfg)
        self.mp4_muxer.Start()

        start_time_ms = time.ticks_ms()

        try:
            while True:
                os.exitpoint()
                self.mp4_muxer.Process()
                self.frame_count += 1

                if self.frame_count % 30 == 0:
                    print(f"已录制帧数: {self.frame_count}")

                elapsed_time = time.ticks_ms() - start_time_ms
                if elapsed_time >= self.max_record_time * 1000:
                    print(">>> 录制已达到最大时长, 准备结束保存...")
                    break

        except BaseException as e:
            print(f"录制过程出错: {e}")

        finally:
            self.stop_recording()

    def stop_recording(self):
        if self.mp4_muxer:
            self.mp4_muxer.Stop()
            self.mp4_muxer.Destroy()
            print(">>> MP4录制完成, 文件已保存!")
            self.mp4_muxer = None

    def __del__(self):
        self.stop_recording()

def ensure_dir(directory):
    """递归创建目录"""
    if not directory or directory == '/': return
    directory = directory.rstrip('/')
    try:
        os.stat(directory)
        return
    except OSError:
        if '/' in directory:
            parent = directory[:directory.rindex('/')]
            if parent and parent != directory:
                ensure_dir(parent)
        try:
            os.mkdir(directory)
        except OSError:
            pass

def get_next_video_path(save_dir="/data/video/"):
    """获取下一个视频保存路径"""
    ensure_dir(save_dir)
    max_idx = 0
    try:
        files = os.listdir(save_dir)
        for filename in files:
            if filename.startswith("vid_") and filename.endswith(".mp4"):
                try:
                    num = int(filename[4:-4])
                    if num > max_idx: max_idx = num
                except ValueError:
                    pass
    except OSError:
        pass
    next_idx = max_idx + 1
    return f"{save_dir}vid_{next_idx:04d}.mp4"

# ================== 核心状态机切入点 ==================

def init_display_mode():
    """初始化屏幕投屏模式资源"""
    print(">>> 正在初始化投屏资源...")
    sensor = Sensor(width=WIDTH, height=HEIGHT, fps=30)
    sensor.reset()
    sensor.set_framesize(width=WIDTH, height=HEIGHT)
    sensor.set_pixformat(Sensor.RGB565)

    Display.init(Display.ST7701, width=WIDTH, height=HEIGHT, to_ide=True)
    MediaManager.init()
    sensor.run()
    return sensor

def deinit_display_mode(sensor):
    """彻底释放投屏模式资源以避免冲突"""
    print(">>> 正在释放投屏资源...")
    if sensor:
        sensor.stop()
    Display.deinit()
    MediaManager.deinit()
    time.sleep_ms(300) # 给予底层硬件断开和释放内存的缓冲时间
    gc.collect()

def main():
    os.exitpoint(os.EXITPOINT_ENABLE)

    # 1. 初始进入投屏模式
    sensor = init_display_mode()
    osd_img = image.Image(WIDTH, HEIGHT, image.ARGB8888)
    clock = time.clock()

    try:
        while True:
            os.exitpoint()
            clock.tick()

            # --- 检测串口指令 ---
            uart_data = uart.read()
            if uart_data and b'1' in uart_data:
                print(f"\n[检测到指令 '1']: 准备切换至独立录制模式")

                # 第一步：彻底摧毁当前投屏所有资源
                deinit_display_mode(sensor)
                sensor = None

                # 第二步：启动独立阻塞定时录制
                # (此时底层处于空置状态，录像库自己起sensor不受干扰)
                file_path = get_next_video_path("/data/video/")
                recorder = MP4Recorder(width=WIDTH, height=HEIGHT, max_record_time=RECORD_TIME)
                recorder.start_recording(file_path)

                # 第三步：录像已经结束(阻塞跳出)，清理残留并恢复投屏
                del recorder
                gc.collect() # 强行回收录制时的残留内存

                print("\n[录制结束]: 准备恢复正常投屏模式")
                sensor = init_display_mode()

                # 跳过本轮循环剩下的渲染代码，直接开始新的投屏周期
                continue

            # --- 正常投屏抓图与OSD ---
            if sensor:
                img = sensor.snapshot()

                osd_img.clear()
                osd_img.draw_string_advanced(10, 80, 32, "Previewing...", color=(0, 255, 0, 255))

                Display.show_image(osd_img, 0, 0, Display.LAYER_OSD3)
                if img is not None:
                    Display.show_image(img)

            gc.collect()
            time.sleep_us(10)

    except BaseException as e:
        print(f"程序退出或异常: {e}")
    finally:
        # 退出时的安全释放
        deinit_display_mode(sensor)

if __name__ == "__main__":
    main()
