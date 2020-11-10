#include "DfisX.hpp"
#include "Daero.hpp"
#include "dvd_maths.hpp"

#include <iostream> 
#include <math.h>
/*
||||||||||||Daero|||||||||||||||||
Handles the aerodynamic forces of disc simulation.
Also gravity.
*/

// Model Constants for all discs
// putching moment arms as a percentage of total diameter
#define PITCHING_MOMENT_FORM_DRAG_PLATE_OFFSET (0.02) // % of diameter toward the front of the disc for plate drag force centre
#define PITCHING_MOMENT_CAVITY_LIFT_OFFSET     (0.05) // % of diameter toward the back of the disc for cavity lift force centre
#define PITCHING_MOMENT_CAMBER_LIFT_OFFSET     (0.23) // % of diameter toward the front of the disc for camber lift force centre
#define RIM_CAMBER_EXPOSURE (0.75) // % of lower rim camber exposed to the airflow vs a rim_width * diameter rectangle


namespace DfisX
{
  ///for display purposes     see Sim_State
  // determines the division between
  // SIM_STATE_FLYING_TURN  -  TURN_CONST   -   SIM_STATE_FLYING   -  FADE_CONST  -  SIM_STATE_FLYING_FADE
  const double HS_TURN_CONST = -0.05;
  const double TURN_CONST =  0.05;
  const double FADE_CONST =  0.15;

  void             make_unit_vector         (Eigen::Vector3d &vector_to_unitize)
  {
      vector_to_unitize /= vector_to_unitize.norm();
  }

  Eigen::Vector3d  get_unit_vector          (Eigen::Vector3d vector_to_unitize)
  {
      return vector_to_unitize /= vector_to_unitize.norm();
  }

  double           angle_between_vectors    (Eigen::Vector3d a, Eigen::Vector3d b) 
  {
      double angle = 0.0;
      angle = std::atan2(a.cross(b).norm(), a.dot(b));
      return angle;
  }


  // GUST TEST
  // Gaussian Noise Generator
  // generate numbers with mean 0 and standard deviation 1. 
  // (To adjust to some other distribution, multiply by the standard deviation and add the mean.)
  // ~0.65s for 10000000 rands on X86
  float gaussrand()
  {
    // not sure if these are a problem for multi-throw, don't think so
    static float V1, V2, S;
    static int phase = 0;
    float X;

    if(phase == 0) {
      do {
        float U1 = (float)rand() / (float)RAND_MAX;
        float U2 = (float)rand() / (float)RAND_MAX;

        V1 = 2 * U1 - 1;
        V2 = 2 * U2 - 1;
        S = V1 * V1 + V2 * V2;
        } while(S >= 1 || S == 0);

      X = V1 * sqrt(-2 * log(S) / S);
    } else
      X = V2 * sqrt(-2 * log(S) / S);

    phase = 1 - phase;

    return X;
  }


  void Daero_compute_gusts(Throw_Container *throw_container)
  {
    // apply static var filters (this will need to change for multidisc)
    // approximate a 1st order butterworth filrter with 0.2Hz cutoff, and 200Hz sampling
    const float N = 1.0 / (0.2/(200/2)) * 0.3;
    // do it with filtered white noise instead
    // we'll do some noise with a standard deviation of 0.3
    // and then bound it to +-1.0
    const double gust_stddev = 0.3;
    const double scaling_factor = N/10.0;

    double raw_gust_noise[3] = 
    {
      gaussrand() * gust_stddev,
      gaussrand() * gust_stddev,
      gaussrand() * gust_stddev * 0.25
    };

    // Bound to +-1.0 absolute
    BOUND_VARIABLE(raw_gust_noise[0], -1.0, 1.0);
    BOUND_VARIABLE(raw_gust_noise[1], -1.0, 1.0);
    BOUND_VARIABLE(raw_gust_noise[2], -1.0, 1.0);

    // Amplify based on gust enum directly for now
    double gust_amplitude = ((double)(throw_container->disc_environment).gust_factor);
    gust_amplitude *= gust_amplitude; // square it

    raw_gust_noise[0] *= gust_amplitude;
    raw_gust_noise[1] *= gust_amplitude;
    raw_gust_noise[2] *= gust_amplitude;

    LP_FILT(d_forces.gust_vector_xyz[0], raw_gust_noise[0], N);
    LP_FILT(d_forces.gust_vector_xyz[1], raw_gust_noise[1], N);
    LP_FILT(d_forces.gust_vector_xyz[2], raw_gust_noise[2], N);
  }

