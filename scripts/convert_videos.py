import os
import subprocess
import sys

script_dir = os.path.dirname(os.path.abspath(__file__))
input_dir = os.path.join(script_dir, 'videos')
output_dir = os.path.join(script_dir, 'avi')

if not os.path.exists(output_dir):
    os.makedirs(output_dir)

# Supported extensions
extensions = ('.mp4', '.avi', '.mov', '.mkv')

files = [f for f in os.listdir(input_dir) if f.lower().endswith(extensions)]

if not files:
    print(f"No video files found in {input_dir}")
    sys.exit(0)

fps = '24'
quality = '5'
audio_rate = '44100'
audio_channels = '1'

print("Conversion mode: 24 FPS, 240x240, mono")

for f in files:
    input_path = os.path.join(input_dir, f)
    filename = os.path.splitext(f)[0]
    output_path = os.path.join(output_dir, filename + '.avi')
    
    print(f"Converting {f} to {output_path}...")
    
    # FFmpeg command
    cmd = [
        'ffmpeg', '-y',
        '-i', input_path,
        '-c:v', 'mjpeg',
        '-q:v', quality,
        '-pix_fmt', 'yuvj420p',
        '-r', fps,
        '-c:a', 'pcm_s16le',
        '-ar', audio_rate,
        '-ac', audio_channels,
        '-vf', 'scale=240:240:force_original_aspect_ratio=increase,crop=240:240',
        output_path
    ]
    
    try:
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        print(f"Successfully converted {f}")
    except subprocess.CalledProcessError as e:
        print(f"Error converting {f}: {e}")

print("All conversions finished.")
