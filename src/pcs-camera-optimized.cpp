/*
 * Artur Balanuta, Evan Li
 * pcs-camera-optimized.cpp
 */

#include <cstring>
#include <iostream>
#include <chrono>

#include <string>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include <librealsense2/rs.hpp>

#include <omp.h>
#include <immintrin.h>
#include <xmmintrin.h>

#define TIME_NOW    std::chrono::high_resolution_clock::now()
#define BUF_SIZE    5000000
#define CONV_RATE   1000.0
#define DOWNSAMPLE  1
#define PORT        8000

struct point {
    
    // Coordenates
    short x;
    short y;
    short z;

    // Colors
    char r;
    char g;
    char b; 
};

typedef std::chrono::high_resolution_clock clockTime;
typedef std::chrono::duration<double, std::milli> timeMilli;
typedef std::chrono::time_point<clockTime> timestamp;

char *filename;

bool display_updates = false;
bool send_buffer = false;
bool cutoff = false;
bool use_simd = false;
bool compress = false;
int num_of_threads = 1;
int client_sock = 0;
int sockfd = 0;

short *thread_buffers[16];

timestamp time_start, time_end;

float tf_mat[] =   {-0.99977970,  0.00926272,  0.01883480,  0.00000000,
                    -0.01638983,  0.21604544, -0.97624574,  3.41600000,
                    -0.01311186, -0.97633937, -0.21584603,  1.80200000,
                     0.00000000,  0.00000000,  0.00000000,  1.00000000};
                 
__m128 ss_a = _mm_set_ps(0, tf_mat[8], tf_mat[4], tf_mat[0]);
__m128 ss_b = _mm_set_ps(0, tf_mat[9], tf_mat[5], tf_mat[1]);
__m128 ss_c = _mm_set_ps(0, tf_mat[10], tf_mat[6], tf_mat[2]);
__m128 ss_d = _mm_set_ps(0, tf_mat[11], tf_mat[7], tf_mat[3]);

// Creates TCP stream socket and connects to the central computer.
void initSocket(int port) {
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    
    auto sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    // reuse SOCKET AND ADDRESS/PORT
    int opt = 1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR , &opt, sizeof(opt))) perror("setsockopt failed");

    if (sockfd < 0) {
        std::cerr << "\nSocket fd not received." << std::endl;
        exit(EXIT_FAILURE);
    }

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "\nBind failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, 3) < 0) {
        std::cerr << "\nListen failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "Waiting for client..." << std::endl;

    if ((client_sock = accept(sockfd, NULL, NULL)) < 0) {
        std::cerr << "\nConnection failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "Established connection with client_sock: " << client_sock << std::endl;
}

int sendXYZRGBPointcloud(rs2::points pts, rs2::video_frame color, short * buffer);

// Exit gracefully by closing all open sockets and freeing buffer
void sigintHandler(int dummy) {
    std::cout << "\n Exiting \n " << std::endl;
    exit(0);
}

void print_usage() {
    printf("\nUsage: pcs-camera-test-samples -f <samples.bag> -v (visualize)\n\n");
}

// Parse arguments for extra runtime options
void parseArgs(int argc, char** argv) {
    int c;
    while ((c = getopt(argc, argv, "hf:vst:cmz")) != -1) {
        switch(c) {
            case 'h':
                print_usage();
                exit(0);
            case 'f':
                filename = optarg;
                break;
            case 'v':
                display_updates = true;
                break;
            case 's':
                send_buffer = true;
                break;
            case 't':
                num_of_threads = atoi(optarg);
                break;
            case 'c':
                cutoff = true;
                break;
            case 'm':
                use_simd = true;
                break;
            case 'z':
                compress = true;
                break;
        }
    }
}