  //main file function
  //this takes a throw container reference and a step time in seconds and performs the aerdynamic force and torque calculations
  //step_daero saves these calculations into the throw container
  void step_Daero(Throw_Container *throw_container, const float dt)
  {
    /* ripped from dfisx.py, naming scheme isnt accurate yet
    #####Unit Vectors
    #vel_unit:  unit vector of total disc velocity
    #lift_unit: unit vector 90 degrees from vel_unit in line with discs 'up' vector
    
    #disc_normal_unit:  unit vector of disc which points 'up' relative to the disc
    #disc_unit_y:  unit vector of velocity vector projected onto discs planes (angle between this and vel_unit is angle of attack)
    #disc_unit_x:  unit vector of disc which points perpendicular to direction of travel but lays on the discs plane
    
    #rotation direction goes from disc_unit_x to disc_unit_y
    """
    */


    //// new and old model flags (for now)
    // this is the parasitic 'skin' drag produced as the disc rotates in plane about the disc normal Z axis
    const bool use_updated_rotational_drag_model = true;

    // this is the form drag generated by the disc rotating about the 'x' axis (orthogonal to the disc normal)
    // think "paddle boat turbine"
    const bool use_pitching_drag_model            = true;

    // this is a simplified form drag model for linear airflow across the disc
    // it is a simple combination of:
    // 'Form Drag from exposed disc edge'
    // and
    // 'Form Drag from exposed disc plate'
    // with a simple sinusoid for effective exposed area to the airflow
    const bool use_updated_form_drag_model        = true;
    const bool use_updated_lift_model             = true;

    // Baed on observations from the paper
    // 1. plate form drag is not at the disc centre
    // 2. lift for bernoulli effect due to camber is not at the disc centre
    // 3. lift for bernoulli effect due to cavity is not at the disc centre
    const bool use_updated_pitching_moment_model  = true;

    // add LP-filtered white-noise gusts
    Daero_compute_gusts(throw_container);

    Eigen::Vector3d disc_air_velocity_vector = d_velocity - throw_container->disc_environment.wind_vector_xyz - d_forces.gust_vector_xyz;

    d_forces.disc_velocity_unit_vector = get_unit_vector(disc_air_velocity_vector);
    make_unit_vector(d_orientation);


  ////////////////////////////////////////////////////////////Div//By//Zero//Protection///////////////////////////////////////////////////////////////
    //division by zero protection......maybe not necessary
    if (!d_orientation.isApprox(d_forces.disc_velocity_unit_vector))
    {
      d_forces.disc_x_unit_vector = d_forces.disc_velocity_unit_vector.cross(d_orientation);
      make_unit_vector (d_forces.disc_x_unit_vector);
      d_forces.disc_y_unit_vector = d_forces.disc_x_unit_vector.cross(d_orientation);
      make_unit_vector (d_forces.disc_y_unit_vector);

      d_forces.disc_lift_unit_vector = d_forces.disc_x_unit_vector.cross (d_forces.disc_velocity_unit_vector);
    }
    ///divide by zero case (disc is travelling perdicularily through the air)
    else
    {
      d_forces.disc_x_unit_vector =    Eigen::Vector3d    (0,0,0);
      d_forces.disc_y_unit_vector =    Eigen::Vector3d    (0,0,0);
      d_forces.disc_lift_unit_vector = Eigen::Vector3d    (0,0,0);
    //if (basic_console_logging) std::cout << "This would produce an error if there was no divide by zero protection in Daero unit vector creation process!!!!!!!!!!!!!!!!!";
    }
  ////////////////////////////////////////////////////////////Div//By//Zero//Protection/////////////////////////////////////////////////////////


    //////////////////////////////////////////Multiuse Variables/////////////////////////////////
    //#AoAr angle of attack (radians)
    //aoar = vel_unit.angle(disc_normal_unit)-np.deg2rad(90)
    d_forces.aoar = angle_between_vectors (d_forces.disc_velocity_unit_vector, d_orientation) - M_PI_2;
    
    //#velocity squared
    //V2 = (vel.magnitude()) ** 2
    d_forces.velocity_magnitude = disc_air_velocity_vector.norm();
    d_forces.v2 =                 d_forces.velocity_magnitude * d_forces.velocity_magnitude;

    //#0.5 * pressure * area * velocity^2
    //pav2by2 = p * a * V2 / 2
    d_forces.pav2by2 = throw_container->disc_environment.air_density * d_object.area * d_forces.v2 * 0.5;
    //////////////////////////////////////////Multiuse Variables/////////////////////////////////


    //if(!use_updated_pitching_moment_model)
    //{
      /////////////Calculating the realized flight coefficients////////////////////////////////////
      //normal flight conditions
      if (d_forces.aoar > -0.52 && d_forces.aoar > -0.52)
      {
        d_forces.coefficient_curve =  0.5 * std::sin(6*d_forces.aoar) + std::sin(2*d_forces.aoar);
        d_forces.realized_lift_coefficient =            d_object.lift_coefficient_base +     d_object.lift_coefficient_per_radian * d_forces.coefficient_curve;
        d_forces.realized_pitching_moment_coefficient = d_object.pitching_moment_base  +     d_object.pitching_moment_per_radian  * d_forces.coefficient_curve;
        d_forces.stall_induced_drag = 0.0;

      }
      //stall conditions
      else
      {
        d_forces.stall_curve =         std::sin(2*d_forces.aoar);
        d_forces.realized_lift_coefficient =            d_object.lift_coefficient_base + d_object.lift_coefficient_per_radian * d_forces.stall_curve;
        d_forces.realized_pitching_moment_coefficient = d_object.pitching_moment_base  + d_object.pitching_moment_per_radian  * d_forces.stall_curve;
        d_forces.stall_induced_drag = -std::cos(2*d_forces.aoar)+0.55;
      }
      /////////////Calculating the realized flight coefficients////////////////////////////////////
    //}

    /////////////Sim_State calculations for display purposes////////////////////////////////////////
    if      (d_forces.aoar < HS_TURN_CONST)   d_state.sim_state = SIM_STATE_FLYING_HIGH_SPEED_TURN;
    else if (d_forces.aoar < TURN_CONST)      d_state.sim_state = SIM_STATE_FLYING_TURN;
    else if (d_forces.aoar < FADE_CONST)      d_state.sim_state = SIM_STATE_FLYING;
    else                                      d_state.sim_state = SIM_STATE_FLYING_FADE;
  /*
        if      (d_forces.aoar < 0)                                                d_state.sim_state = SIM_STATE_FLYING_HIGH_SPEED_TURN;
      else if (d_forces.realized_pitching_moment_coefficient <= TURN_CONST)      d_state.sim_state = SIM_STATE_FLYING_TURN;
      else if (d_forces.realized_pitching_moment_coefficient >  TURN_CONST && 
               d_forces.realized_pitching_moment_coefficient <  FADE_CONST)      d_state.sim_state = SIM_STATE_FLYING;
      else if (d_forces.realized_pitching_moment_coefficient >= FADE_CONST)      d_state.sim_state = SIM_STATE_FLYING_FADE;
      */
      /////////////Sim_State calculations for display purposes////////////////////////////////////////

  /*
  induced drag Cdi = Cl**2 / pi AR

  AR = 1.27 for a circular disc
  pi * AR = PI_X_AR = 3.99
   */

    // parasidic drag torque calculations:
    
    // Inertia of a thin disc:
    // Iz =      1/2 * m * r^2
    // Ix = Iy = 1/4 * m * r^2

    // torque = accel * I
    const float r2 = (d_object.radius * d_object.radius);
    const float r5 = (d_object.radius * d_object.radius * d_object.radius * d_object.radius * d_object.radius);
    if(use_updated_rotational_drag_model)
    {
      // rotational Reynolds number = Re = omega * r^2 / linear_v
      // where (I think) linear_v is along the rotational plane (not sure)
      // for now, we can just take the total lin vel magnitude...
      Eigen::Vector3d v = disc_air_velocity_vector;
      const float airspeed_vel_mag = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);

      // (I think?) The rotational reynolds number is a function only of the linear aispeed
      // across the disc surface (orthogonal to the disc normal)
      // So we can compute the dot product between the disc normal unit vector, and the airspeed vector
      // to compute the effective angle between the airspeed vector and the disc normal
      // then, we can use this angle to attenuate the magnitude of the airspeed vector to produce 
      // the magnitude of airflow in the disc plane only
      const float ang_disc_normal_to_airspeed = angle_between_vectors(v, d_state.disc_orientation);
      const float airspeed_vel_mag_disc_plane = sin(ang_disc_normal_to_airspeed) * airspeed_vel_mag;
      const float Re_rot = (std::fabs(d_state.disc_rotation_vel) * r2) / MAX(airspeed_vel_mag_disc_plane, CLOSE_TO_ZERO);

      // https://www.sciencedirect.com/topics/engineering/rotating-disc
      // approximate surrounded by laminar   Cm = 3.87Re^(-1/2)
      // approximate surrounded by turbulent Cm = 0.146Re^(-1/5)
      // NO IDEA, let's just tune this with the laminar formula
      float Cm = 0.0;
      if(airspeed_vel_mag_disc_plane > CLOSE_TO_ZERO)
      {
        const float Cm_base = 0.08;
        Cm = Cm_base * (1.0 / MAX(std::sqrt(Re_rot), CLOSE_TO_ZERO));
      }

      // parasidic drag torque = Tq = 0.5 * rho * omega^2 * r^5 * Cm
      // where omega is the angular vel in m/s
      // and 'r' is the radius in m
      d_forces.aero_torque_z =
        -signum(d_state.disc_rotation_vel) *
        0.5 * 
        throw_container->disc_environment.air_density * 
        (d_state.disc_rotation_vel * d_state.disc_rotation_vel) * 
        r5 * 
        Cm;
    }
    else
    {
      const double hacky_spin_drag_rate = 5.0;
      const float Iz = 0.5 * d_object.mass * r2;
      d_forces.aero_torque_z = -signum(d_state.disc_rotation_vel) * hacky_spin_drag_rate * Iz;
    }

