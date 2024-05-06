# Pointcloud Stitching for IQR Lab

## Overview
Scalable, multicamera distributed system for realtime pointcloud stitching in [IQR Lab](https://iqr-lab.github.io/). This program is currently designed to use the **D400 Series Intel RealSense** depth cameras. Using the [librealsense 2.0 SDK](https://github.com/IntelRealSense/librealsense), depth frames are grabbed and pointclouds are computed on the edge, before sending the raw XYZRGB values to a central computer over a TCP sockets. The central program stitches the pointclouds together and displays it a viewer using [PCL](http://pointclouds.org/) libraries.

### `host.py` Script
This Python script is designed to run any arbitrary script on each of the edge servers. We provide some useful scripts in [`edge_scripts/`](./edge_scripts). Run `python host.py -h` to see usage instructions.

Note: `host.py` follows the `iqr-vision-i.local` hostnaming convention.

## Installation
Different steps of installation are required for installing the realsense camera servers versus the central computing system. The current instructions are for running on Ubuntu 22.04 LTS.

#### Central Computer
1. Follow the instructions to download and install the `pcl` from their [website](https://pointclouds.org/downloads/). 
  - Ensure that your `pcl` version is 1.13 or later. If this version is not available in your package manager, you will need to download the source code and build it yourself.
1. Clone [this repository](https://github.com/iqr-lab/pointcloud_stitching) and `cd` into it.
1. Build and compile the central computing system binaries.
  ```
  mkdir -p build && cd build
  cmake .. -DBUILD_CLIENT=true
  make
  ```

#### Camera Edge Server
1. Go to the IQR Lab [librealsense guide](https://iqr-lab.github.io/docs/computer-vision/intel-realsense.html) and follow the instructions to install the `librealsense` 2.0 SDK.
1. Ensure that your `cmake` version is 3.1 or later. If not, download and install a newer version from the [CMake website](https://cmake.org/download/)
1. There are two methods for obtaining the source
    1. Release - Clone [this repository](https://github.com/iqr-lab/pointcloud_stitching) into `~/pointcloud_stitching`
    1. FS Mount - This allows all edge servers to share one, live version of the source
        1. Create an SSH key on the edge server to the central computer
        1. Test the key and accept the fingerprint
        1. Install `sshfs`: `sudo apt install sshfs`
        1. Modify [`edge_scripts/mount.sh`](edge_scripts/mount.sh) on the central computer with the correct user login, central computer hostname, absolute paths, and identity file
        1. Add this edge server's hostname to [`HOSTS`](/HOSTS)
        1. Run `python host.py edge_scripts/mount.sh`
        1. To set this FS mount as permanent, please follow [these instructions](https://www.digitalocean.com/community/tutorials/how-to-use-sshfs-to-mount-remote-file-systems-over-ssh#step-3-permanently-mounting-the-remote-filesystem)
1. Ensure that `~/pointcloud_stitching` is the path of the local repo
1. Run `python host.py edge_scripts/build_edge.sh`

## Usage
Each RealSense is connected to an edge computer, which are all accessible through ssh from the central computer. 

To start running, do the following:

1. SSH to each edge computer and run:
    ```
    pcs-camera-optimized -s
    ```
  
    If the servers are setup correctly, each one should say `Waiting for client...` 
1. Then on the central computer, run:
    ```
    pcs-multicamera-optimized -v
    ```

    This begins the pointcloud stitching (`-v` for visualizing the pointcloud). 
    
    For more available options, run `pcs-multicamera-client -h` for help and an explanation of each option.