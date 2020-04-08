#include "DfisX.hpp"
#include <iostream> 
#include <string> 
#include <sstream>

DfisX::Disc_Mold_Enum disc_mold_enum;
double command_line_arg_throw = 1; 

double posx = 0;
double posy = 0;
double posz = 1.5;
double velx = 0;
double vely = 0;
double velz = 0;

double thrown_disc_roll = 0;
double thrown_disc_pitch = 0;
double thrown_disc_radians_per_second;
double thrown_disc_wobble = 0;

Eigen::Vector3d thrown_disc_position;
Eigen::Vector3d thrown_disc_velocity;

std::string save_path;

int main(int argc, char *argv[]) 
{
    

	DfisX::init();
	//DfisX::global_variables.save_path = "\\flight_saves\\saved_throw2.csv";
	 //std::cout << "It is being saved to the file " << DfisX::global_variables;

	for (int count{ 2 }; count < argc; count += 2)
	{

		std::string arg_value = argv[count-1];
		double arg_value_value =  strtod(argv[count], NULL);

		if  	(arg_value == "test" && arg_value_value == 1.0 ) 		
		{
			DfisX::test();
			command_line_arg_throw = 0;  //to make sure the command line arg throw wont pass 
		}
		else if (arg_value == "hyzer") 		thrown_disc_roll 				= arg_value_value;
		else if (arg_value == "pitch") 		thrown_disc_pitch 				= arg_value_value;
		else if (arg_value == "posx") 		posx 							= arg_value_value;
		else if (arg_value == "posy") 		posy 							= arg_value_value;
		else if (arg_value == "posz") 		posz 							= arg_value_value;
		else if (arg_value == "velx") 		velx 							= arg_value_value;
		else if (arg_value == "vely") 		vely 							= arg_value_value;
		else if (arg_value == "velz") 		velz 							= arg_value_value;
		else if (arg_value == "wobble") 	thrown_disc_wobble 				= arg_value_value;
		else if (arg_value == "spinrate") 	thrown_disc_radians_per_second 	= arg_value_value;
		else if (arg_value == "discmold") 	disc_mold_enum 					= static_cast<DfisX::Disc_Mold_Enum>(arg_value_value);
		else if (arg_value == "savepath") 	
		{
											save_path 						= argv[count+1];
											DfisX::set_save_path ("flight_saves\\"+save_path);
											count += 2;
		}
		
		//else if (arg_value == "savepath") 		thrown_disc_roll 				= arg_value_value;



		else 								std::cout << "\n arg value " << arg_value << " was unable to be passed with the value of " << arg_value_value << "\n";
		
	}
	

	if (command_line_arg_throw &&  thrown_disc_radians_per_second  && velx || vely )
	{
	std::cout << "\n creating throw from command line args \n";
	thrown_disc_position = Eigen::Vector3d(posx,posy,posz);
	thrown_disc_velocity = Eigen::Vector3d(velx,vely,velz);
	DfisX::new_throw (disc_mold_enum,thrown_disc_position,thrown_disc_velocity,thrown_disc_roll,thrown_disc_pitch,thrown_disc_radians_per_second,thrown_disc_wobble);
	DfisX::simulate_throw();
	std::cout << "successfully passed a command line throw \n";
	}
 	else if	(thrown_disc_radians_per_second ||  velx || vely )
 	{
 	std::cout << "\nERROR: Some values of a new throw were passed as command line args, but not enough to instantiate a throw \n ";	
 	} 
 	
    
}