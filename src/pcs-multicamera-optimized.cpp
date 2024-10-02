/*
 * Evan Li, Artur Balanuta
 * pcs-multicamera-client.cpp
 *
 * Creates multiple TCP connections where each connection is sending
 * pointclouds in realtime to the client for post processing and
 * visualization. Each pointcloud is rotated and translated through
 * a hardcoded transform that was precomputed from a camera registration
 * step.
 */

#include <librealsense2/rs.hpp>
#include <pcl/point_cloud.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/common/transforms.h>
#include <pcl/io/ply_io.h>
#include <pcl/filters/voxel_grid.h>
// #include "mqtt/client.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <signal.h>
#include <chrono>
#include <thread>

typedef pcl::PointCloud<pcl::PointXYZ> pointCloudXYZ;
typedef pcl::PointCloud<pcl::PointXYZRGB> pointCloudXYZRGB;
typedef std::chrono::high_resolution_clock clockTime;
typedef std::chrono::time_point<clockTime> timePoint;
typedef std::chrono::duration<double, std::milli> timeMilli;

const int NUM_CAMERAS = 2;

// const int CLIENT_PORT = 8000;
const int SERVER_PORT = 8000;
const int BUF_SIZE = 5000000;
const int STITCHED_BUF_SIZE = 32000000;
const float CONV_RATE = 1000.0;
const char PULL_XYZ = 'Y';
const char PULL_XYZRGB = 'Z';

const std::string IP_ADDRESS[NUM_CAMERAS] = {"192.168.2.8", "192.168.2.9"};

int loop_count = 1;
bool clean = true;
bool fast = false;
bool timer = false;
bool save = false;
bool visual = false;
int downsample = 1;
int framecount = 0;
int server_sockfd = 0;
int client_sockfd = 0;
int sockfd_array[NUM_CAMERAS];
int socket_array[NUM_CAMERAS];
short *stitched_buf;
Eigen::Matrix4f transform[NUM_CAMERAS];
std::thread *pcs_thread[NUM_CAMERAS];

// Exit gracefully by closing all open sockets
void sigintHandler(int dummy)
{
    // client.disconnect();
    for (int i = 0; i < NUM_CAMERAS; i++)
    {
        close(sockfd_array[i]);
    }
    close(server_sockfd);
    close(client_sockfd);
}

// Parse arguments for extra runtime options
void parseArgs(int argc, char **argv)
{
    int c;
    while ((c = getopt(argc, argv, "hftsvd:n")) != -1)
    {
        switch (c)
        {
        // Displays the pointcloud without color, only XYZ values
        case 'n':
            clean = false;
            break;
        case 'f':
            fast = true;
            break;
        // Prints out the runtime of the main expensive functions
        case 't':
            timer = true;
            break;
        // Saves the first 20 frames in .ply
        case 's':
            save = true;
            break;
        // Visualizes the pointcloud in real time
        case 'v':
            visual = true;
            break;
        // Sets downsampling factor by specified amount
        case 'd':
            downsample = atoi(optarg);
            break;
        default:
        case 'h':
            std::cout << "\nMulticamera pointcloud stitching" << std::endl;
            std::cout << "Usage: pcs-multicamera-client [options]\n"
                      << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << " -h (help)        Display command line options" << std::endl;
            std::cout << " -f (fast)        Increases the frame rate at the expense of color" << std::endl;
            std::cout << " -t (timer)       Displays the runtime of certain functions" << std::endl;
            std::cout << " -s (save)        Saves 20 frames in a .ply format" << std::endl;
            std::cout << " -v (visualize)   Visualizes the pointclouds using PCL visualizer" << std::endl;
            std::cout << " -d (downsample)  Downsamples the stitched pointcloud by the specified integer" << std::endl;
            exit(0);
        }
    }
}

