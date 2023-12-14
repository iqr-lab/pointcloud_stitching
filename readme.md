# Pointcloud Stitching for IQR Lab

## Overview
Scalable, multicamera distributed system for realtime pointcloud stitching in [IQR Lab](https://iqr-lab.github.io/). This program is currently designed to use the **D400 Series Intel RealSense** depth cameras. Using the [librealsense 2.0 SDK](https://github.com/IntelRealSense/librealsense), depth frames are grabbed and pointclouds are computed on the edge, before sending the raw XYZRGB values to a central computer over a TCP sockets. The central program stitches the pointclouds together and displays it a viewer using [PCL](http://pointclouds.org/) libraries.

<!-- This system has been designed to allow 10-20 cameras to be connected simultaneously. Currently, our set up involves each RealSense depth camera connected to its own Intel i7 NUC computer. Each NUC is connected to a local network via ethernet, as well as the central computer that will be doing the bulk of the computing. -->

## Calibration [Under Construction]

## Installation
Different steps of installation are required for installing the realsense camera servers versus the central computing system. The current instructions are for running on Ubuntu 22.04 LTS.

#### Camera servers on the edge
- Go to the IQR Lab [librealsense guide](https://github.com/IntelRealSense/librealsense) and follow the instructions to install the `librealsense` 2.0 SDK.
- Ensure that your `cmake` version is 3.1 or later. If not, download and install a newer version from the [CMake website](https://cmake.org/download/)
- Clone [this repository](https://github.com/iqr-lab/pointcloud_stitching) and `cd` into it.
- Build and install the camera edge server binaries

  ```
  mkdir build && cd build
  cmake ..
  make && sudo make install
  ```
- Install OpenCV [todo: need to test if this is necessary]

  ```
  sudo apt install libopencv-dev
  ```
- Install OPENGL [todo: need to test if this is necessary]

  ```
  sudo apt-get install libglfw3-dev libgl1-mesa-dev libglu1-mesa-dev
  ```

#### Central computing system
- Follow the instructions to download and install the `pcl` from their [website](https://pointclouds.org/downloads/). 
  - Ensure that your `pcl` version is 1.13 or later. If this version is not available in your package manager, you will need to download the source code and build it yourself.
- Clone [this repository](https://github.com/iqr-lab/pointcloud_stitching) and `cd` into it.
- Build and compile the central computing system binaries.
  ```
  mkdir build && cd build
  cmake .. -DBUILD_CLIENT=true
  make && sudo make install
  ```

## Usage
Each realsense is connected to an edge computer, which are all accessible through ssh from the central computer. 

To start running, do the following:

1. SSH to each edge computer and run:
    ```
    pcs-camera-optimized -s
    ```
  
    If the servers are setup correctly, each one should say `Waiting for client...` 
2. Then on the central computer, run:
    ```
    pcs-multicamera-client -v
    ```

    This begins the pointcloud stitching (`-v` for visualizing the pointcloud). 
    
    For more available options, run `pcs-multicamera-client -h` for help and an explanation of each option.

<!-- ## Optimized Code
To run the optimized version of pcs-camera-server, you will want to run pcs-camera-optimized. This contains benchmarking tools that show the runtime of the optimized version of processing the depth frames, performing transforms, and then converting the values. It also includes the theoretical FPS, but this is calculated without taking in to consideration the time it takes to grab a frame from the realsense as well as the time it takes to send the data over the network. To run the optimized code, run `pcs-camera-optimized`<br />
- Usage:
  ```
  -f <file> sample data set to run
  -s        send data to central camera server if available
  -m        use SIMD instructions
  -t <n>    use OpenMP with n threads
  ``` -->
  