    //std::cout << std::to_string(d_forces.step_count) << ": RotParaDrag Torque = " << std::to_string(d_forces.aero_torque_z) << 
    //  " Nm, SPIN = " << std::to_string(d_state.disc_rotation_vel) << "rad/s" << std::endl;

    d_forces.aero_torque_x = 0;
    d_forces.aero_torque_y = 0;
    if(use_pitching_drag_model)
    {
      // from 'drag of rotating disc pitching'
      // k = 0.13412
      // Td = 2.0 * k * Cwdxy * r^5 * rho * w^2

      // https://www.engineeringtoolbox.com/drag-coefficient-d_627.html
      const double Cwdxy = 1.17;

      const double Td_x = 
        -signum(d_state.disc_pitching_vel) *
        2.0 * 0.13412 *
        Cwdxy *
        r5 *
        throw_container->disc_environment.air_density *
        (d_state.disc_pitching_vel*d_state.disc_pitching_vel);

      d_forces.aero_torque_x = Td_x;

      const double Td_y = 
        -signum(d_state.disc_rolling_vel) *
        2.0 * 0.13412 *
        Cwdxy *
        r5 *
        throw_container->disc_environment.air_density *
        (d_state.disc_rolling_vel*d_state.disc_rolling_vel);

      d_forces.aero_torque_y = Td_y;

      //std::cout << std::to_string(d_forces.step_count) << ": Rot Drag XY Torque = " << out.str() << " Nm vs gyro induced torque = " << std::to_string(d_forces.gyro_torque_x) << std::endl;
    }

