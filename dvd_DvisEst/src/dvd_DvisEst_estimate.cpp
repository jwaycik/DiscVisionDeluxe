#include <dvd_DvisEst_estimate.hpp>
#include <dvd_DvisEst_image_capture.hpp>

#include <iostream>
#include <vector>
#include <cstring>
#include <cctype>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <iostream>
#include <fstream>

// Timer stuff
#include <unistd.h>
#include <chrono>
#define _BSD_SOURCE
#include <sys/time.h>
#include <stack>
#include <ctime>

// threading stuff
#include <thread>
#include <future>
#include <queue>
#include <mutex>
#include <atomic>

#include <exception>
#include <typeinfo>
#include <stdexcept>

// More opencv stuff
#include <opencv2/core/matx.hpp>

using namespace std;
using namespace cv;

// Define macros for our measurement queue
// Queue states are:
// - available -> No apriltag thread has claimed this slot yet
// - reserved  -> An apriltag thread has indicated that is is currently processing a measurement for this slot
// - populated -> Apriltag thread has returned with data, measurement is awaiting consumption by the Kalman Filter
#define MEAS_QUEUE_STATUS_AVAILABLE (0)
#define MEAS_QUEUE_STATUS_RESERVED  (1)
#define MEAS_QUEUE_STATUS_POPULATED (2)

// We need atomic arrays, but that will cause the compiler some sadness
// so we can cheat and use an eval-style macro to access our pseudo-array of atomics
// (this is undoubtably bad form, oh well, need ma loops)
// for now, let's just have 3 slots, we can expand this later if necessary pending how
// many concurrent apriltag threads we expect

#define KF_FILTER_PRED_DT_NS (MS_TO_NS(1.0)) // 1.0 ms, 1000Hz for now
#define KF_FILTER_PRED_DT_S  (NS_TO_S(KF_FILTER_PRED_DT_NS))
// if the filter isn't active, and we havent had a tag detection for at least this long
// allow frame skips again
#define KF_FILTER_TAG_DETECT_RESET_TIME_S (0.1) 

//Concatenate preprocessor tokens x and y after macro-expanding them.
/*#define JOIN_NX(x,y) (x##y)
#define JOIN(x,y) JOIN_NX(x,y)
#define MEAS_QUEUE_STATUS(y) JOIN(sv_meas_queue_status_,y)
#define MEAS_QUEUE_MEAS(y)   JOIN(sv_meas_queue_meas_,y)*/

// should this match the thread count in the apriltag threadpool? probably not if we can't process frames fast enough
#define MEAS_QUEUE_SIZE (2000)
std::vector<std::atomic<uint8_t>>  sv_meas_queue_status(MEAS_QUEUE_SIZE);
std::vector<dvd_DvisEst_kf_meas_t> sv_meas_queue_meas(MEAS_QUEUE_SIZE);
#define MEAS_QUEUE_STATUS(y) (sv_meas_queue_status[y])
#define MEAS_QUEUE_MEAS(y)   (sv_meas_queue_meas[y])

// current index of measurement queue
uint8_t              sv_meas_queue_read_idx      (0);
uint8_t              sv_meas_queue_write_idx     (0);
std::mutex           sv_meas_queue_write_mutex;

// filter prime queue (isolated from the apriltag threads)
std::deque<dvd_DvisEst_kf_meas_t> meas_prime_queue;

// are we getting apriltag detections? we may wish to suppress frame skips for now
std::atomic<bool> sv_kf_estimate_tags_detected (false);

std::atomic<uint8_t> sv_kf_estimate_stage (KF_EST_STAGE_MEAS_COLLECT);

// filter thread
std::thread kf_process_filter;

// ground plane transformation
cv::Matx33d R_CG = cv::Matx33d(0,0,0,0,0,0,0,0,0);
cv::Matx31d T_CG = cv::Matx31d(0,0,0);

// Kalman Filter statics
// initial covariances values
#define LIN_POS_VAR_INIT  (0.1)   //(m^2)
#define LIN_VEL_VAR_INIT  (10.0)  //(m/s)^2
#define ANG_POS_VAR_INIT  (0.1)   //(rad)^2
#define ANG_VEL_VAR_INIT  (10.0)  //(rad/s)^2

// Fixed meas variance for now
#define LIN_POS_MEAS_VAR  (0.01)
#define ANG_POS_MEAS_VAR  (0.01)
// function of dt
#define LIN_POS_PROC_VAR  (0.5 * KF_FILTER_PRED_DT_S)
#define LIN_VEL_PROC_VAR  (20.0 * KF_FILTER_PRED_DT_S)
#define ANG_POS_PROC_VAR  (0.5 * KF_FILTER_PRED_DT_S)
#define ANG_VEL_PROC_VAR  (20.0 * KF_FILTER_PRED_DT_S)

dvd_DvisEst_kf_state_t sv_kf_state;
dvd_DvisEst_kf_state_t sv_kf_ideal_state;

std::atomic<uint32_t> meas_count_populated (0);
std::atomic<uint32_t> meas_count_empty     (0);

std::atomic<uint32_t> sv_last_meas_frame_id (0);
std::atomic<uint32_t> sv_last_pop_meas_frame_id (0);

// log file pointers
bool log_meas  (false);
bool log_state (false);
std::ofstream meas_csvlog;
std::ofstream state_csvlog;
std::ofstream state_out_csvlog;

// Static Funcs
static bool get_linear_fit(double * slope, double * x, double * y, const uint16_t n);

static void kf_prediction_step(void);
static void kf_check_for_ideal_output_state(void);
static void kf_meas_update_step(void);
static void angle_hyzer_pitch_spin_from_R(double * hyzer_angle, double * pitch_angle, double * spin_angle, cv::Matx33d R_GD);
static void kf_get_kalman_gain(const double S2dm, const double MAT2X2(P), double VEC2(K));
static void kf_apply_meas_update_state(const double VEC2(K), const double innovation, double * pos_state, double * vel_state);
static void kf_apply_meas_update_var(const double VEC2(K), double MAT2X2(P));
static void kf_prediction_state(const double dt, double * pos_state, double * vel_state);
static void kf_prediction_var(const double S2dp, const double S2vp, const double dt, double MAT2X2(P));

