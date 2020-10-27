#pragma once
#include <Eigen/Dense>
#include <vector>

#define d_statistics    throw_container->disc_statistics
#define d_velocity      throw_container->current_disc_state.disc_velocity
#define d_location      throw_container->current_disc_state.disc_location
#define d_orientation   throw_container->current_disc_state.disc_orientation
#define d_state         throw_container->current_disc_state
#define d_array         throw_container->disc_state_array
#define p_state         throw_container->previous_disc_state
#define d_forces        throw_container->current_disc_state.forces_state
#define d_object        throw_container->disc_object

//Used for quick radian to degree conversions
#define RAD_360   6.28318531
#define RAD_180   3.14159265
#define RAD_90    1.57079633

//air density
#define RHO       1.225
//disc aerodynamic constant
#define PI_X_AR   3.99

namespace DfisX
{
  enum Disc_Mold_Enum
  {
    NONE,
    GROUNDPLANE,
    GROUNDPLANE_BIG,
    PUTTER,
    PUTTER_OS,
    PUTTER_US,
    MIDRANGE,
    MIDRANGE_OS,
    MIDRANGE_US,
    FAIRWAY,
    FAIRWAY_OS,
    FAIRWAY_US,
    DRIVER,
    DRIVER_OS,
    DRIVER_US,
    SPECIAL
    
  };


  //SIMULATION STATE
  //this enum roughly describes the state of flight a disc is in
  enum Sim_State
  {
    SIM_STATE_STOPPED,
    SIM_STATE_STARTED,
    SIM_STATE_FLYING_HIGH_SPEED_TURN,
    SIM_STATE_FLYING_TURN,
    SIM_STATE_FLYING,
    SIM_STATE_FLYING_FADE,
    SIM_STATE_SKIPPING,
    SIM_STATE_TREE_HIT,
    SIM_STATE_ROLLING,
    SIM_STATE_SLIDING
    
  };


  // Forces State
  //Aerodynamic simulation step variables
  //  used to calculate the forces/accelerations on a disc in flight
  struct Forces_State
  {
      Eigen::Vector3d current_wind;    //in meters per second as a vector

      Eigen::Vector3d force_sum;
      double acceleration_sum;
      double angular_velocity;   //radians_per_second                                                               

      Eigen::Vector3d aero_force;
      double aero_torque;

      Eigen::Vector3d collision_force;
      Eigen::Vector3d collision_location;
      //Eigen::Vector3d collision_angle;
      double collision_torque;

      int step_count;

      /////////////aero holders

      Eigen::Vector3d disc_x_unit_vector;
      Eigen::Vector3d disc_y_unit_vector;
      Eigen::Vector3d disc_lift_unit_vector;
      Eigen::Vector3d disc_velocity_unit_vector;
      Eigen::Vector3d lift_force_vector;
      Eigen::Vector3d drag_force_vector;

      double aoar;
      double velocity_magnitude;
      double v2;
      double pav2by2;

      double coefficient_curve;
      double stall_curve;

      double lift_force_magnitude;
      double drag_force_magnitude;

      double realized_lift_coefficient;
      double realized_drag_coefficient;
      double induced_drag_coefficient;
      double realized_pitching_moment_coefficient;
      double stall_induced_drag;

      double lift_induced_pitching_moment;  //needs to go into struct
  };



//Describes the state of a disc at one point in time
//
//   everything needed for display purposes and then some
struct Disc_State
{
  Eigen::Vector3d disc_location;    //in meters
  Eigen::Vector3d disc_velocity;    //in meters per second
  Eigen::Vector3d disc_orientation; //orientation vector is the normal of the plane inscribed by the disc
  double disc_rotation;             //in radians
  Sim_State sim_state;              //describes the current state of the disc
  Forces_State forces_state;        //describes the aerodynamic state of a disc in flight including forces,torques,moments, and coefficients  
};



//Holds variables need through all DfisX related files that are set during runtime
struct DfisX_Config
{
  std::string save_path;
  Eigen::Vector3d global_wind_vel;    //in meters per second as a vector
  bool matlab_export;                 //used to export saved results to a matlab visualizer
  std::string install_path;
};



//Disc Model
//contains the name and the physics/aerodynamic properties of a disc mold
struct Disc_Model
{
  std::string mold_name;
  float lift_coefficient_base;
  float lift_coefficient_per_radian;
  float drag_coefficient;
  float pitching_moment_base;

  float pitching_moment_per_radian;
  float mass;
  float diameter;
  float radius;
  float area;
};
//example....const Disc_Model disc_aviar {"bigz buzzz",0.150000,0.599500,0.050000,-0.015000,0.120000,0.175000,0.217000};



//Disc Statistics
//used to track various stats about a discs flight, such as time aloft and distance travelled
struct Disc_Statistics
{
  Eigen::Vector3d disc_start_location;
  Eigen::Vector3d disc_finish_location;
  Eigen::Vector3d disc_landed_location;
  double disc_cumulative_roll;
};



//Throw Container
//contains all of the info needed to simulate and display a disc
struct Throw_Container
{
  Disc_State current_disc_state;              //current state of disc
  Disc_State previous_disc_state;             //state of disc last step
  Disc_Model disc_object;                    //aero properties of disc being simulated
  Disc_Statistics disc_statistics;            //used to track stats of a flight
  std::vector <Disc_State> disc_state_array;  //array of disc states: used to hold the flight data of a simulated throw
};



bool                is_finished_simulating(Throw_Container *throw_container);

std::vector <Disc_State> get_disc_state_array(Throw_Container *throw_container);

//Simulates the current throw to completion
void                simulate_throw(Throw_Container *throw_container, const float dt);

//Steps the aero simulation forward one step
void                step_simulation(Throw_Container *throw_container, const float dt);

//Instantiates a new throw, ready to be stepped or simulated
void                new_throw(
  Throw_Container *throw_container, 
  const Disc_Mold_Enum disc_mold_enum,
  const Eigen::Vector3d thrown_disc_position,
  const Eigen::Vector3d thrown_disc_velocity, 
  const double thrown_disc_roll, 
  const double thrown_disc_pitch, 
  const double thrown_disc_radians_per_second, 
  const double thrown_disc_wobble);

void                new_throw(
  Throw_Container *throw_container, 
  const Disc_Mold_Enum disc_mold_enum,
  const Eigen::Vector3d thrown_disc_position, 
  const float thrown_disc_speed, 
  const float thrown_disc_direction, 
  const float thrown_disc_loft, 
  const float thrown_disc_roll,
  const float thrown_disc_pitch,
  const float thrown_disc_spin_percent, 
  const float thrown_disc_wobble);

//Finishes a throw
//This includes things such as saving/serializing the throw and performing various stat calculations
void                finish_throw        (Throw_Container *throw_container, const float dt);

void                activate_matlab_export ();




}