#ifndef HOKUYOLASER_H
#define HOKUYOLASER_H

/* needed for new carmen_inline def for gcc >= 4.3 */
#include <carmen/carmen.h>

//#define HOKUYO_ALWAYS_IN_SCIP20

#define HOKUYO_BUFSIZE 8192
typedef enum {
  HOKUYO_TYPE_URG,
  HOKUYO_TYPE_UTM
  } hokuyo_type_t;
//UTM parameters
#define UTM_ANGULAR_STEP (M_PI/720.0)
#define UTM_MAX_BEAMS 1081
#define UTM_MAX_RANGE 30.0
#define UTM_ACCURACY .0001
#define UTM_FOV (270.0*M_PI/180.0)
#define UTM_START_ANGLE (-UTM_FOV/2.0)

//URG parameters
#define URG_ANGULAR_STEP (M_PI/512.0)
#define URG_MAX_BEAMS 768
#define URG_MAX_RANGE 5.6
#define URG_ACCURACY .001
#define URG_FOV (240.0*M_PI/180.0)
#define URG_START_ANGLE   (-URG_FOV/2.0) //(-120.0*M_PI/180.0)



typedef struct HokuyoRangeReading{
  int timestamp;
  int status;
  int n_ranges;
  unsigned short ranges[UTM_MAX_BEAMS];  //use the UTM size, which wastes a bit of space for URG, but whatever...
  unsigned short startStep, endStep, clusterCount;
  hokuyo_type_t type;
} HokuyoRangeReading;

typedef struct HokuyoLaser{
  int fd;
  int isProtocol2;
  int isContinuous;
  int isInitialized;
} HokuyoLaser;

// opens the hokuyoLaser, returns <=0 on failure
int hokuyo_open(HokuyoLaser* hokuyoLaser, const char* filename);

// initializes the hokuyoLaser and sets it to the new scip2.0 protocol
// returns <=0 on failure
int hokuyo_init(HokuyoLaser* hokuyoLaser);

// reads a packet into the buffer
unsigned int hokuyo_readPacket(HokuyoLaser* hokuyoLaser, char* buf, int bufsize, int faliures);

// starts the continuous mode
int hokuyo_startContinuous(HokuyoLaser* hokuyoLaser, int startStep, int endStep, int clusterCount);

// starts the continuous mode
int hokuyo_stopContinuous(HokuyoLaser* hokuyoLaser);
int hokuyo_reset(HokuyoLaser* hokuyoLaser);
int hokuyo_close(HokuyoLaser* hokuyoLaser);
void hokuyo_parseReading(HokuyoRangeReading* r, char* buffer);

#endif