    // effective edge heighr for our simplified 'edge' and 'plate' model approximation
    const float edge_height = 0.006;
    if(use_updated_form_drag_model)
    {
      // https://www.engineeringtoolbox.com/drag-coefficient-d_627.html
      const double Cd_plate = 1.17;
      const double Cd_edge  = 1.1;
      const double A_plate  = r2 * M_PI;
      const double A_edge   = d_object.radius * 2 * edge_height; // say effectively a 6mm tall rrectangle edge for now

      const double rhov2o2  = throw_container->disc_environment.air_density * d_forces.v2 * 0.5;

      // Disc 'Form' Drag
      // what we need here is the projection of the airspeed induced force against the surfaces of the disc
      // we are considering two surfaces right now:
      // 1. disc edge, this will be minimal on drivers, but not on putters. This effective area should later be a disc param
      // 2. disc plate, this is the flat surface on the top or bottom of the disc. The Cd*A is probably higher on the bottom due to the lips,
      // but we'll consider it symmetric for the moment

      // For the EDGE:
      // The incident force of the air on the edge will be felt along the plane of the disc.
      // e.g. a nose-down disc with a horz airflow will produce lift, and get pushed backward along the air force vector
      // and a nose-up disc with a horz airflow will produce negative lift, and get pushed backward along the air force vector
      // ^                                /\<------ V
      //  \                              / /
      //  \\\                           / /
      //   \ \                         ///   
      //    \ \                        /
      //     \/<----- V               v

      // For the PLATE:
      // The incident force of the air on the plate should always project (positively or negatively) along 
      // the disc nomrla plane. You can think of this like a normal force for an applied force on the ground
      // (it is always orthogonal to the incident surface)

      // The magnitude of these applied forces is a function of how much of this surface is incident with the airflow.
      // So if the disc is completely flat in a horizontal airflow, there is NO PLATE DRAG FORCE
      // and similarly, a flat disc in vertical free-fall would have NO EDGE DRAG FORCE
      // this is only considering form drag, and NOT parasitic surface drag

      const double Fd_edge  = rhov2o2 * Cd_edge  * A_edge  * cos(d_forces.aoar);
      const double Fd_plate = rhov2o2 * Cd_plate * A_plate * sin(d_forces.aoar);

      // Parasitic Skin Drag
      // This is not included in the two components of form drag listed above, but will change with AOA
      // It is akin to 'air friction', and is addressed for the rotational case in 'aero_torque_z' above
      // For the linear airflow interaction, 
      // we assume that maximum skin drag occurs when the most surface area is exposed to an airflow
      // this is very probably when the disc is flat
      // The effective area used for this drag term may be related to the boundary layer, and how
      // long laminar airflows stay attached to the disc.
      // For now, we'll just approximate it as a fixed valuefor any AOA (to be revisited later if required)
      // Skin drag is considered to be in the direction of the airflow vector
      const double Cd_skin = 0.01;
      const double Fd_skin  = rhov2o2 * Cd_skin  * (A_edge + A_plate);


      // now we can determine the unit vector to apply the edge drag force in
      // get orth vector to airspeed and disc norm
      // then use this vector to get the projected edge force vector along the disc plane
      Eigen::Vector3d edge_force_vector = d_orientation.cross(d_orientation.cross(d_forces.disc_velocity_unit_vector));
      make_unit_vector(edge_force_vector);

      d_forces.drag_force_vector *= 0;
      d_forces.drag_force_vector += Fd_edge  * edge_force_vector;
      d_forces.drag_force_vector += Fd_plate * d_orientation;
      d_forces.drag_force_vector += Fd_skin  * -d_forces.disc_velocity_unit_vector;

      // Use form drag terms to apply some of the resulting pitching moment
      if(use_updated_pitching_moment_model)
      {
        d_forces.lift_induced_pitching_moment *= 0;
        // for now, we just assume that the centre of application for 'plate drag' is 15% forward from the 
        // disc centre. This actually changes with thickness, and other factors, but this approximation is OK
        // for now.
        const double plate_moment_arm_length = PITCHING_MOMENT_FORM_DRAG_PLATE_OFFSET * d_object.radius * 2;

        // The paper seems to imply that the camber (see below) causes extra torque due to the plate drag
        // We could add this to the torque term as a function of 'amplified' torque here
        // using the camber arc length as an extended effective A_plate
        // we may want to add this to the Fd_plate later as well, not too sure yet
        // Copied from below:
        const double camber_m_edge_depth = 0.011; // 1cm for now
        // treat this like the pulled out edges of a rectangle for now (seems OK)
        const double camber_rect_arc_length = d_object.radius * 2 + camber_m_edge_depth * 2;
        // attenuate this factor with AOA since the entire 'plate' is exposed at some point...?
        const double Fd_plate_pitching_factor = MAX(1.0, camber_rect_arc_length / (d_object.radius * 2) * cos(d_forces.aoar));

        // AOA is about the 'X' axis to the right, positive wrt Fd_plate sign, arm is toward the leading end
        const double Fd_plate_induced_moment_Nm = plate_moment_arm_length * Fd_plate * Fd_plate_pitching_factor * sin(d_forces.aoar);

        // HOWEVER: thise nose-down effect here seems too strong.
        // that is making me thing that this is actually caused by the "lower surface of rim camber"
        // on the underside of the rim
        // note how a form drag force applied there would not affect the same force at the back of the disc.
        // I would then propose that the strength of this torque is a function of AOA, and the (maybe?) 
        // normal of the rim camber
        // we'll assume 'camber_m_edge_depth' is symmetric, and apply a torque here as a function of the
        // rim width.
        const double disc_rim_width = 0.024;
        // optimal angle would be rim_camber_norm_angle = atan2(camber_m_edge_depth, radius) if we assume that is a straight plane
        // then the 'centre' of this effect should be at cos(AOA + rim_camber_norm_angle)
        const double rim_camber_norm_angle = atan2(camber_m_edge_depth, d_object.radius);
        // effect is maxed at cos(aoa + rim_camber_norm_angle - pi/2)

        const double rim_camber_incidence_angle = d_forces.aoar + rim_camber_norm_angle - M_PI_2;
        const double effective_rim_camber_area = disc_rim_width * d_object.radius * 2 * RIM_CAMBER_EXPOSURE;
              double effective_rim_camber_force_N = rhov2o2 * Cd_edge  * effective_rim_camber_area * cos(rim_camber_incidence_angle);
        // if the angle is too far nose-down, this is no longer a factor
        effective_rim_camber_force_N = MAX(0.0, effective_rim_camber_force_N);

        // assume the force is applied halfway along thr rim width
        const double rim_camber_moment_arm_length = d_object.radius * 2 - disc_rim_width * 0.5;

        const double rim_camber_induced_moment_Nm = rim_camber_moment_arm_length * effective_rim_camber_force_N;        

        // assume effective_rim_camber_force_N is all in the disc normal for simplification
        d_forces.lift_induced_pitching_moment += rim_camber_induced_moment_Nm + Fd_plate_induced_moment_Nm;
        // ALSO add the linear force here!
        d_forces.drag_force_vector += effective_rim_camber_force_N * d_orientation;
      }
    }
    else
    {
      d_forces.induced_drag_coefficient  = d_forces.realized_lift_coefficient * d_forces.realized_lift_coefficient / PI_X_AR;
      d_forces.realized_drag_coefficient = d_object.drag_coefficient + d_forces.induced_drag_coefficient + d_forces.stall_induced_drag;
      d_forces.drag_force_magnitude      = d_forces.pav2by2 * d_forces.realized_drag_coefficient;
      d_forces.drag_force_vector = -d_forces.drag_force_magnitude * d_forces.disc_velocity_unit_vector;
    }