int main (int argc, char** argv) {
    parseArgs(argc, argv);              // Parse Arguments
    signal(SIGINT, sigintHandler);      // Set interrupt signal
    
    int buff_size = 0, buff_size_sum = 0;
    short *buffer = (short *)malloc(sizeof(short) * BUF_SIZE);
    
    if (filename == NULL) {
        char pull_request[1] = {0};
        rs2::pointcloud pc;
        rs2::pipeline pipe;
        rs2::pipeline_profile selection = pipe.start();
        rs2::device selected_device = selection.get_device();
        auto depth_sensor = selected_device.first<rs2::depth_sensor>();

        // Turn off laser emitter for better accuracy with multiple camera setup
        if (depth_sensor.supports(RS2_OPTION_EMITTER_ENABLED))
            depth_sensor.set_option(RS2_OPTION_EMITTER_ENABLED, 0.f);

        initSocket(PORT);
        signal(SIGINT, sigintHandler);

        // Loop until client disconnected
        while (1) {
            // if (timer)
            //     frame_start = TIME_NOW;
            
            // Wait for pull request
            if (recv(client_sock, pull_request, 1, 0) < 0) {
                std::cout << "Client disconnected" << std::endl;
                break;
            }
            if (pull_request[0] == 'Z') {          // Client requests color pointcloud (XYZRGB)
                // if (timer) {
                //     grab_frame_start = TIME_NOW;
                // }

                // Grab depth and color frames, and map each point to a color value
                auto frames = pipe.wait_for_frames();
                auto color = frames.get_color_frame();

                // if (timer) {
                //     grab_frame_end_calculate_start = TIME_NOW;
                // }
                pc.map_to(color);  // Maps color values to a point in 3D space

                auto depth = frames.get_depth_frame();

                auto pts = pc.calculate(depth);

                // if (timer) {
                //     calculate_end = TIME_NOW;
                // }

                buff_size = sendXYZRGBPointcloud(pts, color, buffer);
            }
            else {                                     // Did not receive a correct pull request
                std::cerr << "Faulty pull request" << std::endl;
                exit(EXIT_FAILURE);
            }
        }

        close(client_sock);
        close(sockfd);
    }
    else {
        std::cout << "Reading Frames from File: " << filename << std::endl;
        
        rs2::config cfg;
        rs2::pipeline pipe;
        rs2::pointcloud pc;
        
        cfg.enable_device_from_file(filename);
        rs2::pipeline_profile selection = pipe.start(cfg);

        rs2::device device = pipe.get_active_profile().get_device();
        std::cout << "Camera Info: " << device.get_info(RS2_CAMERA_INFO_NAME) << " FW ver:" << device.get_info(RS2_CAMERA_INFO_FIRMWARE_VERSION) << std::endl;
        if (num_of_threads) std::cout << "OpenMP Threads: " << num_of_threads << std::endl;
        
        //auto depth_stream = selection.get_stream(RS2_STREAM_DEPTH).as<rs2::video_stream_profile>();
        //auto resolution = std::make_pair(depth_stream.width(), depth_stream.height());
        //auto intr = depth_stream.get_intrinsics();

        // rs2::pipeline pipe;
        // pipe.start();

        // const auto CAPACITY = 5; // allow max latency of 5 frames
        // rs2::frame_queue queue(CAPACITY);
        // std::thread t([&]() {
        //     while (true)
        //     {
        //         rs2::depth_frame frame;
        //         if (queue.poll_for_frame(&frame))
        //         {
        //             frame.get_data();
        //             // Do processing on the frame
        //         }
        //     }
        // });
        // t.detach();

        // while (true)
        // {
        //     auto frames = pipe.wait_for_frames();
        //     queue.enqueue(frames.get_depth_frame());
        // }
        //get_extrinsics(const rs2::stream_profile& from_stream, const rs2::stream_profile& to_stream)

        int i = 0, last_frame = 0;
        double duration_sum = 0;
        
        rs2::frameset frames;

        if (send_buffer) initSocket(PORT);
        
        while (true)
        {    
            
            if (!pipe.poll_for_frames(&frames))
            {
                continue;
            }
            else
            {

                if (!send_buffer && last_frame > frames.get_frame_number()) break;
                if (last_frame == frames.get_frame_number()) continue;
                
                //std::cout << "Got Frame # " << last_frame << std::endl;
                last_frame = frames.get_frame_number();
                i++;

                //use frames here
                
                                                                    // stairs.bag vs sample.bag
                rs2::video_frame color = frames.get_color_frame();  // 0.003 ms vs 0.001ms
                rs2::depth_frame depth = frames.get_depth_frame();  // 0.001ms vs 0.001ms
                rs2::points pts = pc.calculate(depth);              // 27ms vs 27ms            
                pc.map_to(color);       // 0.01ms vs 0.02ms  // Maps color values to a point in 3D space
                
                time_start = TIME_NOW;
                buff_size = sendXYZRGBPointcloud(pts, color, buffer);   // 86ms vs 9.7ms
                time_end = TIME_NOW;

                std::cout << "Frame Time: " << timeMilli(time_end - time_start).count() \
                    << " ms " << "FPS: " << 1000.0 / timeMilli(time_end - time_start).count() \
                    << "\t Buffer size: " << float(buff_size)/(1<<20) << " MBytes" << std::endl;
                duration_sum += timeMilli(time_end - time_start).count();
                buff_size_sum += buff_size;

            }
        }
        
        pipe.stop();
        
        if (send_buffer)
        {
            close(client_sock);
            close(sockfd);
        }

        // Use last Frame to display frame Info
        rs2::video_frame color = frames.get_color_frame();
        rs2::depth_frame depth = frames.get_depth_frame();
        rs2::points pts = pc.calculate(depth);

        std::cout << "\n### Video Frames H x W : " << color.get_height() << " x " << color.get_width() << std::endl;
        std::cout << "### Depth Frames H x W : " << depth.get_height() << " x " << depth.get_width() << std::endl;
        std::cout << "### # Points : " << pts.size() << std::endl;
        
        std::cout << "\n### Total Frames = " << i << std::endl;
        std::cout << "### AVG Frame Time: " << duration_sum / i << " ms" << std::endl;
        std::cout << "### AVG FPS: " << 1000.0 / (duration_sum / i) << std::endl;
        
        if (num_of_threads)
        {
            std::cout << "### OpenMP Threads : " << num_of_threads   << std::endl;
        }else
        {
            std::cout << "### Running Serialized" << std::endl;
        }

        if (compress)
        {
            std::cout << "\n### Sending Compressed Stream" << std::endl;
            std::cout << "### AVG Bytes/Frame: " << float(buff_size_sum) / (i*1000000) << " MBytes" << std::endl;
            std::cout << "### AVG Compression Ratio " << float(buff_size_sum) / ( (pts.size()/100) * 5 * sizeof(short) * i) << " %" << std::endl;
        }else
        {
            std::cout << "\n### AVG Bytes/Frame: " << float(buff_size_sum) / (i*1000000) << " MBytes" << std::endl;
            std::cout << "### AVG Filter Compress Ratio " << float(buff_size_sum) / ( (pts.size()/100) * 5 * sizeof(short) * i) << " %" << std::endl;
        }
    }

    free(buffer);
    return 0;
}

