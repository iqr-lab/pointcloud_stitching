# Calibration

Calibration is done with the Kalibr toolkit. Ensure that all edge servers are listed in [`HOSTS`](/HOSTS). Do NOT move any of the cameras physically during this process.

Critically, this process generates a chain of transformations. Therefore, transformation matrices are pairwse-relative. If not all cameras can see the target board at the same time, you can link together separate chains.

## Installation
Install `kalibr` following this [doc](https://iqr.cs.yale.edu/docs/computer-vision/kalibr.html).

Install the dependencies for this module by running `python host.py edge_scripts/calibration_dependencies.sh`. This script assumes you have `uv` properly configured. All scripts are intended to be at project root. Edge nodes are assumed to have the project files installed.

## Dataset Creation
1. Run `python host.py edge_scripts/capture_images.sh`. While this is running, make sure to move the calibration target around for about 20 seconds.
    - Larger target appearance + higher target orientation variance lead to better datasets 
1. Update the local path and SSH keys as necessary in  [`edge_scripts/transfer_images.sh`](/edge_scripts/transfer_images.sh) 
1. Run `python host.py edge_scripts/transfer_images.sh`. If images are still being captured, the script will error and tell you.
1. Run `cd calibration && python rename.py`
1. Set the `FOLDER` env var to the absolute path of the local `dataset` directory
    ```bash
    export FOLDER=$PWD/dataset # bash
    ```
1. Run the following to enter the `kalibr` Docker container terminal. Keep this open in a terminal window.
    ```bash
    xhost +local:docker
    docker run -it -e "DISPLAY" -e "QT_X11_NO_MITSHM=1" \
        -v "/tmp/.X11-unix:/tmp/.X11-unix:rw" \
        -v "$FOLDER:/data" kalibr
    ```
1. Run the following inside the `kalibr` container:
    ```bash
    source devel/setup.bash
    rosrun kalibr kalibr_bagcreater --folder /data/ --output-bag /data/dataset.bag # create rosbag from raw dataset
    ```

## Calibration
1. Generate the calibration command by running `python generate_calibration_command.py`
1. Run this outputted command inside the `kalibr` container
1. The calibration results will available at `dataset/dataset-camchain.yaml`. `T_cn_cnm1` is the transformation matrix to the **previous** camera's coordinate system. Keep this in mind when calculating final transformation matrices.
1. In [`src/pcs-multicamera-optimized.cpp`](../src/pcs-multicamera-optimized.cpp), adjust the `transform` array in `int main()` as necessary. Make sure to rebuild before running.

## Ceiling Calibration
1. The two ceiling cameras can be calibrated but they are relatively tricky - the best result obtained so far is the following:
    ```
    0.19199692344029018, -0.045889554322095114, -0.9803220543237724, 1.7082854571998982,
    0.29050534336855743, 0.9567967685195846, 0.012107403718190302, 0.2016156185905812,
    0.9374133703248414, -0.2871133792678394, 0.1970331966487422, 1.6116656746455273,
    0.0, 0.0, 0.0, 1.0;
    ```
