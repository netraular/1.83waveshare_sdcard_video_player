import cv2
import os

video_dir = 'videos'
files = [f for f in os.listdir(video_dir) if f.endswith('.avi')]

for f in files:
    path = os.path.join(video_dir, f)
    cap = cv2.VideoCapture(path)
    if cap.isOpened():
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        fps = cap.get(cv2.CAP_PROP_FPS)
        print(f"{f}: {width}x{height} @ {fps}fps")
    cap.release()
