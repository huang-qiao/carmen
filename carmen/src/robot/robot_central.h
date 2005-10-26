/*********************************************************
 *
 * This source code is part of the Carnegie Mellon Robot
 * Navigation Toolkit (CARMEN)
 *
 * CARMEN Copyright (c) 2002 Michael Montemerlo, Nicholas
 * Roy, and Sebastian Thrun
 *
 * CARMEN is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public 
 * License as published by the Free Software Foundation; 
 * either version 2 of the License, or (at your option)
 * any later version.
 *
 * CARMEN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied 
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more 
 * details.
 *
 * You should have received a copy of the GNU General 
 * Public License along with CARMEN; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, 
 * Suite 330, Boston, MA  02111-1307 USA
 *
 ********************************************************/

#ifndef ROBOT_H
#define ROBOT_H

#include <carmen/base_messages.h>

#ifdef __cplusplus
extern "C" {
#endif

  // MAX_READINGS controls the number of odometry measurements we 
  // remember for interpolating position stamps.

  // Clock skew estimate is guaranteed only if sufficient odometry
  // is available, so ESTIMATES_CONVERGE is the amount of odometry
  // we need to estimate the clock skew

#define        MAX_READINGS                    5
#define        ESTIMATES_CONVERGE              (MAX_READINGS+25)

#define        ALL_STOP                        1
#define        ALLOW_ROTATE                    2

#define      ODOMETRY_AVERAGE          0
#define      LOCALIZE_AVERAGE          5

#define      MIN_ALLOWED_VELOCITY      0.03 // cm/s

extern carmen_base_odometry_message carmen_robot_latest_odometry;
extern carmen_base_odometry_message carmen_robot_odometry[MAX_READINGS];
extern int carmen_robot_position_received;
extern int carmen_robot_converge;
extern carmen_robot_config_t carmen_robot_config;
extern char *carmen_robot_host;

extern double carmen_robot_collision_avoidance_frequency;
  
 /* cyrill: this leads to an error (gcc 4.0.2, SuSE 10.0), since */
 /* carmen_robot_laser_bearing_skip_rate is declared static. */
    
 /* extern double carmen_robot_laser_bearing_skip_rate; */

extern int carmen_robot_odometry_count; 

extern inline double 
carmen_robot_get_odometry_skew(void)
{
  if(strcmp(carmen_robot_host, carmen_robot_latest_odometry.host) == 0)
    return 0;  
  else 
    return carmen_running_average_report(ODOMETRY_AVERAGE);
}

#ifdef __cplusplus
}
#endif

#endif