static uint8_t get_next_meas_queue_size(uint8_t start_idx)
{
  uint8_t next_idx = start_idx + 1;
  if(next_idx > MEAS_QUEUE_SIZE - 1)
  {
    next_idx = 0;
  }
  return next_idx;
}

// Get time stamp in nanoseconds.
static uint64_t nanos()
{
  uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::
          now().time_since_epoch()).count();
  return ns; 
}

// Get boot-time stamp in nanoseconds.
static uint64_t uptime_get_ns()
{
  static uint64_t start_time_ns = 0;
  if(start_time_ns == 0)
  {
    start_time_ns = nanos();
  }

  return (nanos() - start_time_ns); 
}

// timing and profiling
static uint64_t profile_start_time_ns = 0;
void tic() {
  profile_start_time_ns = nanos();
}
void toc() {
  const uint64_t profile_end_time_ns = nanos();
  double s_elapsed = NS_TO_S(profile_end_time_ns - profile_start_time_ns);
  std::cerr << "******* Time elapsed: "
            << s_elapsed * 1000
            << " ms ("
            << 1.0/s_elapsed * (int)(meas_count_populated + meas_count_empty)
            << " Hz for "
            << (int)(meas_count_populated + meas_count_empty)
            << " samples). ["
            << (int)(meas_count_populated)
            << "/"
            << (int)(meas_count_empty)
            << "] valid/empty meas *******"
            << endl;

/*  // why is this necessary? do your job flush...
  int i;
  for(i=0;i<10;i++)
  {
    std::cerr << std::endl;
  }*/
}

// write measurements and states to output files
static void meas_csv_log_open()
{
  meas_csvlog.open("meas.csv", std::ios_base::trunc);// discard old file contents each run
  meas_csvlog << "time_ms, meas_time_ms, frame_id, lin_x_m, lin_y_m, lin_z_m, ang_h_rad, ang_p_rad, ang_s_rad, disc_index, player" << endl;
}

static void meas_csv_log_close()
{
  meas_csvlog.close();
}

static void meas_csv_log_write(dvd_DvisEst_kf_meas_t * meas, uint64_t state_time_ns, const bool filter_active)
{
  char out[256];
  sprintf(
      out, "%0.8f, %0.8f, %u, %0.3f, %0.3f, %0.3f, %0.5f, %0.5f, %0.5f, %d, %d, %d",
      NS_TO_MS(state_time_ns),
      NS_TO_MS(meas->timestamp_ns), 
      meas->frame_id, 
      meas->lin_xyz_pos[0], meas->lin_xyz_pos[1], meas->lin_xyz_pos[2], 
      meas->ang_hps_pos[0], meas->ang_hps_pos[1], meas->ang_hps_pos[2], 
      meas->disc_index, meas->player, filter_active
      );

  meas_csvlog << out << endl;
}

static void state_csv_log_open()
{
  state_csvlog.open("state.csv", std::ios_base::trunc);// discard old file contents each run
  state_csvlog << 
    "time_ms, lin_x_pos, lin_y_pos, lin_z_pos, lin_x_vel, lin_y_vel, lin_z_vel, "
             "ang_h_pos, ang_p_pos, ang_s_pos, ang_h_vel, ang_p_vel, ang_s_vel, "
             "lin_x_pos_var, lin_y_pos_var, lin_z_pos_var, "
             "lin_x_vel_var, lin_y_vel_var, lin_z_vel_var, "
             "ang_h_pos_var, ang_p_pos_var, ang_s_pos_var, "
             "ang_h_vel_var, ang_p_vel_var, ang_s_vel_var"
    << endl;
}

static void state_csv_log_close()
{
  state_csvlog.close();
}

static void state_csv_log_write(dvd_DvisEst_kf_state_t * state)
{
  char out[512];
  sprintf(
      out, "%0.8f, %0.3f, %0.3f, %0.3f, %0.3f, %0.3f, %0.3f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f",
      NS_TO_MS(state->timestamp_ns),      
      state->lin_xyz[0].pos, state->lin_xyz[1].pos, state->lin_xyz[2].pos,
      state->lin_xyz[0].vel, state->lin_xyz[1].vel, state->lin_xyz[2].vel,
      state->ang_hps[0].pos, state->ang_hps[1].pos, state->ang_hps[2].pos,
      state->ang_hps[0].vel, state->ang_hps[1].vel, state->ang_hps[2].vel,
      state->lin_xyz[0].var[i2x2(0,0)], state->lin_xyz[1].var[i2x2(0,0)], state->lin_xyz[2].var[i2x2(0,0)],
      state->lin_xyz[0].var[i2x2(1,1)], state->lin_xyz[1].var[i2x2(1,1)], state->lin_xyz[2].var[i2x2(1,1)],
      state->ang_hps[0].var[i2x2(0,0)], state->ang_hps[1].var[i2x2(0,0)], state->ang_hps[2].var[i2x2(0,0)],
      state->ang_hps[0].var[i2x2(1,1)], state->ang_hps[1].var[i2x2(1,1)], state->ang_hps[2].var[i2x2(1,1)]
      );

  state_csvlog << out << endl;
}

static void state_out_csv_log_open()
{
  state_out_csvlog.open("state_out.csv", std::ios_base::trunc);// discard old file contents each run
  state_out_csvlog << 
    "time_ms, lin_x_pos, lin_y_pos, lin_z_pos, lin_x_vel, lin_y_vel, lin_z_vel, "
             "ang_h_pos, ang_p_pos, ang_s_pos, ang_h_vel, ang_p_vel, ang_s_vel, "
             "lin_x_pos_var, lin_y_pos_var, lin_z_pos_var, "
             "lin_x_vel_var, lin_y_vel_var, lin_z_vel_var, "
             "ang_h_pos_var, ang_p_pos_var, ang_s_pos_var, "
             "ang_h_vel_var, ang_p_vel_var, ang_s_vel_var"
    << endl;
}

static void state_out_csv_log_close()
{
  state_out_csvlog.close();
}

