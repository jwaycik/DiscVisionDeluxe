//std includes
#include <vector>
#include <fstream>
#include <iostream> 
#include <sstream>
#include <string> 
#include <typeinfo>   
#include <iomanip>
#include <stdio.h>
#include <stdexcept>

//lib includes
#include <Eigen/Dense>

//DfisX includes
#include "DfisX.hpp"
#include "Daero.hpp"
#include "Dgyro.hpp"
#include "Dpropagate.hpp"
#include "Dio.hpp"

#include "disc_params.hpp"

//#define basic_console_logging   (true)
//#define verbose_console_logging (false)

// these should all be considered static functions, and should contain NO REFERECES to global variables

namespace DfisX
{
  //Simulate Throw
  //simulates the current throw to completion
  void simulate_throw(Throw_Container *throw_container, const float dt)
  {
    while (throw_container->current_disc_state.sim_state != SIM_STATE_STOPPED)
    {
      DfisX::step_simulation(throw_container, dt);
    }
  }

  //Step Simulation
  //used to simulate one 'step' of physics
  void step_simulation(Throw_Container *throw_container, const float dt)
  {
    step_Daero (throw_container, dt);
    //step_Dcollision (throw_container, dt);
    step_Dgyro (throw_container, dt);
    propagate (throw_container, dt); 

    //temporary ground collision detection
    if (throw_container->current_disc_state.disc_location[2] <= 0) 
    {
      finish_throw(throw_container, dt);
    }
      //in case of a hang
    else if (throw_container->current_disc_state.forces_state.step_count > 10000)
    {
      std::cout << "The throw simulation aborted due to reaching maximum steps (10000)\n";
      throw_container->current_disc_state.sim_state = SIM_STATE_STOPPED;
    }
  } 

  //Finish Throw
  //used calculate various throw statistics and send the saved throw to the desired output
  void finish_throw(Throw_Container *throw_container, const float dt)
  {
    ///Time aloft and distance travelled 
    double test_time_aloft = throw_container->disc_state_array.size() * dt;
    std::cout << "\n         The throw spent " << test_time_aloft << "s in the air";
    Eigen::Vector3d throw_distance_vector = throw_container->disc_state_array[0].disc_location - throw_container->disc_state_array[throw_container->disc_state_array.size()-1].disc_location;
    double throw_distance_magnitude = throw_distance_vector.norm();
    std::cout << "\n         The throw went " << throw_distance_magnitude << "m  (" << 3.28*throw_distance_magnitude << "feet)";
    Eigen::Vector3d throw_velocity_vector = throw_container->disc_state_array[0].disc_velocity;
    double throw_velocity_magnitude = throw_velocity_vector.norm();
    std::cout << "\n         The throw's max speed was " << throw_velocity_magnitude << "m/s  (" << 2.236*throw_velocity_magnitude << "mph)\n";

    std::cout << std::setprecision(3) << "The throw simulated " << throw_container->current_disc_state.forces_state.step_count << " steps before ending normally.\n";
    throw_container->current_disc_state.sim_state = SIM_STATE_STOPPED;
    /*if (dfisx_config.matlab_export)
    {
      if (basic_console_logging) std::cout << "\nSending output file to matlab for viewing...\n";   
      std::string system_call = ("matlab -nosplash -nodesktop -r \"cd('" + dfisx_config.install_path + "\\matlab\\visualizers'); dvd_DfisX_plot_disc_trajectory('" + dfisx_config.install_path + "\\dvd_DfisX\\" + dfisx_config.save_path + "'); exit\"");
      system(system_call.c_str());
    }*/
  }

  bool is_finished_simulating (Throw_Container *throw_container)
  {
    if (throw_container->current_disc_state.sim_state == SIM_STATE_STOPPED) return true;
    else                                        return false;
  }

  std::vector <Disc_State> get_disc_state_array (Throw_Container *throw_container)
  {
    return (throw_container->disc_state_array);
  }

