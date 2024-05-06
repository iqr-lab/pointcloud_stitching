from pathlib import Path

# List all directory names in the dataset directory
dataset_dir = Path("dataset")
dataset_dirs = [d.name for d in dataset_dir.iterdir() if d.is_dir()]

sorted_dirs = sorted(dataset_dirs)

for i, d in enumerate(sorted_dirs):
    new_name = f"{dataset_dir}/cam{i}"
    Path(f"{dataset_dir}/{d}").rename(new_name)
    print(f"Renamed {d} to cam{i}")