static void state_out_csv_log_write(dvd_DvisEst_kf_state_t * state)
{
  char out[512];
  sprintf(
      out, "%0.8f, %0.3f, %0.3f, %0.3f, %0.3f, %0.3f, %0.3f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f, %0.5f",
      NS_TO_MS(state->timestamp_ns),      
      state->lin_xyz[0].pos, state->lin_xyz[1].pos, state->lin_xyz[2].pos,
      state->lin_xyz[0].vel, state->lin_xyz[1].vel, state->lin_xyz[2].vel,
      state->ang_hps[0].pos, state->ang_hps[1].pos, state->ang_hps[2].pos,
      state->ang_hps[0].vel, state->ang_hps[1].vel, state->ang_hps[2].vel,
      state->lin_xyz[0].var[i2x2(0,0)], state->lin_xyz[1].var[i2x2(0,0)], state->lin_xyz[2].var[i2x2(0,0)],
      state->lin_xyz[0].var[i2x2(1,1)], state->lin_xyz[1].var[i2x2(1,1)], state->lin_xyz[2].var[i2x2(1,1)],
      state->ang_hps[0].var[i2x2(0,0)], state->ang_hps[1].var[i2x2(0,0)], state->ang_hps[2].var[i2x2(0,0)],
      state->ang_hps[0].var[i2x2(1,1)], state->ang_hps[1].var[i2x2(1,1)], state->ang_hps[2].var[i2x2(1,1)]
      );

  state_out_csvlog << out << endl;
}

// Get the linear fit from a set of x, y points
static bool get_linear_fit(double * slope, double * x, double * y, const uint16_t n)
{
  *slope = 0;

  if( n < 2 ) {
    // Fail: infinitely many lines passing through this single point
    return false;
  }

  double sumX=0, sumY=0, sumXY=0, sumX2=0;
  for(int i = 0; i < n; i++) 
  {
    sumX  += x[i];
    sumY  += y[i];
    sumXY += x[i] * y[i];
    sumX2 += x[i] * x[i];
  }

  const double xMean = sumX / (double)n;
  const double yMean = sumY / (double)n;
  const double denominator = sumX2 - sumX * xMean;

  // You can tune the eps (1e-7) below for your specific task
  if( std::fabs(denominator) < 1e-7 ) {
    // Fail: it seems a vertical line
    return false;
  }
  *slope = (sumXY - sumX * yMean) / denominator;
  // y offset: yInt = yMean - _slope * xMean;
  return true;
}

// Init Kalman filter states and measurement queues
bool dvd_DvisEst_estimate_init(cv::String gnd_plane_file, const bool kflog)
{
  cerr << "Call dvd_DvisEst_estimate_init" << endl;

  // init structs to zero
  memset(&sv_kf_state, 0, sizeof(dvd_DvisEst_kf_state_t));
  memset(&sv_kf_ideal_state, 0, sizeof(dvd_DvisEst_kf_state_t));

  int i;
  for(i = 0; i < MEAS_QUEUE_SIZE; i++)
  {
    MEAS_QUEUE_STATUS(i) = MEAS_QUEUE_STATUS_AVAILABLE;
    memset(&MEAS_QUEUE_MEAS(i), 0, sizeof(dvd_DvisEst_kf_meas_t));
  }

  // enable test logging
  if(kflog)
  {
    log_meas  = true;
    log_state = true;
  }

  // init test logs
  if(log_meas)
  {
    meas_csv_log_open();
  }
  if(log_state)
  {
    state_csv_log_open();
  }

  // load ground plane transformation
  FileStorage fs;
  fs.open(gnd_plane_file, FileStorage::READ);
  if (!fs.isOpened())
  {
    cerr << "Failed to open gnd_plane_file: " << gnd_plane_file << endl;
    return false;
  }

  FileNode R_CG_fn = fs["R_CG"];
  FileNode T_CG_fn = fs["T_CG"];

  try
  {
    // looks like we can just set these directly due to contiguous memory
    R_CG = R_CG_fn.mat();
    T_CG = T_CG_fn.mat();
    //cv::Mat R_CG_mat = R_CG_fn.mat();
    //cv::Mat T_CG_mat = T_CG_fn.mat();

    /*int j;
    for(i = 0; i < 3; i++)
    {      
      T_CG(i, 0) = T_CG_mat.at<double>(i, 0);
      for(j = 0; j < 3; j++)
      {
        R_CG(i, j) = R_CG_mat.at<double>(i, j);
      }
    }*/
  }
  catch (...) 
  {
    std::exception_ptr p = std::current_exception();
    std::cerr <<(p ? p.__cxa_exception_type()->name() : "null") << std::endl;
    exit(1);
  }

  return true;
}

void dvd_DvisEst_estimate_set_tags_detected(bool tags_detected, uint32_t frame_id)
{
  // don't let this update past the ACTIVE stage of the filter
  if(sv_kf_estimate_stage < KF_EST_STAGE_PRIME)
  {
    sv_kf_estimate_tags_detected = tags_detected;
    if(tags_detected && frame_id > 0)
    {
      // Update timestamps, even if we are in Scout mode, to prevent things from automatically timing back out
      // TODO: Address the race condition here when our frame_skip_max is large enough to time out during frame processing
      // maybe this isn't a problem since the timeout is based on processed frame ID?
      sv_last_meas_frame_id     = frame_id;
      sv_last_pop_meas_frame_id = frame_id;
    }
  }
}

bool dvd_DvisEst_estimate_get_tags_detected()
{
  return sv_kf_estimate_tags_detected;
}

uint8_t dvd_DvisEst_get_estimate_stage()
{
  return sv_kf_estimate_stage;
}

bool dvd_DvisEst_estimate_complete()
{
  return (sv_kf_estimate_stage == KF_EST_STAGE_COMPLETE);
}

