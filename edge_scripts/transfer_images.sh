grep -Fxq "done" ~/collection_output.txt

# Create device-specific calibration dataset
set HOSTNAME (hostname)
set DATASET_PATH /home/neilsong/Projects/pointcloud_stitching/calibration/dataset/$HOSTNAME
ssh neilsong@iqr-neil.local -i /home/lab/.ssh/iqr-neil "rm -rf '$DATASET_PATH/*'; mkdir -p $DATASET_PATH/"
scp -i /home/lab/.ssh/iqr-neil ~/calibration_dataset/* neilsong@iqr-neil.local:$DATASET_PATH