bool initialized = false;
int pts_size;
int w, h, cl_bp, cl_sb, w_min, h_min;

// TODO CLEAN
float conv_rate, point5f, w_f, h_f, h_min_f, cl_bp_f, cl_sb_f;

// Float
__m128 _conv_rate, _cl_bp_f, _cl_sb_f, _f5, _w, _h, z_lo, z_hi, x_lo, x_hi;

// integ
__m128i _zero, _w_min, _h_min, _cl_bp, _cl_sb;


int copyPointCloudXYZRGBToBufferSIMD(rs2::points& pts, const rs2::video_frame& color, short * pc_buffer)
{
    const auto vert = pts.get_vertices();
    const rs2::texture_coordinate* tcrd = pts.get_texture_coordinates();
    const uint8_t* color_data = reinterpret_cast<const uint8_t*>(color.get_data());

    if (!initialized){
        initialized = true;
        pts_size = pts.size();
        w = color.get_width();
        h = color.get_height();
        cl_bp = color.get_bytes_per_pixel();
        cl_sb = color.get_stride_in_bytes();
        w_min = (w - 1);
        h_min = (h - 1);

        // TODO CLEAN
        conv_rate = CONV_RATE;
        point5f = .5f;
        w_f = float(w);
        h_f = float(h);
        //w_min_f = float(w_min);
        //h_min_f = float(h_min);
        cl_bp_f = float(cl_bp);
        cl_sb_f = float(cl_sb);

        // Float
        _conv_rate = _mm_broadcast_ss(&conv_rate);
        _cl_bp_f = _mm_broadcast_ss(&cl_bp_f);
        _cl_sb_f = _mm_broadcast_ss(&cl_sb_f);
        //_h_min_f = _mm_broadcast_ss(&h_min_f);
        _f5 = _mm_broadcast_ss(&point5f);
        
        _w = _mm_broadcast_ss(&w_f);
        _h = _mm_broadcast_ss(&h_f);
        z_lo = _mm_set_ps1(0);
        z_hi = _mm_set_ps1(1.5);
        x_lo = _mm_set_ps1(-2);
        x_hi = _mm_set_ps1(2);

        // integ
        _zero = _mm_setzero_si128();
        _w_min = _mm_set1_epi32(w_min);
        _h_min = _mm_set1_epi32(h_min);
        _cl_bp = _mm_set1_epi32(cl_bp);
        const __m128i _cl_sb = _mm_set1_epi32(cl_sb);
    }
    
    int global_count = 0;
    
    #pragma omp parallel for ordered schedule(static, 10000) num_threads(num_of_threads)
    for (int i = 0; i < pts_size; i += 4) {
            //unsigned int id = omp_get_thread_num();
        
        int i1 = i;
        int i2 = i+1;
        int i3 = i+2;
        int i4 = i+3;

        __attribute__((aligned(16))) float v_temp1[4];
        __attribute__((aligned(16))) float v_temp2[4];
        __attribute__((aligned(16))) float v_temp3[4];
        __attribute__((aligned(16))) float v_temp4[4];

        __attribute__((aligned(16))) int idx[4];
        __attribute__((aligned(16))) int idy[4];

        // load
        __m128 _x = _mm_set_ps(tcrd[i1].u, tcrd[i2].u, tcrd[i3].u, tcrd[i4].u); // load u
        __m128 _y = _mm_set_ps(tcrd[i1].v, tcrd[i2].v, tcrd[i3].v, tcrd[i4].v); // load v

        _x = _mm_fmadd_ps(_x, _w, _f5); // fma
        _y = _mm_fmadd_ps(_y, _h, _f5);

        // float to int
        __m128i _xi = _mm_cvttps_epi32(_x);
        __m128i _yi = _mm_cvttps_epi32(_y);

        _xi = _mm_max_epi32(_xi, _zero);    // max
        _yi = _mm_max_epi32(_yi, _zero);
        _xi = _mm_min_epi32(_xi, _w_min);   // min
        _yi = _mm_min_epi32(_yi, _h_min);
        
        _mm_storeu_si128((__m128i_u*)idx, _xi);
        _mm_storeu_si128((__m128i_u*)idy, _yi);

        int idx1 = idx[3]*cl_bp + idy[3]*cl_sb;
        int idx2 = idx[2]*cl_bp + idy[2]*cl_sb;
        int idx3 = idx[1]*cl_bp + idy[1]*cl_sb;
        int idx4 = idx[0]*cl_bp + idy[0]*cl_sb;

        // Point Transformation using SIMD instructions for Matrix Multiplication on xyz points
        __m128 ss_x1 = _mm_broadcast_ss(&vert[i].x);
        __m128 ss_y1 = _mm_broadcast_ss(&vert[i].y);
        __m128 ss_z1 = _mm_broadcast_ss(&vert[i].z);

        __m128 ss_x2 = _mm_broadcast_ss(&vert[i2].x);
        __m128 ss_y2 = _mm_broadcast_ss(&vert[i2].y);
        __m128 ss_z2 = _mm_broadcast_ss(&vert[i2].z);

        __m128 ss_x3 = _mm_broadcast_ss(&vert[i3].x);
        __m128 ss_y3 = _mm_broadcast_ss(&vert[i3].y);
        __m128 ss_z3 = _mm_broadcast_ss(&vert[i3].z);

        __m128 ss_x4 = _mm_broadcast_ss(&vert[i4].x);
        __m128 ss_y4 = _mm_broadcast_ss(&vert[i4].y);
        __m128 ss_z4 = _mm_broadcast_ss(&vert[i4].z);

        __m128 _v1 = _mm_fmadd_ps(ss_x1, ss_a, ss_d);
        _v1 = _mm_fmadd_ps(ss_y1, ss_b, _v1);
        _v1 = _mm_fmadd_ps(ss_z1, ss_c, _v1);

        __m128 _v2 = _mm_fmadd_ps(ss_x2, ss_a, ss_d);
        _v2 = _mm_fmadd_ps(ss_y2, ss_b, _v2);
        _v2 = _mm_fmadd_ps(ss_z2, ss_c, _v2);

        __m128 _v3 = _mm_fmadd_ps(ss_x3, ss_a, ss_d);
        _v3 = _mm_fmadd_ps(ss_y3, ss_b, _v3);
        _v3 = _mm_fmadd_ps(ss_z3, ss_c, _v3);

        __m128 _v4 = _mm_fmadd_ps(ss_x4, ss_a, ss_d);
        _v4 = _mm_fmadd_ps(ss_y4, ss_b, _v4);
        _v4 = _mm_fmadd_ps(ss_z4, ss_c, _v4);

        // SIMD multiply by conversion rate to be converted from floats to shorts later
        _v1 = _mm_mul_ps(_v1, _conv_rate); // multiply with _conv_rate
        _v2 = _mm_mul_ps(_v2, _conv_rate); // multiply with _conv_rate
        _v3 = _mm_mul_ps(_v3, _conv_rate); // multiply with _conv_rate
        _v4 = _mm_mul_ps(_v4, _conv_rate); // multiply with _conv_rate

        // Store SIMD Vectors into memory (arrays) 
        _mm_store_ps(v_temp1, _v1);
        _mm_store_ps(v_temp2, _v2);
        _mm_store_ps(v_temp3, _v3);
        _mm_store_ps(v_temp4, _v4);

        if (cutoff) {
            // Check if x and z points are within range
            __m128 z_pts = _mm_set_ps(vert[i].z, vert[i2].z, vert[i3].z, vert[i4].z);
            __m128 x_pts = _mm_set_ps(vert[i].x, vert[i2].x, vert[i3].x, vert[i4].x);

            __m128 z_gt_lo = _mm_cmpgt_ps(z_pts, z_lo);
            __m128 z_le_hi = _mm_cmple_ps(z_pts, z_hi);
            __m128 x_gt_lo = _mm_cmpgt_ps(x_pts, x_lo);
            __m128 x_le_hi = _mm_cmple_ps(x_pts, x_hi);

            __m128 z_mask = _mm_and_ps(z_gt_lo, z_le_hi);
            __m128 x_mask = _mm_and_ps(x_gt_lo, x_le_hi);
            __m128 pt_mask = _mm_and_ps(z_mask, x_mask);

            float pt_mask_f[4];
            _mm_store_ps(pt_mask_f, pt_mask);

            long count = -1;

            //v1
            if (pt_mask_f[0] != 0) {

                #pragma omp atomic capture
                count = global_count++;

                pc_buffer[count * 5 + 0] = short(v_temp1[0]);
                pc_buffer[count * 5 + 1] = short(v_temp1[1]);
                pc_buffer[count * 5 + 2] = short(v_temp1[2]);
                pc_buffer[count * 5 + 3] = color_data[idx1] + (color_data[idx1 + 1] << 8);
                pc_buffer[count * 5 + 4] = color_data[idx1 + 2];

                // printf("%.2f,%.2f = %f\n", vert[i].z, vert[i].x, pt_mask_f[0]);
            }
            
            //v2
            if (pt_mask_f[1] != 0) {
                
                #pragma omp atomic capture
                count = global_count++;

                pc_buffer[count * 5 + 0] = short(v_temp2[0]);
                pc_buffer[count * 5 + 1] = short(v_temp2[1]);
                pc_buffer[count * 5 + 2] = short(v_temp2[2]);
                pc_buffer[count * 5 + 3] = color_data[idx2] + (color_data[idx2 + 1] << 8);
                pc_buffer[count * 5 + 4] = color_data[idx2 + 2];

                // printf("%.2f,%.2f = %f\n", vert[i2].z, vert[i2].x, pt_mask_f[1]);
            }
            
            //v3
            if (pt_mask_f[2] != 0) {
                
                #pragma omp atomic capture
                count = global_count++;
                
                pc_buffer[count * 5 + 0] = short(v_temp3[0]);
                pc_buffer[count * 5 + 1] = short(v_temp3[1]);
                pc_buffer[count * 5 + 2] = short(v_temp3[2]);
                pc_buffer[count * 5 + 3] = color_data[idx3] + (color_data[idx3 + 1] << 8);
                pc_buffer[count * 5 + 4] = color_data[idx3 + 2];

                // printf("%.2f,%.2f = %f\n", vert[i3].z, vert[i3].x, pt_mask_f[2]);
            }

            //v4
            if (pt_mask_f[3] != 0) {
                
                #pragma omp atomic capture
                count = global_count++;

                pc_buffer[count * 5 + 0] = short(v_temp4[0]);
                pc_buffer[count * 5 + 1] = short(v_temp4[1]);
                pc_buffer[count * 5 + 2] = short(v_temp4[2]);
                pc_buffer[count * 5 + 3] = color_data[idx4] + (color_data[idx4 + 1] << 8);
                pc_buffer[count * 5 + 4] = color_data[idx4 + 2];

                // printf("%.2f,%.2f = %f\n", vert[i4].z, vert[i4].x, pt_mask_f[3]);
            }
        }

        else {
            //v1
            pc_buffer[i * 5 + 0] = short(v_temp1[0]);
            pc_buffer[i * 5 + 1] = short(v_temp1[1]);
            pc_buffer[i * 5 + 2] = short(v_temp1[2]);
            pc_buffer[i * 5 + 3] = color_data[idx1] + (color_data[idx1 + 1] << 8);
            pc_buffer[i * 5 + 4] = color_data[idx1 + 2];
            
            //v2
            pc_buffer[i * 5 + 5] = short(v_temp2[0]);
            pc_buffer[i * 5 + 6] = short(v_temp2[1]);
            pc_buffer[i * 5 + 7] = short(v_temp2[2]);
            pc_buffer[i * 5 + 8] = color_data[idx2] + (color_data[idx2 + 1] << 8);
            pc_buffer[i * 5 + 9] = color_data[idx2 + 2];
            
            //v3
            pc_buffer[i * 5 + 10] = short(v_temp3[0]);
            pc_buffer[i * 5 + 11] = short(v_temp3[1]);
            pc_buffer[i * 5 + 12] = short(v_temp3[2]);
            pc_buffer[i * 5 + 13] = color_data[idx3] + (color_data[idx3 + 1] << 8);
            pc_buffer[i * 5 + 14] = color_data[idx3 + 2];
            
            //v4
            pc_buffer[i * 5 + 15] = short(v_temp4[0]);
            pc_buffer[i * 5 + 16] = short(v_temp4[1]);
            pc_buffer[i * 5 + 17] = short(v_temp4[2]);
            pc_buffer[i * 5 + 18] = color_data[idx4] + (color_data[idx4 + 1] << 8);
            pc_buffer[i * 5 + 19] = color_data[idx4 + 2];
        }
    
}
    
    
    if (cutoff)
        return global_count;

    return pts_size;
}