// Indicate that a new frame has been received, and is currently in processing
// reserve a slot in the incoming measurement queue so that measurements are processed in the correct order
bool dvd_DvisEst_estimate_reserve_measurement_slot(uint32_t frame_id, uint8_t * slot_id, uint16_t skipped_frames)
{
  // increment skipped frames count for book-keeping
  meas_count_empty += skipped_frames;

  // TODO: use frame_id to help with consumption order?

  // only give out next slot if it is available
  // only give out next slot to preserve adjacency

  // mutex access to the measurement queue allocation
  sv_meas_queue_write_mutex.lock();
  
  const uint16_t next_idx = get_next_meas_queue_size(sv_meas_queue_write_idx);

  if(MEAS_QUEUE_STATUS(next_idx) == MEAS_QUEUE_STATUS_AVAILABLE)
  {
    sv_meas_queue_write_idx = next_idx;
    MEAS_QUEUE_STATUS(next_idx) = MEAS_QUEUE_STATUS_RESERVED;
    (*slot_id) = next_idx;
    sv_meas_queue_write_mutex.unlock();
    return true;
  }
  sv_meas_queue_write_mutex.unlock();
  return false;
}
// Perhaps AprilTag detection failed? cancel our slot reservation
void dvd_DvisEst_estimate_cancel_measurement_slot(const uint8_t slot_id, const bool meas_mode, const uint32_t frame_id)
{
  // If we are returning an empty frame in measurement mode, set the latest frame_id to indicate detection timeouts
  if(meas_mode && frame_id >= sv_last_pop_meas_frame_id)
  {
    sv_last_meas_frame_id = frame_id;
  }

  // If we're in scout mode but the frame, force the timeout

  // free measurement slot
  //cerr << "Cancel measurement in slot " << (int)slot_id << endl;
  MEAS_QUEUE_STATUS(slot_id) = MEAS_QUEUE_STATUS_AVAILABLE;

  // indicate that measurement returned empty (and thus popped an empty frame)
  if(meas_mode)
  {
    meas_count_empty++;
  }
}

// Add the actual measurement output to a previously reserved slot in the incoming queue
void dvd_DvisEst_estimate_fulfill_measurement_slot(uint8_t slot_id, dvd_DvisEst_kf_meas_t * kf_meas)
{
  if(kf_meas->frame_id == 0)
  {
    cerr << "************************************************************** frame_id of zero reported to meas queue!" << endl;
  }

  // Indicate that this frame ID was measured, and populated with an apriltag
  sv_last_meas_frame_id     = kf_meas->frame_id;
  sv_last_pop_meas_frame_id = kf_meas->frame_id;

  // add measurement to queue
  MEAS_QUEUE_MEAS(slot_id)   = (*kf_meas);
  // mark measurement slot populated and ready for consumption
  MEAS_QUEUE_STATUS(slot_id) = MEAS_QUEUE_STATUS_POPULATED;

  // indicate the measurement returned with a tag detection
  meas_count_populated++;
}

// Transform Apriltag measurement into KF disc measurement (includes ground plane transformation)
// Transform camera-to-disc transformation into groundplane-to-disc transformation
// Then compute the disc measurement
void dvd_DvisEst_estimate_transform_measurement(cv::Matx33d R_CD, cv::Matx31d T_CD, dvd_DvisEst_kf_meas_t * kf_meas)
{
  try
  {
  // rotate by base groundplane
  // R_GD = R_CG * R_CD;
  cv::Matx33d R_GD = R_CG * R_CD;

  // rotate xyz_CD positions into xyz_GD frame
  // subtract base xyz offset defined in CG frame
  cv::Matx31d T_GD = R_CG * (T_CD - T_CG);
  //T_GD = R_CG * T_GD;
  // invert the y axis per our axis defs
  T_GD(1, 0) = -T_GD(1, 0);

  // Get the angular measurement parameters
  angle_hyzer_pitch_spin_from_R(&kf_meas->ang_hps_pos[0], &kf_meas->ang_hps_pos[1], &kf_meas->ang_hps_pos[2], R_GD);

  kf_meas->lin_xyz_pos[0] = T_GD(0, 0);
  kf_meas->lin_xyz_pos[1] = T_GD(1, 0);
  kf_meas->lin_xyz_pos[2] = T_GD(2, 0);

  }
  catch (...) 
  {
    std::exception_ptr p = std::current_exception();
    std::cerr <<(p ? p.__cxa_exception_type()->name() : "null") << std::endl;
    exit(1);
  }
}