    if(use_updated_lift_model)
    {
      const double Cl_base = 1.0;
      const double A_plate  = r2 * M_PI;

      // from the model validation and comparison with wind tunnel data in
      // "dvd_DfisX_form_drag_and_stall_drag_comparison.m"

      // These are all for a destroyer

      // 1. Effective drag in increase from air hitting the back of the disc inner lip
      const double disc_inner_lip_height = 0.012;
      const double disc_rim_width = 0.024;
      // this was shown to be aroun 0.5 by comparison with the wind tunnel models
      const double lip_exposed_surface_factor = 0.5;
      
      // get effective area exposed by inner lip
      const double A_eff_lip = 
        (d_object.radius * 2 - disc_rim_width * 2) *
        disc_inner_lip_height * lip_exposed_surface_factor;

      const double A_eff_lip_at_aoa = A_eff_lip * cos(d_forces.aoar); 

      const double rhov2o2  = throw_container->disc_environment.air_density * d_forces.v2 * 0.5;
      const double Cd_edge  = 1.1;

      const double Fd_lip = rhov2o2 * Cd_edge * A_eff_lip_at_aoa * sin(d_forces.aoar);

      Eigen::Vector3d edge_force_vector = d_orientation.cross(d_orientation.cross(d_forces.disc_velocity_unit_vector));
      make_unit_vector(edge_force_vector);
      d_forces.drag_force_vector += Fd_lip * edge_force_vector;

      // 2. Lift from decreased aispeed below the disc due to the inner lip 'edge' form drag noted above
      // This is a Bernoulli lift effect, since the slowed air below the disc
      // results in an increase in pressure below, resulting in lift
      // define the range for Bernoulli effects from the inner lip
      const double scaling_factor_Fl_lip = 1.0; // arbitrary model tuner for now
      const double lift_factor = 0.0005 / pow(A_eff_lip, 0.95) * scaling_factor_Fl_lip;
            double Fl_lip = rhov2o2 * Cl_base * A_plate * lift_factor;

      // Only attenuate this lift for nose-down, i.e. negative AOAs
      // TODO: Is this right? who knows 
      if(d_forces.aoar < -DEG_TO_RAD(10))
      {
        //Fl_lip *= cos(d_forces.aoar)*cos(d_forces.aoar);
      }

      d_forces.lift_force_vector *= 0;
      d_forces.lift_force_vector += d_orientation * Fl_lip;

      //std::cout << "Fl Lip  = " << std::to_string(Fl_lip) << " N, Lift Factor = " << std::to_string(lift_factor)  << ", Inter = " << std::to_string(A_plate) << std::endl; 

      // 3. Lift from increased top arc-length, and corresponding increasing in 'above disc' airspeed
      // This is the classic 'wing' Bernoulli effect, where the extra distance covered by the airstream
      // above the disc, results in a higher airspeed, and a lower pressure than below, resulting in lift
      // height above edge for camber dome peak
      const double camber_m_edge_depth = 0.012; // 1cm for now

      // treat this like the pulled out edges of a rectangle for now (seems OK)
      const double camber_rect_arc_length = d_object.radius * 2 + camber_m_edge_depth * 2;
      const double camber_arc_to_diameter_ratio = camber_rect_arc_length / (d_object.radius * 2);

      // define the stall range for this effect
      double Fl_arc = 0;
      const double scaling_factor_Fl_arc = 1.0; // arbitrary model tuner for now
      
      if(d_forces.aoar > -DEG_TO_RAD(15) && d_forces.aoar < DEG_TO_RAD(45))
      {
        Fl_arc  = rhov2o2 * A_plate * Cl_base * camber_arc_to_diameter_ratio * sin(d_forces.aoar) * scaling_factor_Fl_arc;
      }

      d_forces.lift_force_vector += d_orientation * Fl_arc;


      // Use bernoulli lift terms to apply some of the resulting pitching moment
      if(use_updated_pitching_moment_model)
      {
        // from the paper and matlab: 
        // the centre offset for the 'Fl_lip' seems to be around 0.1*diameter offset to the back
        // AOA is about the 'X' axis to the right, negative wrt Fl_lip sign, arm is toward the trailing end
        const double Fl_lip_moment_arm_length = PITCHING_MOMENT_CAVITY_LIFT_OFFSET * d_object.radius * 2;
        const double Fl_lip_induced_moment_Nm = -Fl_lip_moment_arm_length * Fl_lip;

        // after contending with the complication of 'Fd_plate_pitching_factor' in the paper results
        // it looks like there is about a 0.1*diameter moment arm left over for the bernoulli lift effects due to the camber
        // this probably changes with AOA, but we'll just make it static for now
        const double Fl_arc_moment_arm_length = PITCHING_MOMENT_CAMBER_LIFT_OFFSET * d_object.radius * 2;

        // We observe that the camber (below) causes extra torque due to the plate drag
        // AOA is about the 'X' axis to the right, so this is positive wrt Fl_arc sign, arm is toward the leading end
        const double Fl_arc_induced_moment_Nm = Fl_arc_moment_arm_length * Fl_arc;

        /*std::cout << "Pitching moment from plate drag = " << std::to_string(d_forces.lift_induced_pitching_moment) << 
          " Nm vs from Fl_lip = " << std::to_string(Fl_lip_induced_moment_Nm) << 
          " Nm, from Fl_arc = " << std::to_string(Fl_arc_induced_moment_Nm) << 
          " Nm, vs old model sum (AOA = " << RAD_TO_DEG(d_forces.aoar) << ") = " <<           
          std::to_string(d_forces.lift_induced_pitching_moment + Fl_lip_induced_moment_Nm + Fl_arc_induced_moment_Nm) << "/" <<  
          std::to_string(0.25 * d_forces.pav2by2 * d_forces.realized_pitching_moment_coefficient * d_object.diameter) << std::endl;*/


        d_forces.lift_induced_pitching_moment += Fl_lip_induced_moment_Nm;
        d_forces.lift_induced_pitching_moment += Fl_arc_induced_moment_Nm;
      }
    }
    else
    {
      d_forces.lift_force_magnitude         = d_forces.pav2by2 * d_forces.realized_lift_coefficient;
      d_forces.lift_force_vector =  d_forces.lift_force_magnitude * d_forces.disc_lift_unit_vector;
    }

    if(use_updated_pitching_moment_model)
    {

    }
    else
    {
      d_forces.lift_induced_pitching_moment = -0.25 * d_forces.pav2by2 * d_forces.realized_pitching_moment_coefficient * d_object.diameter;
    }

    d_forces.aero_force = d_forces.lift_force_vector + d_forces.drag_force_vector;
  }
}