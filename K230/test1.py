import os
import cv2

def extract_frames(video_path, output_dir, num_frames=10):
    # 均匀抽帧
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        print(f"无法打开视频文件: {video_path}")
        return

    fps = cap.get(cv2.CAP_PROP_FPS)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    
    if fps <= 0 or total_frames <= 0:
        print(f"视频 {video_path} 元数据无效，跳过")
        cap.release()
        return

    # 算时长和间隔
    duration = total_frames / fps
    interval = duration / num_frames
    
    # 采样帧号
    frame_indices = [int(i * interval * fps) for i in range(num_frames)]
    frame_indices = [min(idx, total_frames-1) for idx in frame_indices]

    base_name = os.path.splitext(os.path.basename(video_path))[0]
    saved_count = 0

    for i, idx in enumerate(frame_indices):
        cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
        ret, frame = cap.read()
        if not ret:
            print(f"视频 {base_name} 第 {idx} 帧读取失败")
            continue
        
        output_path = os.path.join(output_dir, f"{base_name}_{i+1:02d}.jpg")
        cv2.imwrite(output_path, frame)
        saved_count += 1

    cap.release()
    print(f"从 {base_name} 成功提取 {saved_count} 张图片")

def main():
    input_dir = r"D:\version\radio"
    output_dir = r"D:\version\photo"

    os.makedirs(output_dir, exist_ok=True)

    for filename in os.listdir(input_dir):
        if filename.lower().endswith(".mp4"):
            video_path = os.path.join(input_dir, filename)
            print(f"正在处理: {filename}")
            extract_frames(video_path, output_dir)

    print("所有视频处理完成！")

if __name__ == "__main__":
    main()