static void process_filter_thread(void)
{
  uint64_t last_loop_ns = 0;
  uint64_t now;

  bool latch_tic = false;
  bool latch_toc = false;

  while(sv_kf_estimate_stage < KF_EST_STAGE_COMPLETE)
  {
    now = uptime_get_ns();

    // check whether to reset the frame skip lock-out
    // We need a better criteria for timeouts here, since we can't trust that the threads till complete in anything close to real-time
    // Check the reported frame_id from the image processing queue vs the last apriltag detect frame_id
    // frame_id_delta * dt > timeout -> trigger the timeout    
    const double apriltag_detect_loss_time_s = (double)(sv_last_meas_frame_id - sv_last_pop_meas_frame_id) / max(CLOSE_TO_ZERO, dvd_DvisEst_image_capture_get_fps());

    if(
        (apriltag_detect_loss_time_s >= KF_FILTER_TAG_DETECT_RESET_TIME_S || dvd_DvisEst_image_capture_image_capture_queue_empty()) && 
        dvd_DvisEst_estimate_get_tags_detected() &&
        sv_kf_estimate_stage < KF_EST_STAGE_PRIME
      )
    {
      // mark filter as ready for prime
      if(sv_kf_estimate_stage == KF_EST_STAGE_READY)
      {
        sv_kf_estimate_stage = KF_EST_STAGE_PRIME;
      }

      std::cerr << "Time out apriltag detects! kfstage = " << (int)sv_kf_estimate_stage << ", Time since apriltag detect(ms): " << apriltag_detect_loss_time_s*1000 << "FIDs [" << sv_last_pop_meas_frame_id << ", " << sv_last_meas_frame_id << "]" << std::endl;
    
      dvd_DvisEst_estimate_set_tags_detected(false, 0);
    }

    // if we got some cancellations, just skip them and move the read idx forward
    // for obvious reasons, don't let the read idx surpass the write!!
    if(MEAS_QUEUE_STATUS(sv_meas_queue_read_idx) == MEAS_QUEUE_STATUS_AVAILABLE && sv_meas_queue_read_idx != sv_meas_queue_write_idx)
    {
      sv_meas_queue_read_idx = get_next_meas_queue_size(sv_meas_queue_read_idx);        
    }

    // run at KF_FILTER_PRED_DT_NS intervals
    if((now - last_loop_ns) >= KF_FILTER_PRED_DT_NS)
    {
      // for now (test), just assume the measurement is consumed right away
      // consume all ready adjacent measurements    
      while(MEAS_QUEUE_STATUS(sv_meas_queue_read_idx) == MEAS_QUEUE_STATUS_POPULATED)
      {
        // start timing profile block
        if(!latch_tic)
        {
          // tic
          tic();
          latch_tic = true;
        }

        cerr << "Re-queued slot " << (int)sv_meas_queue_read_idx << ", FID: " << MEAS_QUEUE_MEAS(sv_meas_queue_read_idx).frame_id << " at " << NS_TO_MS(MEAS_QUEUE_MEAS(sv_meas_queue_read_idx).timestamp_ns) << " ms (uptime = " << NS_TO_MS(now) << " ms) ";
        cerr << "XYZ: [" << MEAS_QUEUE_MEAS(sv_meas_queue_read_idx).lin_xyz_pos[0] << ", " << MEAS_QUEUE_MEAS(sv_meas_queue_read_idx).lin_xyz_pos[1] << ", " << MEAS_QUEUE_MEAS(sv_meas_queue_read_idx).lin_xyz_pos[2] << "]" << endl;

        // move queue mes over to prime queue
        dvd_DvisEst_kf_meas_t temp;
        memcpy(&temp, &MEAS_QUEUE_MEAS(sv_meas_queue_read_idx), sizeof(dvd_DvisEst_kf_meas_t));
        meas_prime_queue.push_back(temp);

        // We may not actually consume this until the Kalman filter starts, but mark it since we've re-queued it to free up the apriltag detection thread queue
        MEAS_QUEUE_STATUS(sv_meas_queue_read_idx) = MEAS_QUEUE_STATUS_AVAILABLE;

        // Go to next entry in the queue
        if(sv_meas_queue_read_idx != sv_meas_queue_write_idx)
        {
          sv_meas_queue_read_idx = get_next_meas_queue_size(sv_meas_queue_read_idx);
        }
      }

      // perform the measurement update step
      kf_meas_update_step();

      // run the kalman filter prediction step if the filter is active
      //sv_kf_state.timestamp_ns = now;
      kf_prediction_step();

      // after the queue is empty, let's mark the estimate complete so we can test thread joining
      // and do some profiling (this is valid for test images only!)
      if((dvd_DvisEst_image_capture_image_capture_queue_empty() || sv_kf_estimate_stage > KF_EST_STAGE_PRIME) && latch_tic && !latch_toc)
      {
        toc();
        //sv_kf_estimate_complete = true;
        latch_toc = true;
      }

      last_loop_ns = now;
    }
    else
    {
      // sleep for a bit so we don't busy poll
      usleep(1000);
    }
  }
  cerr << "Estimate Thread completed." << endl;
}

//This implies that we need 'KF_IDEAL_CHECK_QUEUE_SIZE' samples after priming to have a valid estimate
#define KF_IDEAL_CHECK_QUEUE_SIZE (5)
static void kf_check_for_ideal_output_state()
{
  // Keep a queue of the last KF_IDEAL_CHECK_QUEUE_SIZE entries so we can check the velocity variance
  // (boy, we're using a lot of queues eh?)
  static deque<dvd_DvisEst_kf_state_t> kf_ideal_check_queue;
  static double min_variance_sum = 9999999;

  if(sv_kf_estimate_stage >= KF_EST_STAGE_ACTIVE)
  {
    kf_ideal_check_queue.push_back(sv_kf_state);
    // ditch old entries
    while(kf_ideal_check_queue.size() > KF_IDEAL_CHECK_QUEUE_SIZE)
    {
      kf_ideal_check_queue.pop_front();
    }

    if(kf_ideal_check_queue.size() >= KF_IDEAL_CHECK_QUEUE_SIZE)
    {
      // calculate vel mean
      double lin_xyz_vel_mean[3] = {0};
      double lin_xyz_vel_var_sum = 0;
      uint8_t xyz;
      uint8_t queue_i;
      for(xyz = 0; xyz < 3; xyz++)
      {
        for(queue_i = 0; queue_i < KF_IDEAL_CHECK_QUEUE_SIZE; queue_i++)
        {
          lin_xyz_vel_mean[xyz] += kf_ideal_check_queue[queue_i].lin_xyz[xyz].vel * (1.0 / KF_IDEAL_CHECK_QUEUE_SIZE);
        }
        // check variance
        for(queue_i = 0; queue_i < KF_IDEAL_CHECK_QUEUE_SIZE; queue_i++)
        {
          double vel_diff = (kf_ideal_check_queue[queue_i].lin_xyz[xyz].vel - lin_xyz_vel_mean[xyz]);
          lin_xyz_vel_var_sum += (vel_diff * vel_diff);
        }        
      }

      if(lin_xyz_vel_var_sum < min_variance_sum)
      {
        min_variance_sum = lin_xyz_vel_var_sum;

        // save state as minimum variance
        memcpy(&sv_kf_ideal_state, &sv_kf_state, sizeof(dvd_DvisEst_kf_state_t));

        cerr << "Set new ideal state at " << NS_TO_MS(sv_kf_state.timestamp_ns) << " ms! (" << min_variance_sum << ")" << endl;

        if(log_state)
        {
          state_out_csv_log_open();
          state_out_csv_log_write(&sv_kf_ideal_state);
          state_out_csv_log_close();
        }
      }
    }    
  }
}