// Create TCP socket with specific port and IP address.
int initSocket(int port, std::string ip_addr, int index)
{

    std::cout << "i0 port:" << port << "Ip" << ip_addr << std::endl;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr(ip_addr.c_str());

    std::cout << "i1" << std::endl;

    // Create socket
    if ((sockfd_array[index] = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        std::cerr << "Couldn't create socket" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "i2" << std::endl;

    // Connect to camera server
    if ((connect(sockfd_array[index], (struct sockaddr *)&serv_addr, sizeof(serv_addr))) < 0)
    {
        std::cerr << "Connection failed at " << ip_addr << "." << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "i3" << std::endl;

    std::cout << "Connection made at " << sockfd_array[index] << std::endl;
    return 0;
}

// Sends pull request to socket to signal server to send pointcloud data.
void sendPullRequest(int sockfd, char pull_char)
{
    if (send(sockfd, &pull_char, 1, 0) < 0)
    {
        std::cerr << "Pull request failure from sockfd: " << sockfd << std::endl;
        exit(EXIT_FAILURE);
    }
}

// Helper function to read N bytes from the buffer to ensure that
// the entire buffer has been read from.
void readNBytes(int sockfd, unsigned int n, void *buffer)
{
    int total_bytes, bytes_read;
    total_bytes = 0;

    // Keep reading until N total_bytes have been read
    while (total_bytes < n)
    {
        if ((bytes_read = read(sockfd, buffer + total_bytes, n - total_bytes)) < 1)
        {
            std::cerr << "Receive failure" << std::endl;
            exit(EXIT_FAILURE);
        }

        total_bytes += bytes_read;
    }
}

// Parses the buffer and converts the short values into float points and
// puts the XYZ and RGB values into the pointcloud.
pointCloudXYZRGB::Ptr convertBufferToPointCloudXYZRGB(short *buffer, int size)
{
    int count = 0;
    pointCloudXYZRGB::Ptr new_cloud(new pointCloudXYZRGB);

    new_cloud->width = size / downsample;
    new_cloud->height = 1;
    new_cloud->is_dense = false;
    new_cloud->points.resize(new_cloud->width);

    for (int i = 0; i < size; i++)
    {
        if (i % downsample == 0)
        {
            new_cloud->points[count].x = (float)buffer[i * 5 + 0] / CONV_RATE;
            new_cloud->points[count].y = (float)buffer[i * 5 + 1] / CONV_RATE;
            new_cloud->points[count].z = (float)buffer[i * 5 + 2] / CONV_RATE;
            new_cloud->points[count].r = (uint8_t)(buffer[i * 5 + 3] & 0xFF);
            new_cloud->points[count].g = (uint8_t)(buffer[i * 5 + 3] >> 8);
            new_cloud->points[count].b = (uint8_t)(buffer[i * 5 + 4] & 0xFF);
            count++;
        }
    }

    return new_cloud;
}

// Reads from the buffer and converts the data into a new XYZRGB pointcloud.
void updateCloudXYZRGB(int thread_num, int sockfd, pointCloudXYZRGB::Ptr cloud)
{
    double update_total, convert_total;
    timePoint loop_start, loop_end, read_start, read_end_convert_start, convert_end;

    if (timer)
        read_start = std::chrono::high_resolution_clock::now();

    short *cloud_buf = (short *)malloc(sizeof(short) * BUF_SIZE);
    int size;

    // Read the first integer to determine the size being sent, then read in pointcloud
    readNBytes(sockfd, sizeof(int), (void *)&size);
    readNBytes(sockfd, size, (void *)&cloud_buf[0]);
    // Send a pull_XYZRGB request after finished reading from buffer
    sendPullRequest(sockfd, PULL_XYZRGB);

    if (timer)
        read_end_convert_start = std::chrono::high_resolution_clock::now();

    // Parse the pointcloud and perform transformation according to camera position
    *cloud = *convertBufferToPointCloudXYZRGB(&cloud_buf[0], size / sizeof(short) / 5);
    pcl::transformPointCloud(*cloud, *cloud, transform[thread_num]);

    free(cloud_buf);

    if (timer)
    {
        convert_end = std::chrono::high_resolution_clock::now();
        std::cout << "updateCloud " << thread_num << ": " << timeMilli(convert_end - read_end_convert_start).count() << " ms" << std::endl;
    }
}

// Primary function to update the pointcloud viewer with an XYZRGB pointcloud.
void runStitching()
{
    double total;
    timePoint loop_start, loop_end, stitch_start, stitch_end_viewer_start;

    pcl::visualization::PCLVisualizer::Ptr viewer(new pcl::visualization::PCLVisualizer("3D Viewer"));

    std::vector<pointCloudXYZRGB::Ptr, Eigen::aligned_allocator<pointCloudXYZRGB::Ptr>> cloud_ptr(NUM_CAMERAS);
    pointCloudXYZRGB::Ptr stitched_cloud(new pointCloudXYZRGB);
    pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGB> cloud_handler(stitched_cloud);

    std::cout << "-1" << std::endl;

    viewer->setBackgroundColor(0.05, 0.05, 0.05, 0);

    std::cout << "0" << std::endl;

    // Initializing cloud pointers, pointcloud viewer, and sending pull
    // requests to each camera server.
    for (int i = 0; i < NUM_CAMERAS; i++)
    {
        cloud_ptr[i] = pointCloudXYZRGB::Ptr(new pointCloudXYZRGB);
        sendPullRequest(sockfd_array[i], PULL_XYZRGB);
    }

    std::cout << "1" << std::endl;

    int i = 0;
    // Loop until the visualizer is stopped
    while (1)
    {
        if (timer)
            loop_start = std::chrono::high_resolution_clock::now();

        if (clean)
            stitched_cloud->clear();

        if (timer)
            stitch_start = std::chrono::high_resolution_clock::now();

        // Spawn a thread for each camera and update pointcloud, and perform transformation,
        for (int i = 0; i < NUM_CAMERAS; i++)
        {
            pcs_thread[i] = new std::thread(updateCloudXYZRGB, i, sockfd_array[i], cloud_ptr[i]);
        }
        // Wait for thread to finish running, then add cloud to stitched cloud
        for (int i = 0; i < NUM_CAMERAS; i++)
        {
            pcs_thread[i]->join();
            *stitched_cloud += *cloud_ptr[i];
        }

        if (timer)
            stitch_end_viewer_start = std::chrono::high_resolution_clock::now();

        // Update the pointcloud visualizer
        if (visual)
        {
            if (!i)
            {
                viewer->addPointCloud<pcl::PointXYZRGB>(stitched_cloud, cloud_handler, "cloud");
                viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 2, "cloud");
            }
            else
                viewer->updatePointCloud(stitched_cloud, "cloud");

            viewer->spinOnce();
            i++;

            if (viewer->wasStopped())
            {
                exit(0);
            }
        }

        if (timer)
        {
            double temp = timeMilli(stitch_end_viewer_start - stitch_start).count();
            total += temp;
            std::cout << "Stitch average: " << total / loop_count << " ms" << std::endl;
            loop_count++;
        }

        if (save)
        {
            std::string filename("pointclouds/stitched_cloud_" + std::to_string(framecount) + ".ply");
            pcl::io::savePLYFileBinary(filename, *stitched_cloud);
            std::cout << "Saved frame " << framecount << std::endl;
            framecount++;
            if (framecount == 20)
                save = false;
        }
    }
}

int main(int argc, char **argv)
{

    parseArgs(argc, argv);

    stitched_buf = (short *)malloc(sizeof(short) * STITCHED_BUF_SIZE);

    /* Reminder: how transformation matrices work :
                 |-------> This column is the translation, which represents the location of the camera with respect to the origin
    | r00 r01 r02 x |  \
    | r10 r11 r12 y |   }-> Replace the 3x3 "r" matrix on the left with the rotation matrix
    | r20 r21 r22 z |  /
    |   0   0   0 1 |    -> We do not use this line (and it has to stay 0,0,0,1)
    */

    // Camera 1 is the global frame
    transform[1] = Eigen::Matrix4f::Identity();

    transform[0] << 0.5935022481044256, -0.03449428319439147, 0.8040927968349786, -0.8489125400359394,
        0.09617852335329619, 0.9949615013234632, -0.028307287572989108, 0.0019325393711517556,
        -0.7990649367483048, 0.09413689665091773, 0.5938294970345968, 0.42644689416334686,
        0.0, 0.0, 0.0, 1.0;

    for (int i = 0; i < NUM_CAMERAS; i++)
    {
        initSocket(SERVER_PORT, IP_ADDRESS[i], i);
    }

    signal(SIGINT, sigintHandler);

    runStitching();

    close(sockfd_array[0]);

    close(server_sockfd);
    close(client_sockfd);

    return 0;
}