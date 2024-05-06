# Captures images at 4Hz from the camera for 10 seconds and saves them to a folder

import cv2
import os
import sys
import time
import pyrealsense2 as rs
import numpy as np

import argparse

def main(folder_name):

    folder_name = sys.argv[1]
    if not os.path.exists(folder_name):
        os.makedirs(folder_name)
    else:
        print(f"Folder {folder_name} already exists. Overwriting all files in the folder.")
        for file in os.listdir(folder_name):
            os.remove(f"{folder_name}/{file}")

    pipeline = rs.pipeline()
    config = rs.config()
    config.enable_stream(rs.stream.color, 640, 480, rs.format.bgr8, 30)

    pipeline.start(config)

    # Give auto-exposure some time to adjust
    for _ in range(30):
        pipeline.wait_for_frames()

    start_time = time.time()
    while time.time() - start_time < 20:
        frames = pipeline.wait_for_frames()
        color_frame = frames.get_color_frame()
        color_image = np.asanyarray(color_frame.get_data())
        # Save image under the folder_name with the current timestamp, truncate decimal
        timestamp = format(color_frame.get_timestamp(), '.6f').replace('.', '')
        cv2.imwrite(f"{folder_name}/{timestamp}.jpg", color_image)

    pipeline.stop()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Capture images from camera')
    parser.add_argument('folder_name', type=str, help='Folder name to save images')
    args = parser.parse_args()
    main(args.folder_name)
    print("done")