// Converts the XYZ values into shorts for less memory overhead,
// and puts the XYZRGB values of each point into the buffer.
int copyPointCloudXYZRGBToBuffer(rs2::points& pts, const rs2::video_frame& color, short * pc_buffer) {

    const auto vertices = pts.get_vertices();
    const rs2::texture_coordinate* tex_coords = pts.get_texture_coordinates();
    const uint8_t* color_data = reinterpret_cast<const uint8_t*>(color.get_data());

    const int pts_size = pts.size();
    const int w = color.get_width();
    const int h = color.get_height();
    const int cl_bp = color.get_bytes_per_pixel();
    const int cl_sb = color.get_stride_in_bytes();
    const int w_min = w - 1;
    const int h_min = h - 1;
    
    // TODO Optimize return size

    // use 4 threads
    #pragma omp parallel for schedule(static, 10000) num_threads(num_of_threads)
    for (int i = 0; i < pts_size; i++) {

        if (cutoff)
        {
            if (!vertices[i].z) continue;
            if (!vertices[i].x) continue;
            if (vertices[i].z > 1.5) continue;
            if (!(-2 < vertices[i].x < 2)) continue;
        }
               
        int x = std::min(std::max(int(tex_coords[i].u*w + .5f), 0), w_min);
        int y = std::min(std::max(int(tex_coords[i].v*h + .5f), 0), h_min);
        
        int idx = x * cl_bp + y * cl_sb;

        // float x_p = tf_mat[0] * vertices[i].x + tf_mat[1] * vertices[i].y + tf_mat[2] * vertices[i].z + tf_mat[3];
        // float y_p = tf_mat[4] * vertices[i].x + tf_mat[5] * vertices[i].y + tf_mat[6] * vertices[i].z + tf_mat[7];
        // float z_p = tf_mat[8] * vertices[i].x + tf_mat[9] * vertices[i].y + tf_mat[10] * vertices[i].z + tf_mat[11];
        
        pc_buffer[i * 5    ] = static_cast<short>( vertices[i].x * CONV_RATE);
        pc_buffer[i * 5 + 1] = static_cast<short>( vertices[i].y * CONV_RATE);
        pc_buffer[i * 5 + 2] = static_cast<short>( vertices[i].z * CONV_RATE);
        pc_buffer[i * 5 + 3] = color_data[idx] + (color_data[idx + 1] << 8);
        pc_buffer[i * 5 + 4] = color_data[idx + 2];
    }

    return pts_size;

}