  //New Throw
  //used to start a new simulation
  void new_throw(
    Throw_Container *throw_container, 
    const Disc_Mold_Enum disc_mold_enum,
    const Eigen::Vector3d thrown_disc_position,
    const Eigen::Vector3d thrown_disc_velocity, 
    const double thrown_disc_roll, 
    const double thrown_disc_pitch, 
    const double thrown_disc_radians_per_second, 
    const double thrown_disc_wobble)
  /*
  Takes the following inputs

  d state
      Eigen::Vector3d thrown_disc_position
      Eigen::Vector3d thrown_disc_velocity
      double thrown_disc_roll
      double thrown_disc_pitch
  d force
      double thrown_disc_radians_per_second
      double thrown_disc_wobble  
  d object
      Disc_Mold_Enum disc_mold_enum

    
  Does the following things
  */
  {  
    // init throw container
    memset(throw_container, 0, sizeof(Throw_Container));

    //convert world frame roll/pitch into and orientation vector

    const double x_component = sin (-thrown_disc_pitch) * cos (thrown_disc_roll);
    const double y_component = sin (thrown_disc_roll)   * cos (thrown_disc_pitch);
    const double z_component = cos (thrown_disc_pitch)  * cos (thrown_disc_roll);

    const Eigen::Vector3d thrown_disc_orientation = {x_component,y_component,z_component};
    const double thrown_disc_rotation = 0;

    //create the starting d state
    throw_container->current_disc_state = {thrown_disc_position,thrown_disc_velocity,thrown_disc_orientation,thrown_disc_rotation};
    throw_container->current_disc_state.sim_state = SIM_STATE_STARTED;  
    throw_container->current_disc_state.forces_state = {};
    throw_container->current_disc_state.forces_state.angular_velocity = thrown_disc_radians_per_second;
    throw_container->previous_disc_state = {};

    throw_container->disc_object = disc_object_array[disc_mold_enum];

    throw_container->disc_object.mass = 0.175;
    throw_container->disc_object.diameter = 0.25;

    throw_container->disc_object.radius = throw_container->disc_object.diameter / 2;
    throw_container->disc_object.area = 3.1415 * throw_container->disc_object.radius * throw_container->disc_object.radius;

    std::vector <Disc_State> disc_state_array = {};
    throw_container->disc_state_array.clear ();
  }

  // overloaded
  void new_throw(
    Throw_Container *throw_container, 
    const Disc_Mold_Enum disc_mold_enum,
    const Eigen::Vector3d thrown_disc_position, 
    const float thrown_disc_speed, 
    const float thrown_disc_direction, 
    const float thrown_disc_loft, 
    const float thrown_disc_roll,
    const float thrown_disc_pitch,
    const float thrown_disc_spin_percent, 
    const float thrown_disc_wobble)
  {
    //Disc_Mold_Enum disc_mold_enum
    //Eigen::Vector3d thrown_disc_position
    //float thrown_disc_speed
    const float thrown_disc_direction_rad = thrown_disc_direction / 57.3;
    const float thrown_disc_loft_rad = thrown_disc_loft / 57.3;
    const float thrown_disc_pitch_rad = thrown_disc_pitch / 57.3;
    const float thrown_disc_roll_rad = thrown_disc_roll / 57.3;
    //float thrown_disc_nose_up_rad = thrown_disc_nose_up / 57.3;
    const float thrown_disc_spin_percent_dec = thrown_disc_spin_percent / 100;
    //float thrown_disc_wobble

    const float thrown_disc_velocity_x = thrown_disc_speed * cos(thrown_disc_loft_rad) * cos(thrown_disc_direction_rad);
    const float thrown_disc_velocity_y = thrown_disc_speed * cos(thrown_disc_loft_rad) * sin(thrown_disc_direction_rad);
    const float thrown_disc_velocity_z = thrown_disc_speed * sin(thrown_disc_loft_rad);
    const Eigen::Vector3d thrown_disc_velocity = Eigen::Vector3d (thrown_disc_velocity_x,thrown_disc_velocity_y,thrown_disc_velocity_z);

    //assuming diameter of 212mm
    const float thrown_disc_rotations_per_second = thrown_disc_speed / (0.212 * 3.1415) * thrown_disc_spin_percent_dec;
    const float thrown_disc_radians_per_second = thrown_disc_rotations_per_second * -2 * 3.1415;

    new_throw(
      throw_container,
      disc_mold_enum,
      thrown_disc_position,
      thrown_disc_velocity,
      thrown_disc_roll_rad,
      thrown_disc_pitch_rad,
      thrown_disc_radians_per_second,
      thrown_disc_wobble);
  }

  //Various Getters and Setters
  Disc_State get_disc_state(Throw_Container *throw_container)
  {
    return throw_container->current_disc_state;
  }

  void activate_matlab_export() 
  {
    //dfisx_config.matlab_export = true;
  }
}