// Kalman Filter Functions
#define MEAS_PRIME_QUEUE_MAX_AGE_NS           (MS_TO_NS(100)) // no older than 100ms
#define MEAS_PRIME_QUEUE_PRIME_MAX_ENTRIES    (20)
#define MEAS_PRIME_QUEUE_PRIME_COUNT          (10)
#define MEAS_PRIME_QUEUE_PRIME_MIN_VAR        (2.0) // (m/s)^2
static void kf_meas_update_step()
{
  // TODO: Queue the measurement if the filter has not yet initialized
  // Let's just keep this queue as a local static for now (bad form, but eh)
  
  // marlk where the queue priming should begin from (offset from the front (oldest) of the queue)
  static uint16_t prime_idx = 0;

  // We haven't started the kalman filter yet, keep buffering measurements
  if(sv_kf_estimate_stage < KF_EST_STAGE_ACTIVE)
  {
    // these are now static so we can save the priming values (had to change now that we can't run the KF real time)
    // TODO: Fix this refactored hack! agh, so messy
    int xyz;
           double meas_vel_xyz_mean[3] = {0};
    static double meas_vel_hps_mean[3] = {0};
    static double meas_vel_xyz[MEAS_PRIME_QUEUE_PRIME_COUNT-1][3] = {0};
           double meas_vel_hps[MEAS_PRIME_QUEUE_PRIME_COUNT-1][3] = {0};
    static double dt[MEAS_PRIME_QUEUE_PRIME_COUNT-1] = {0};
    const int queue_size = meas_prime_queue.size()-1;

    if(sv_kf_estimate_stage < KF_EST_STAGE_READY)
    {
      // There are 2 conditions used to determine whether we should prime the initial KF state:
      // 1. Have we queued more than MEAS_PRIME_QUEUE_PRIME_MAX_ENTRIES?
      // OR
      // 2. Is the velocity variance of the last MEAS_PRIME_QUEUE_PRIME_COUNT entries in the queue less than MEAS_PRIME_QUEUE_PRIME_MIN_VAR?
      // Ideally we will achieve criteria #2, but failing that, we need to start the filter one way or another
      if(meas_prime_queue.size() >= MEAS_PRIME_QUEUE_PRIME_MAX_ENTRIES && sv_kf_estimate_stage == KF_EST_STAGE_MEAS_COLLECT)
      {
        cerr << "******** Meas Queue Entry Count Target for Kalman Filter prime has been met! ********" << endl;
        prime_idx = queue_size;
        sv_kf_estimate_stage = KF_EST_STAGE_READY;
      }

      // define these out here, since they'll be used later to prime the filter
      if(meas_prime_queue.size() >= MEAS_PRIME_QUEUE_PRIME_COUNT)
      {
        // check queue variance for final MEAS_PRIME_QUEUE_PRIME_COUNT entries
        bool var_target_met = true;
        int i;
        
        // for now, only use the linear variance to determine whether to prime the queue
        // indexing is 'n from the back of the queue' (meas_prime_queue[queue_size])
        double meas_vel_xyz_var[MEAS_PRIME_QUEUE_PRIME_COUNT-1][3] = {0};
        // reset these to zero each time since they are clumsily defined outside
        meas_vel_xyz_mean[0] = meas_vel_xyz_mean[1] = meas_vel_xyz_mean[2] = 0;
        meas_vel_hps_mean[0] = meas_vel_hps_mean[1] = meas_vel_hps_mean[2] = 0;
        for(i = MEAS_PRIME_QUEUE_PRIME_COUNT-2; i >= 0; i--)
        {
          dt[i] = 
            max(
              CLOSE_TO_ZERO, 
              NS_TO_S((double)(meas_prime_queue[queue_size - i].timestamp_ns - meas_prime_queue[queue_size - i-1].timestamp_ns))
            );
            //cerr << "dt = " << meas_prime_queue[queue_size - i].timestamp_ns << " - " << meas_prime_queue[queue_size - i-1].timestamp_ns << " = " << dt << endl;
          for(xyz = 0; xyz < 3; xyz++)
          { 
            meas_vel_xyz[i][xyz] = (meas_prime_queue[queue_size - i].lin_xyz_pos[xyz] - meas_prime_queue[queue_size - i-1].lin_xyz_pos[xyz]) / dt[i];
            double ang_pos_d = (meas_prime_queue[queue_size - i].ang_hps_pos[xyz] - meas_prime_queue[queue_size - i-1].ang_hps_pos[xyz]);
            WRAP_2PI(ang_pos_d);
            meas_vel_hps[i][xyz] = ang_pos_d / dt[i];

            // try to bound these to reasonable, physics-possible limits
            //BOUND_VARIABLE(meas_vel_xyz[i][xyz], -MAX_LIN_VEL, MAX_LIN_VEL);
            //BOUND_VARIABLE(meas_vel_hps[i][xyz], -MAX_ANG_VEL, MAX_ANG_VEL);

            //cerr << "VEL i = " << i << "xyz = " << xyz << " is " << meas_vel_xyz[i][xyz] << ", dP = " << meas_prime_queue[queue_size - i].lin_xyz_pos[xyz] << " - " << meas_prime_queue[queue_size - i-1].lin_xyz_pos[xyz] << endl;
            // calculate mean
            meas_vel_xyz_mean[xyz] += meas_vel_xyz[i][xyz] * (1.0/(MEAS_PRIME_QUEUE_PRIME_COUNT));
            meas_vel_hps_mean[xyz] += meas_vel_hps[i][xyz] * (1.0/(MEAS_PRIME_QUEUE_PRIME_COUNT));
          }
        }

        // calculate resulting variance
        for(i = MEAS_PRIME_QUEUE_PRIME_COUNT-2; i >= 0; i--)
        {
          for(xyz = 0; xyz < 3; xyz++)
          {          
            double vel_diff = (meas_vel_xyz[i][xyz] - meas_vel_xyz_mean[xyz]);
            meas_vel_xyz_var[i][xyz] = vel_diff * vel_diff;

            //cerr << "var i = " << i << "xyz = " << xyz << " is " << meas_vel_xyz_var[i][xyz] << " for mean of " << meas_vel_xyz_mean[xyz] << endl;

            if(meas_vel_xyz_var[i][xyz] > MEAS_PRIME_QUEUE_PRIME_MIN_VAR)
            {
              var_target_met = false;
              break;            
            }
          }
          if(!var_target_met)
          {
            break;
          }
        }

        if(var_target_met && sv_kf_estimate_stage == KF_EST_STAGE_MEAS_COLLECT)
        {
          cerr << "******** Variance Target for Kalman Filter prime has been met! ********" << endl;
          prime_idx = queue_size;
          sv_kf_estimate_stage = KF_EST_STAGE_READY;
        }
      }
    }

    // Do we have enough measurements to start the filter?
    // Did we stop detecting apriltags?
    // Then we move on, and actually run the Kalman filter
    if(sv_kf_estimate_stage == KF_EST_STAGE_PRIME)
    {
      // We are now ready to prime the initial Kalman filter state! Nice!      
      cerr << "AprilTags stopped being detected, priming Kalman Filters with queued measurements!" << endl;

      // data can be pretty noisy, and a mean velocity may not be sufficient due to our tiny dt
      // get a linear fit for each instead
      // get time series
      double dt_series[MEAS_PRIME_QUEUE_PRIME_COUNT] = {0};
      int i;
      for(i = MEAS_PRIME_QUEUE_PRIME_COUNT-2; i >= 0; i--)
      {
        dt_series[i] = dt_series[i+1] + dt[i];
      }

      cerr << "Prime IDX = " << prime_idx << endl;

      for(xyz = 0; xyz < 3; xyz++)
      {
        // set init positions to latest meas
        sv_kf_state.lin_xyz[xyz].pos = meas_prime_queue[prime_idx].lin_xyz_pos[xyz];
        sv_kf_state.ang_hps[xyz].pos = meas_prime_queue[prime_idx].ang_hps_pos[xyz];        

        // set init vels to a linear fit since they are too noisy for a mean
        double lin_pos_series[MEAS_PRIME_QUEUE_PRIME_COUNT] = {0};
        double ang_pos_series[MEAS_PRIME_QUEUE_PRIME_COUNT] = {0};
        for(i = MEAS_PRIME_QUEUE_PRIME_COUNT-1; i >= 0; i--)
        {
          lin_pos_series[i] = meas_prime_queue[prime_idx - i].lin_xyz_pos[xyz];
          ang_pos_series[i] = meas_prime_queue[prime_idx - i].ang_hps_pos[xyz];

          //cerr << "Meas linpos and time [" <<  dt_series[i] << ", " << lin_pos_series[i] << "]" << endl;
        }
        // defaults to zero on failure
        get_linear_fit(&sv_kf_state.lin_xyz[xyz].vel, dt_series, lin_pos_series, MEAS_PRIME_QUEUE_PRIME_COUNT);
        //get_linear_fit(&sv_kf_state.ang_hps[xyz].vel, dt_series, ang_pos_series, MEAS_PRIME_QUEUE_PRIME_COUNT);
        // stick with the mean for the angular space since wrapping is a pain for fits
        sv_kf_state.ang_hps[xyz].vel = meas_vel_hps_mean[xyz];

        cerr << "xyz = " << xyz << 
            " lin vel [mean,fit] = " << meas_vel_xyz_mean[xyz] << ", " << sv_kf_state.lin_xyz[xyz].vel << "]" <<
            " ang vel [mean,fit] = " << meas_vel_hps_mean[xyz] << ", " << sv_kf_state.ang_hps[xyz].vel << endl;

        // TODO: We may want to override the initial states for linz, hyzer, and pitch rates (with zero)
        // since those are axes we expect to be moving slower

        // set initial state covariance values
        sv_kf_state.lin_xyz[xyz].var[i2x2(0,0)] = LIN_POS_VAR_INIT;
        sv_kf_state.lin_xyz[xyz].var[i2x2(0,1)] = sv_kf_state.lin_xyz[xyz].var[i2x2(1,0)] = 0; // correlation terms start at zero
        sv_kf_state.lin_xyz[xyz].var[i2x2(1,1)] = LIN_VEL_VAR_INIT;
        
        sv_kf_state.ang_hps[xyz].var[i2x2(0,0)] = ANG_POS_VAR_INIT;
        sv_kf_state.ang_hps[xyz].var[i2x2(0,1)] = sv_kf_state.ang_hps[xyz].var[i2x2(1,0)] = 0; // correlation terms start at zero
        sv_kf_state.ang_hps[xyz].var[i2x2(1,1)] = ANG_VEL_VAR_INIT;
      }

      // mark filter as active
      sv_kf_estimate_stage = KF_EST_STAGE_ACTIVE;

      // init state time to last measurement used to prime
      sv_kf_state.timestamp_ns = meas_prime_queue[prime_idx].timestamp_ns;

      // pop all measurements used to prime KF init states from prime queue
      for(i = 0; i <= prime_idx; i++)
      {
        // Log all meas used for priming, mark if filter is active or not
        if(log_meas)
        {
          meas_csv_log_write(&meas_prime_queue.front(), sv_kf_state.timestamp_ns, false);
        }
        meas_prime_queue.pop_front();
      }
      // the queue is now composed only of unprocessed measurements
    }
  }
  else
  {
    // If we have already initialized the filter (KF_EST_STAGE_ACTIVE), consume the measurement normally
    // loop to consume all measurements which 'came in' since the last check against sv_kf_state.timestamp_ns
    if(meas_prime_queue.size() > 0)
    {
      uint64_t meas_timestamp_ns = meas_prime_queue.front().timestamp_ns;
      //cerr << "Queue prime size = " << meas_prime_queue.size() << ", [st,mt] = " << NS_TO_MS(sv_kf_state.timestamp_ns) << ", " << NS_TO_MS(meas_timestamp_ns) << "]" << endl;

      while(sv_kf_state.timestamp_ns > meas_timestamp_ns && meas_prime_queue.size() > 0)
      {
        //cerr << "Consume meas [st,mt] = [" << NS_TO_MS(sv_kf_state.timestamp_ns) << ", " << NS_TO_MS(meas_timestamp_ns) << "] Queue prime size = " << meas_prime_queue.size() << endl;

        dvd_DvisEst_kf_meas_t new_meas = meas_prime_queue.front();

        meas_timestamp_ns = new_meas.timestamp_ns;

        int xyz = 0;
        double VEC2(K);
        for(xyz = 0; xyz < 3; xyz++)
        {
          // linear states
          const double lin_innovation = new_meas.lin_xyz_pos[xyz] - sv_kf_state.lin_xyz[xyz].pos;
          kf_get_kalman_gain(LIN_POS_MEAS_VAR, sv_kf_state.lin_xyz[xyz].var, K);      
          kf_apply_meas_update_state(K, lin_innovation, &sv_kf_state.lin_xyz[xyz].pos, &sv_kf_state.lin_xyz[xyz].vel);
          kf_apply_meas_update_var(K, sv_kf_state.lin_xyz[xyz].var);

          // angular states
          double ang_innovation = new_meas.ang_hps_pos[xyz] - sv_kf_state.ang_hps[xyz].pos;
          // wrap innovation for the angular space
          WRAP_2PI(ang_innovation);
          kf_get_kalman_gain(ANG_POS_MEAS_VAR, sv_kf_state.ang_hps[xyz].var, K);      
          kf_apply_meas_update_state(K, ang_innovation, &sv_kf_state.ang_hps[xyz].pos, &sv_kf_state.ang_hps[xyz].vel);
          WRAP_2PI(sv_kf_state.ang_hps[xyz].pos);
          kf_apply_meas_update_var(K, sv_kf_state.ang_hps[xyz].var);
        }

        // Log all meas, mark if filter is active or not
        if(log_meas)
        {
          meas_csv_log_write(&new_meas, sv_kf_state.timestamp_ns, true);
        }

        meas_prime_queue.pop_front();

        // Now that we have a nice shiny state, let's have a look at what we want to output.
        // We're only doing this after a measurement update since we know that out state doesn't get better than that
        kf_check_for_ideal_output_state();

      }
    }

    // no more meas? complete! (more prediction steps aren't going to help us after this)
    if(meas_prime_queue.size() <= 0)
    {
      cerr << "Empty queue at " << NS_TO_MS(sv_kf_state.timestamp_ns) << " ms " << endl;
      sv_kf_estimate_stage = KF_EST_STAGE_COMPLETE;
    }
  } 
}

