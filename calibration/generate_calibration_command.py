from pathlib import Path

# List all directory names in the dataset directory
dataset_dir = Path("dataset")
dataset_dirs = sorted([f"/{d.name}/image_raw" for d in dataset_dir.iterdir() if d.is_dir()])

# Convert the list of directory names to a space-separated string
dataset_dirs_str = " ".join(dataset_dirs)

CAMERA_MODEL="pinhole-equi"
# Repeat CAMERA_MODEL for each directory in dataset_dirs
camera_models = " ".join([CAMERA_MODEL for _ in dataset_dirs])

# Generate the command to run the calibration script
print(f"rosrun kalibr kalibr_calibrate_cameras --bag /data/dataset.bag --target /data/custom.yaml --models {camera_models} --topics {dataset_dirs_str}")