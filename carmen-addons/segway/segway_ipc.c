#include <carmen/carmen.h>
#include "segwaycore.h"
#include "segway_messages.h"

extern segway_t segway;

static void segway_velocity_handler(MSG_INSTANCE msgRef, 
				    BYTE_ARRAY callData,
				    void *clientData __attribute__ ((unused)))
{
  IPC_RETURN_TYPE err;
  carmen_base_velocity_message vel;
 
  err = IPC_unmarshallData(IPC_msgInstanceFormatter(msgRef), callData, &vel,
                           sizeof(carmen_base_velocity_message));
  IPC_freeByteArray(callData);
  carmen_test_ipc_return(err, "Could not unmarshall", 
			 IPC_msgInstanceName(msgRef));
  segway_set_velocity(&segway, vel.tv, vel.rv);
}

void carmen_segway_register_messages(void)
{
  IPC_RETURN_TYPE err;

  /* define messages created by this module */
  err = IPC_defineMsg(CARMEN_BASE_ODOMETRY_NAME, IPC_VARIABLE_LENGTH,
                      CARMEN_BASE_ODOMETRY_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_BASE_ODOMETRY_NAME);
  
  err = IPC_defineMsg(CARMEN_BASE_SONAR_NAME, IPC_VARIABLE_LENGTH,
                      CARMEN_BASE_SONAR_FMT);
  carmen_test_ipc_exit(err, "Could not define IPC message", 
		       CARMEN_BASE_SONAR_NAME);

  err = IPC_defineMsg(CARMEN_BASE_VELOCITY_NAME, IPC_VARIABLE_LENGTH,
                      CARMEN_BASE_VELOCITY_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_BASE_VELOCITY_NAME);

  err = IPC_defineMsg(CARMEN_SEGWAY_POSE_NAME, IPC_VARIABLE_LENGTH,
		      CARMEN_SEGWAY_POSE_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_SEGWAY_POSE_NAME);

  /* setup incoming message handlers */
  err = IPC_subscribe(CARMEN_BASE_VELOCITY_NAME, 
                      segway_velocity_handler, NULL);
  carmen_test_ipc_exit(err, "Could not subscribe", CARMEN_BASE_VELOCITY_NAME);
  IPC_setMsgQueueLength(CARMEN_BASE_VELOCITY_NAME, 1);
}

void carmen_segway_publish_odometry(segway_p segway, double timestamp)
{
  carmen_base_odometry_message odometry;
  IPC_RETURN_TYPE err;

  odometry.x = segway->x;
  odometry.y = segway->y;
  odometry.theta = segway->theta;
  odometry.tv = (segway->lw_velocity + segway->rw_velocity) / 2.0;
  odometry.rv = segway->yaw_rate;
  odometry.acceleration = 0;
  odometry.timestamp = timestamp;
  strcpy(odometry.host, carmen_get_tenchar_host_name());
  err = IPC_publishData(CARMEN_BASE_ODOMETRY_NAME, &odometry);
  carmen_test_ipc_exit(err, "Could not publish", CARMEN_BASE_ODOMETRY_NAME);
}

void carmen_segway_publish_pose(segway_p segway, double timestamp)
{
  static carmen_segway_pose_message pose;
  static int first = 1;
  IPC_RETURN_TYPE err;
  char *host;

  if(first) {
    host = carmen_get_tenchar_host_name();
    strcpy(pose.host, host);
    first = 0;
  }
  pose.pitch = segway->pitch;
  pose.pitch_rate = segway->pitch_rate;
  pose.roll = segway->roll;
  pose.roll_rate = segway->roll_rate;
  pose.lw_velocity = segway->lw_velocity;
  pose.rw_velocity = segway->rw_velocity;
  pose.x = segway->x;
  pose.y = segway->y;
  pose.theta = segway->theta;
  pose.timestamp = timestamp;

  err = IPC_publishData(CARMEN_SEGWAY_POSE_NAME, &pose);
  carmen_test_ipc_exit(err, "Could not publish", CARMEN_SEGWAY_POSE_NAME);
}