static void kf_prediction_step()
{
  if(sv_kf_estimate_stage >= KF_EST_STAGE_ACTIVE)
  {
    int xyz = 0;
    for(xyz = 0; xyz < 3; xyz++)
    {
      // linear states
      kf_prediction_state(KF_FILTER_PRED_DT_S, &sv_kf_state.lin_xyz[xyz].pos, &sv_kf_state.lin_xyz[xyz].vel);
      kf_prediction_var(LIN_POS_PROC_VAR, LIN_VEL_PROC_VAR, KF_FILTER_PRED_DT_S, sv_kf_state.lin_xyz[xyz].var);

      // angular states
      kf_prediction_state(KF_FILTER_PRED_DT_S, &sv_kf_state.ang_hps[xyz].pos, &sv_kf_state.ang_hps[xyz].vel);
      WRAP_2PI(sv_kf_state.ang_hps[xyz].pos);
      kf_prediction_var(ANG_POS_PROC_VAR, ANG_VEL_PROC_VAR, KF_FILTER_PRED_DT_S, sv_kf_state.ang_hps[xyz].var);
    }

    // increment timestamp
    sv_kf_state.timestamp_ns += KF_FILTER_PRED_DT_NS;

    // report state to log after prediction step
    if(log_state)
    {
      state_csv_log_write(&sv_kf_state);
    }
  }  
}

