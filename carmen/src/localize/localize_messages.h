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

#ifndef LOCALIZE_MESSAGES_H
#define LOCALIZE_MESSAGES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialization message for localize */

#define CARMEN_INITIALIZE_UNIFORM     1
#define CARMEN_INITIALIZE_GAUSSIAN    2

typedef struct {
  int distribution;
  int num_modes;
  carmen_point_t *mean, *std;
  double timestamp;
  char host[10];
} carmen_localize_initialize_message;

#define CARMEN_LOCALIZE_INITIALIZE_NAME  "carmen_localize_initialize"
#define CARMEN_LOCALIZE_INITIALIZE_FMT   "{int,int,<{double,double,double}:2>,<{double,double,double}:2>,double,[char:10]}"

  /* initialize by map placename */

typedef struct {
  char *placename;
  double timestamp;
  char host[10];
} carmen_localize_initialize_placename_message;

#define CARMEN_LOCALIZE_INITIALIZE_PLACENAME_NAME "carmen_localize_initialize_placename"
#define CARMEN_LOCALIZE_INITIALIZE_PLACENAME_FMT "{string,double,[char:10]}"

/* Contains the mean and standard deviation of the position of the robot */

typedef struct {
  carmen_point_t globalpos, globalpos_std;
  carmen_point_t odometrypos;
  double globalpos_xy_cov;
  int converged;
  double timestamp;
  char host[10];
} carmen_localize_globalpos_message;

#define CARMEN_LOCALIZE_GLOBALPOS_NAME "carmen_localize_globalpos"
#define CARMEN_LOCALIZE_GLOBALPOS_FMT  "{{double,double,double},{double,double,double},{double,double,double},double,int,double,[char:10]}"

/* particle message */

typedef struct {
  float x, y, theta, weight;
} carmen_localize_particle_ipc_t, *carmen_localize_particle_ipc_p;

typedef struct {
  int num_particles;
  carmen_localize_particle_ipc_p particles;
  carmen_point_t globalpos, globalpos_std;
  double globalpos_xy_cov;
  double timestamp;
  char host[10];
} carmen_localize_particle_message;

#define CARMEN_LOCALIZE_PARTICLE_NAME "carmen_localize_particle"
#define CARMEN_LOCALIZE_PARTICLE_FMT  "{int,<{float,float,float,float}:1>,{double,double,double},{double,double,double},double,double,[char:10]}"

/* sensor message in localize coordinates */

typedef struct {
  int num_readings, laser_skip;
  float *range;
  char *mask;
  carmen_point_t pose;
  int num_laser;
  double timestamp;
  char host[10];
} carmen_localize_sensor_message;

#define CARMEN_LOCALIZE_SENSOR_NAME "carmen_localize_sensor"
#define CARMEN_LOCALIZE_SENSOR_FMT  "{int,int,<float:1>,<char:1>,{double,double,double},int,double,[char:10]}"

typedef struct {
  int global;
  double timestamp;
  char host[10];
} carmen_localize_query_message;
  
#define CARMEN_LOCALIZE_QUERY_NAME "carmen_localize_query"
#define CARMEN_LOCALIZE_QUERY_FMT "{int,double,[char:10]}"
  
typedef struct {
  unsigned char *data;    
  int size;
  carmen_map_config_t config;
  int compressed;
  int global;
  double timestamp;
  char host[10];
} carmen_localize_map_message;  
  
#define CARMEN_LOCALIZE_MAP_NAME "carmen_localize_map"
#define CARMEN_LOCALIZE_MAP_FMT  "{<char:2>,int,{int,int,double,string},int,int,double,[char:10]}"

#ifdef __cplusplus
}
#endif

#endif