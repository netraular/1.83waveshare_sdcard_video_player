import os
import subprocess
import shutil

script_dir = os.path.dirname(os.path.abspath(__file__))
input_dir = os.path.join(script_dir, 'videos')
output_dir = os.path.join(script_dir, 'avi')

if not os.path.exists(output_dir):
    os.makedirs(output_dir)

# Supported extensions
extensions = ('.mp4', '.avi', '.mov', '.mkv')

files = [f for f in os.listdir(input_dir) if f.lower().endswith(extensions)]

for f in files:
    input_path = os.path.join(input_dir, f)
    filename = os.path.splitext(f)[0]
    output_path = os.path.join(output_dir, filename + '.avi')
    
    print(f"Converting {f} to {output_path}...")
    
    # FFmpeg command
    # -c:v mjpeg: Use MJPEG video codec
    # -q:v 10: Video quality (lower is better, 2-31 usually). 10 is decent.
    # -c:a pcm_s16le: Uncompressed 16-bit PCM audio (widely supported in AVI)
    # -ar 44100: Audio sample rate
    # -ac 2: Stereo
    # filter: scale to cover 284x240, then crop to 284x240
    
    cmd = [
        'ffmpeg', '-y',
        '-i', input_path,
        '-c:v', 'mjpeg',
        '-q:v', '13', # Optimized quality for 30fps stability
        '-pix_fmt', 'yuvj420p', # Use YUV 4:2:0 for smaller size and faster decode
        '-r', '30',
        '-c:a', 'pcm_s16le',
        '-ar', '22050', # Default BSP sample rate
        '-ac', '1',     # Mono audio
        '-vf', 'scale=284:240:force_original_aspect_ratio=increase,crop=284:240,transpose=1',
        output_path
    ]
    
    try:
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        print(f"Successfully converted {f}")
    except subprocess.CalledProcessError as e:
        print(f"Error converting {f}: {e}")

print("All conversions finished.")
