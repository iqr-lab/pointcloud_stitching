# Calibration

Calibration is done with the Kalibr toolkit. Ensure that all edge servers are listed in [`HOSTS`](/HOSTS). Do NOT move any of the cameras physically during this process.

## Installation
Install `kalibr` following this [doc](https://iqr.cs.yale.edu/docs/computer-vision/kalibr.html).

Install the dependencies for this module by running `python ../host.py edge_scripts/calibration_dependencies.sh`. This script assumes you have `uv` properly configured.

## Dataset Creation
1. Run `python ../host.py edge_scripts/capture_images.sh`. While this is running, make sure to move the calibration target around for about 20 seconds.
1. Update the local path and SSH keys as necessary in  [`edge_scripts/transfer_images.sh`](/edge_scripts/transfer_images.sh) 
1. Run `python ../host.py edge_scripts/transfer_images.sh`. If images are still being captured, the script will error and tell you.
1. Run `python rename.py`
1. Set the `FOLDER` env var to the absolute path of the local `dataset` directory
1. Run the following to enter the `kalibr` Docker container terminal. Keep this open in a terminal window.
    ```bash
    xhost +local:root
    docker run -it -e "DISPLAY" -e "QT_X11_NO_MITSHM=1" \
        -v "/tmp/.X11-unix:/tmp/.X11-unix:rw" \
        -v "$FOLDER:/data" kalibr
    ```
1. Run the following inside the `kalibr` container:
    ```bash
    source devel/setup.bash
    rosrun kalibr kalibr_bagcreater --folder /data/ --output-bag /data/dataset.bag
    ```

## Calibration
1. Generate the calibration command by running `python generate_calibration_command.py`
1. Run this command inside the `kalibr` container
1. The calibration results will available at `dataset/dataset-camchain.yaml`. `T_cn_cnm1` is the transformation matrix to the **previous** camera's coordinate system. Keep this in mind when calculating final transformation matrices.
1. In `/src/pcs-multicamera-optimized.cpp`, adjust the `transform` array as necessary. Make sure to rebuild before running.