// Tranform GroundPlane-to-Disc rotation matrix into hyzer (worldframe X), pitch (worlframe Y), and spin (discframe Z) angles
static void angle_hyzer_pitch_spin_from_R(double * hyzer_angle, double * pitch_angle, double * spin_angle, cv::Matx33d R_GD)
{
  *hyzer_angle  = asin(R_GD(1,2));
  *pitch_angle  = asin(R_GD(0,2));
  *spin_angle   = atan2(R_GD(0,1), R_GD(0,0));
}

// measurement update functions
static void kf_get_kalman_gain(const double S2dm, const double MAT2X2(P), double VEC2(K))
{
  const double denom = max((S2dm + P[i2x2(0,0)]), CLOSE_TO_ZERO);

  K[0] = P[i2x2(0,0)] / denom;
  K[1] = P[i2x2(1,0)] / denom;
}

static void kf_apply_meas_update_state(const double VEC2(K), const double innovation, double * pos_state, double * vel_state)
{
  // innovation = (dk-dmk)
  (*pos_state) += K[0] * innovation;
  (*vel_state) += K[1] * innovation;
}

static void kf_apply_meas_update_var(const double VEC2(K), double MAT2X2(P))
{
  double p11 = P[i2x2(0,0)];
  double p12 = P[i2x2(0,1)];

  P[i2x2(0,0)] += (-K[0] * p11);
  P[i2x2(0,1)] += (-K[0] * p12);
  P[i2x2(1,0)] += (-K[1] * p11);
  P[i2x2(1,1)] += (-K[1] * p12);
}

// prediction/propagation functions
// Constant velocity assumption for the prediction step suffices
static void kf_prediction_state(const double dt, double * pos_state, double * vel_state)
{
  (*pos_state) += (*vel_state) * dt;
  //(*vel_state) += 0;
}

static void kf_prediction_var(const double S2dp, const double S2vp, const double dt, double MAT2X2(P))
{
  double p12 = P[i2x2(0,1)];
  double p21 = P[i2x2(1,0)];
  double p22 = P[i2x2(1,1)];

  double dte2 = dt*dt;

  P[i2x2(0,0)] += (dte2 * p22) + (dt * (p12 + p21)) + (S2dp);
  P[i2x2(0,1)] += (dt * p22);
  P[i2x2(1,0)] += (dt * p22);
  P[i2x2(1,1)] += (S2vp);
}

// end Kalman Filter Functions

void dvd_DvisEst_estimate_process_filter(void)
{
  kf_process_filter = std::thread(process_filter_thread);
}

void dvd_DvisEst_estimate_end_filter(void)
{
  kf_process_filter.join();

  // init test logs
  if(log_meas)
  {
    meas_csv_log_close();
  }
  if(log_state)
  {
    state_csv_log_close();
  }
}

// remember that we want to load in a ground-plane, and use that to modify 
// the measurements coming from the apriltag threads!
