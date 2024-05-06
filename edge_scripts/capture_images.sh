source ~/calibration_venv/bin/activate.fish
nohup python ~/pointcloud_stitching/calibration/capture_images.py calibration_dataset > ~/collection_output.txt 2>&1 &