#include "DfisX.hpp"
#include "Dio.hpp"
#include "dvd_maths.hpp"
#include "disc_layouts.hpp"
#include <iostream> 
#include <string> 
#include <sstream>


/*
      Dio
      to handle all lower level io for DfisX


" Write me a file, youre a writer
  Do me a wrong, youre a bringer of errors "
*/


void parse_cl (int argc, char *argv[]) 
{

	/*
    Default Values for command line        
    */
  bool run_test = 0;
  DiscIndex disc_index = DiscIndex::NONE; 

  double posx = 0;
  double posy = 0;
  double posz = 1.5;
  double velx = 0;
  double vely = 0;
  double velz = 0;

  double windx = 0;
  double windy = 0;
  double windz = 0;

  double thrown_disc_roll = 0;
  double thrown_disc_pitch = 0;
  double thrown_disc_radians_per_second = 0;
  double thrown_disc_wobble = 0;

  Eigen::Vector3d thrown_disc_position;
  Eigen::Vector3d thrown_disc_velocity;

  std::string save_path;
    

  for (int count{ 2 }; count < argc; count += 2)
  {

    std::string arg_value = argv[count-1];
    double arg_value_value =  strtod(argv[count], NULL);

    if      (arg_value == "test" && arg_value_value == 1.0 )     
    {
      run_test = 1;  
    }
    //else if (arg_value == "matlab")    DfisX::activate_matlab_export ();
    else if (arg_value == "hyzer")     thrown_disc_roll                 = arg_value_value;
    else if (arg_value == "pitch")     thrown_disc_pitch                = arg_value_value;
    else if (arg_value == "posx")      posx                             = arg_value_value;
    else if (arg_value == "posy")      posy                             = arg_value_value;
    else if (arg_value == "posz")      posz                             = arg_value_value;
    else if (arg_value == "velx")      velx                             = arg_value_value;
    else if (arg_value == "vely")      vely                             = arg_value_value;
    else if (arg_value == "velz")      velz                             = arg_value_value;
    else if (arg_value == "windx")     windx                            = arg_value_value;
    else if (arg_value == "windy")     windy                            = arg_value_value;
    else if (arg_value == "windz")     windz                            = arg_value_value;
    else if (arg_value == "wobble")    thrown_disc_wobble               = arg_value_value;
    else if (arg_value == "spinrate")  thrown_disc_radians_per_second   = arg_value_value;
    else if (arg_value == "discmold")  disc_index                       = static_cast<DiscIndex>(arg_value_value);
    else if (arg_value == "savepath")   
    {
                      save_path             = argv[count+1];
                      //DfisX::set_save_path ("flight_saves\\"+save_path);
                      count += 2;
    }
    
    //else if (arg_value == "savepath")     thrown_disc_roll         = arg_value_value;



    else                 std::cout << "\n arg value " << arg_value << " was unable to be passed with the value of " << arg_value_value << "\n";
    
  }
  
  if (run_test) 
  {
  std::cout << "\nSimulating using test function";
  thrown_disc_position = Eigen::Vector3d(posx,posy,posz);
  thrown_disc_velocity = Eigen::Vector3d(velx,vely,velz);
  //DfisX::test(disc_mold_enum,thrown_disc_position,thrown_disc_velocity,thrown_disc_roll,thrown_disc_pitch,thrown_disc_radians_per_second,thrown_disc_wobble);
  }

  if (!run_test &&  thrown_disc_radians_per_second  && (velx || vely) )
  {
    std::cout << "\nCreating throw from command line args";
    thrown_disc_position = Eigen::Vector3d(posx,posy,posz);
    thrown_disc_velocity = Eigen::Vector3d(velx,vely,velz);

    DfisX::Throw_Container throw_container;

    // init disc env and disc model
    throw_container.disc_environment.wind_vector_xyz = Eigen::Vector3d(windx,windy,windz);
    throw_container.disc_environment.gust_factor = DfisX::Gust_Factor::ZERO_DEAD_DIDDLY;
    throw_container.disc_environment.air_density = ISA_RHO;

    const float dt = (1.0/100.0); // run at 100Hz for test

    DfisX::new_throw(&throw_container, disc_index,thrown_disc_position,thrown_disc_velocity,thrown_disc_roll,thrown_disc_pitch,thrown_disc_radians_per_second,thrown_disc_wobble);
    DfisX::simulate_throw(&throw_container, dt);

    std::cout << "Successfully passed a command line throw \n";
  }


  else if  ((thrown_disc_radians_per_second ||  velx || vely) && !run_test )
  {
    std::cout << "\nERROR: Some values of a new throw were passed as command line args, but not enough to instantiate a throw \n ";  
  } 
   
    
}
