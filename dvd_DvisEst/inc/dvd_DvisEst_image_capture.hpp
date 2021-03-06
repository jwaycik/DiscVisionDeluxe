#ifndef DVD_DVISEST_IMAGE_CAPTURE_HPP
#define DVD_DVISEST_IMAGE_CAPTURE_HPP

#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32)) && !defined(SPINNAKER_ALLOWED)
// not available in mingw64 for windows! (sad)
// I'm starting to think spinnaker and apriltag are never meant to
// be together on windows...
#else
#define SPINNAKER_ALLOWED
#endif

#include <string>
#include <iostream>

#include <atomic>

// OpenCV stuff
#include "opencv2/core.hpp"
#include <opencv2/core/utility.hpp>
#include "opencv2/imgproc.hpp"
#include "opencv2/calib3d.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"

// Structures
// Define struct to hold image matrices, timestamps, and frame IDs generated by the camera
struct image_capture_t
{
    image_capture_t()
        : image_data(cv::Mat()), timestamp_ns(0), frame_id(0) {}
    image_capture_t(cv::Mat _image_data, uint64_t _timestamp_ns, uint32_t _frame_id)
        : image_data(_image_data), timestamp_ns(_timestamp_ns), frame_id(_frame_id) {}
    cv::Mat     image_data;   // Image data from the camera capture, converted to cv::Mat
    uint64_t    timestamp_ns; // Timestamp reported by camera, nanoseconds, don't leave your program running for more than 500 years at a time!
    uint32_t    frame_id;     // Frame ID reported by camera, sequential, don't leave your program running for more than 90 days at a time!
};

// Functions
void dvd_DvisEst_image_capture_set_force_capture_thread_closure(const bool force_close);
void dvd_DvisEst_image_capture_set_capture_thread_ready(void);
bool dvd_DvisEst_image_capture_thread_ready(void);
double dvd_DvisEst_image_capture_get_fps(void);
uint32_t dvd_DvisEst_image_capture_get_camera_serial_number(void);
bool dvd_DvisEst_image_capture_test(void);
// Init camera inteface
void dvd_DvisEst_image_capture_init(const bool chime);
// Start collecting frames (this also purges remaining frames in the queue)
void dvd_DvisEst_image_capture_start(void);
// Stop collecting frames
void dvd_DvisEst_image_capture_stop(const bool camera_src);
// load test images into the capture queue and return
void dvd_DvisEst_image_capture_load_test_queue_threaded(const cv::String imgdir_src, const double dt);
bool dvd_DvisEst_image_capture_load_test_queue(const cv::String imgdir_src, const double dt);
uint32_t dvd_DvisEst_image_capture_get_image_capture_queue_size(void);
// Return the next captured image from the front of the queue
bool dvd_DvisEst_image_capture_get_next_image_capture(image_capture_t * image_capture, uint16_t * skipped_frames, std::atomic<uint8_t> * at_thread_mode, uint8_t thread_id, const bool calc_groundplane);
bool dvd_DvisEst_image_capture_image_capture_queue_empty(void);
bool dvd_DvisEst_image_capture_calculate_exposure_gain(const double des_centroid, const double centroid, const bool at_detect);
void dvd_DvisEst_image_capture_set_exposure_gain(const double exposure_us, const double gain);
void dvd_DvisEst_image_capture_get_exposure_gain(double * exposure_us, double * gain);

#endif // DVD_DVISEST_IMAGE_CAPTURE_HPP