int sendXYZRGBPointcloud(rs2::points pts, rs2::video_frame color, short * buffer) {
    int size;
    
    // Clean Buffer
    memset(buffer, 0, BUF_SIZE);

    //TODO some issues with the buffer offset, on the receiver buff+short but size is int

    //TODO Try Zstandard v1.3.7 vs Snappy

    //TODO Investigate https://github.com/IntelRealSense/librealsense/wiki/API-Changes#from-2161-to-2162
    // rs2_project_color_pixel_to_depth_pixel - map pixel in the color image to pixel in depth image
    
    // TODO rs2_create_hole_filling_filter_block - Hole-Filling filter supports three modes of operation:

    // TODO rs2_create_temporal_filter_block - Temporal filter that rectifies depth values based on previously-available frames.

    // TODO Investigate https://github.com/IntelRealSense/librealsense/wiki/API-Changes#from-2161-to-2162

    if (use_simd)
    {
        size = copyPointCloudXYZRGBToBufferSIMD(pts, color, &buffer[0] + sizeof(short));
    }else
    {
        size = copyPointCloudXYZRGBToBuffer(pts, color, &buffer[0] + sizeof(short));
    }
    
    // Size in bytes of the payload
    size = 5 * size * sizeof(short);
    
    if (send_buffer)
    {   
        // Include the number of bytes in the payload
        memcpy(buffer, &size, sizeof(int));
        send(client_sock, (char *)buffer, size + sizeof(int), 0);
    }
    
    return size;
}
