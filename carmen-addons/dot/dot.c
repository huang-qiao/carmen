#include <carmen/carmen.h>
#include "dot_messages.h"
#include "dot.h"


#define MAX_PERSON_FILTER_VELOCITY_WINDOW 1000

typedef struct dot_person_filter {
  double vx[MAX_PERSON_FILTER_VELOCITY_WINDOW];
  double vy[MAX_PERSON_FILTER_VELOCITY_WINDOW];
  int vpos;
  int vlen;
  int hidden_cnt;
  double x;
  double y;
  double px;
  double py;
  double pxy;
  double a;
  double qx;
  double qy; 
  double qxy;
  double rx;
  double ry;
  double rxy;
} carmen_dot_person_filter_t, *carmen_dot_person_filter_p;

typedef struct dot_trash_filter {
  double x;
  double y;
  double px;
  double py;
  double pxy;
  double a;
  double qx;
  double qy;
  double qxy;
  double rx;
  double ry;
  double rxy;
} carmen_dot_trash_filter_t, *carmen_dot_trash_filter_p;

typedef struct dot_door_filter {
  double x;
  double y;
  double t;  //theta
  double px;
  double py;
  double pt;
  double pxy;
  double a;
  double qx;
  double qy;
  double qxy;
  double qt;
} carmen_dot_door_filter_t, *carmen_dot_door_filter_p;

typedef struct dot_filter {
  carmen_dot_person_filter_t person_filter;
  carmen_dot_trash_filter_t trash_filter;
  carmen_dot_door_filter_t door_filter;
  int id;
  int type;
  int allow_change;
  int sensor_update_cnt;
  int do_motion_update;
  int updated;
  int last_type;
} carmen_dot_filter_t, *carmen_dot_filter_p;


static double default_person_filter_a;
static double default_person_filter_px;
static double default_person_filter_py;
static double default_person_filter_pxy;
static double default_person_filter_qx;
static double default_person_filter_qy;
static double default_person_filter_qxy;
static double default_person_filter_rx;
static double default_person_filter_ry;
static double default_person_filter_rxy;
static double default_trash_filter_px;
static double default_trash_filter_py;
static double default_trash_filter_a;
static double default_trash_filter_qx;
static double default_trash_filter_qy;
static double default_trash_filter_qxy;
static double default_trash_filter_rx;
static double default_trash_filter_ry;
static double default_trash_filter_rxy;
static double default_door_filter_px;
static double default_door_filter_py;
static double default_door_filter_pt;
static double default_door_filter_a;
static double default_door_filter_qx;
static double default_door_filter_qy;
static double default_door_filter_qxy;
static double default_door_filter_qt;
static double default_door_filter_rx;
static double default_door_filter_ry;
static double default_door_filter_rt;

static int new_filter_threshold;
static double new_cluster_threshold;
static double map_diff_threshold;
static double map_occupied_threshold;
static double sensor_update_dist;
static int sensor_update_cnt;

static int person_filter_velocity_window;
static double person_filter_velocity_threshold;
static int kill_hidden_person_cnt;


static carmen_localize_globalpos_message odom;
static carmen_point_t last_sensor_update_odom;
static int do_sensor_update = 1;
static carmen_map_t static_map;
static carmen_dot_filter_p filters = NULL;
static int num_filters = 0;
static double laser_max_range;


static void publish_dot_msg(carmen_dot_filter_p f, int delete);

static void reset() {

  free(filters);
  filters = NULL;
  num_filters = 0;
}

static inline double dist(double dx, double dy) {

  return sqrt(dx*dx+dy*dy);
}

static void rotate2d(double *x, double *y, double theta) {

  double x2, y2;

  x2 = *x;
  y2 = *y; 

  *x = x2*cos(theta) - y2*sin(theta);
  *y = x2*sin(theta) + y2*cos(theta);
}

/*
 * inverts 2d matrix [[a, b],[c, d]]
 */
static int invert2d(double *a, double *b, double *c, double *d) {

  double det;
  double a2, b2, c2, d2;

  det = (*a)*(*d) - (*b)*(*c);
  
  if (fabs(det) <= 0.00000001)
    return -1;

  a2 = (*d)/det;
  b2 = -(*b)/det;
  c2 = -(*c)/det;
  d2 = (*a)/det;

  *a = a2;
  *b = b2;
  *c = c2;
  *d = d2;

  return 0;
}

/*
 * computes the orientation of the bivariate normal with variances
 * vx, vy, and covariance vxy.  returns an angle in [-pi/2, pi/2].
 */
static double bnorm_theta(double vx, double vy, double vxy) {

  double theta;
  double e1, e2;  // eigenvalues
  double ex, ey;  // major eigenvector

  e1 = (vx + vy)/2.0 + sqrt(vxy*vxy + (vx-vy)*(vx-vy)/4.0);
  e2 = (vx + vy)/2.0 - sqrt(vxy*vxy + (vx-vy)*(vx-vy)/4.0);
  ex = vxy;
  ey = (e1>e2?e1:e2) - vx;

  theta = atan2(ey, ex);

  return theta;
}

static double bnorm_f(double x, double y, double ux, double uy,
		      double vx, double vy, double vxy) {

  double z, p;

  p = vxy/sqrt(vx*vy);
  z = (x - ux)*(x - ux)/vx - 2.0*p*(x - ux)*(y - uy)/vxy + (y - uy)*(y - uy)/vy;

  return exp(-z/(2.0*(1 - p*p)))/(2.0*M_PI*sqrt(vx*vy*(1 - p*p)));
}

static int dot_classify(carmen_dot_filter_p f) {

  carmen_dot_person_filter_p pf;
  carmen_dot_trash_filter_p tf;
  carmen_dot_door_filter_p df;
  double pdet, tdet, ddet;
  int type;

  if (!f->allow_change)
    return f->type;

  pf = &f->person_filter;
  tf = &f->trash_filter;
  df = &f->door_filter;

  pdet = pf->px*pf->py - pf->pxy*pf->pxy;
  tdet = tf->px*tf->py;
  ddet = df->px*df->py - df->pxy*df->pxy;

  if (pdet <= tdet) {
    if (pdet <= ddet)
      type = CARMEN_DOT_PERSON;
    else
      type = CARMEN_DOT_DOOR;
  }
  else {
    if (tdet <= ddet)
      type = CARMEN_DOT_TRASH;
    else
      type = CARMEN_DOT_DOOR;
  }

  return type;
}

static double person_filter_velocity(carmen_dot_person_filter_p f) {

  int i;
  double vx, vy;

  if (f->vlen < person_filter_velocity_window)
    return 0.0;

  vx = 0.0;
  vy = 0.0;
  for (i = 0; i < person_filter_velocity_window; i++) {
    vx += f->vx[i];
    vy += f->vy[i];
  }
  vx /= (double)i;
  vy /= (double)i;

  printf("vel = %.4f\n", dist(vx, vy));

  return dist(vx, vy);
}

static void person_filter_motion_update(carmen_dot_person_filter_p f) {

  double ax, ay;
  
  // kalman filter time update with brownian motion model
  ax = carmen_uniform_random(-1.0, 1.0) * f->a;
  ay = carmen_uniform_random(-1.0, 1.0) * f->a;

  //printf("ax = %.4f, ay = %.4f\n", ax, ay);

  f->x += ax;
  f->y += ay;
  f->px = ax*ax*f->px + f->qx;
  f->pxy = ax*ax*f->pxy + f->qxy;
  f->py = ay*ay*f->py + f->qy;

  f->vx[f->vpos] = ax;
  f->vy[f->vpos] = ay;
  f->vpos = (f->vpos+1) % person_filter_velocity_window;
  if (f->vlen < person_filter_velocity_window)
    f->vlen++;
  else
    f->vlen = person_filter_velocity_window;
}

static void person_filter_sensor_update(carmen_dot_person_filter_p f,
				       double x, double y) {

  double pxx, pxy, pyx, pyy;
  double mxx, mxy, myx, myy;
  double kxx, kxy, kyx, kyy;
  double x2, y2;

  //printf("update_person_filter()\n");

  pxx = f->px;
  pxy = f->pxy;
  pyx = f->pxy;
  pyy = f->py;

  //printf("pxx = %.4f, pxy = %.4f, pyx = %.4f, pyy = %.4f\n", pxx, pxy, pyx, pyy);

  // kalman filter sensor update
  mxx = pxx + f->rx;
  mxy = pxy + f->rxy;
  myx = pyx + f->rxy;
  myy = pyy + f->ry;

  //printf("mxx = %.4f, mxy = %.4f, myx = %.4f, myy = %.4f\n", mxx, mxy, myx, myy);

  if (invert2d(&mxx, &mxy, &myx, &myy) < 0) {
    carmen_warn("Error: can't invert matrix M");
    return;
  }

  //printf("mxx = %.4f, mxy = %.4f, myx = %.4f, myy = %.4f\n", mxx, mxy, myx, myy);

  kxx = pxx*mxx+pxy*myx;
  kxy = pxx*mxy+pxy*myy;
  kyx = pyx*mxx+pyy*myx;
  kyy = pyx*mxy+pyy*myy;

  //printf("kxx = %.4f, kxy = %.4f, kyx = %.4f, kyy = %.4f\n", kxx, kxy, kyx, kyy);

  x2 = f->x + kxx*(x - f->x) + kxy*(y - f->y);
  y2 = f->y + kyx*(x - f->x) + kyy*(y - f->y);
  f->vx[f->vpos] = x2 - f->x;
  f->vy[f->vpos] = y2 - f->y;
  f->vpos = (f->vpos+1) % person_filter_velocity_window;
  if (f->vlen < person_filter_velocity_window)
    f->vlen++;
  else
    f->vlen = person_filter_velocity_window;
  f->x = x2;
  f->y = y2;
  f->px = (1.0 - kxx)*pxx + (-kxy)*pyx;
  f->pxy = (1.0 - kxx)*pxy + (-kxy)*pyy;
  f->py = (-kyx)*pxy + (1.0 - kyy)*pyy;
}

static void trash_filter_motion_update(carmen_dot_trash_filter_p f) {

  double ax, ay;
  
  // kalman filter time update with brownian motion model
  ax = carmen_uniform_random(-1.0, 1.0) * f->a;
  ay = carmen_uniform_random(-1.0, 1.0) * f->a;

  //printf("ax = %.4f, ay = %.4f\n", ax, ay);

  f->x += ax;
  f->y += ay;
  f->px = ax*ax*f->px + f->qx;
  f->pxy = ax*ax*f->pxy + f->qxy;
  f->py = ay*ay*f->py + f->qy;
}

static void trash_filter_sensor_update(carmen_dot_trash_filter_p f,
				       double x, double y) {

  double pxx, pxy, pyx, pyy;
  double mxx, mxy, myx, myy;
  double kxx, kxy, kyx, kyy;

  pxx = f->px;
  pxy = f->pxy;
  pyx = f->pxy;
  pyy = f->py;
  
  // kalman filter sensor update
  mxx = pxx + f->rx;
  mxy = pxy + f->rxy;
  myx = pyx + f->rxy;
  myy = pyy + f->ry;
  if (invert2d(&mxx, &mxy, &myx, &myy) < 0) {
    carmen_warn("Error: can't invert matrix M");
    return;
  }
  kxx = pxx*mxx+pxy*myx;
  kxy = pxx*mxy+pxy*myy;
  kyx = pyx*mxx+pyy*myx;
  kyy = pyx*mxy+pyy*myy;
  f->x = f->x + kxx*(x - f->x) + kxy*(y - f->y);
  f->y = f->y + kyx*(x - f->x) + kyy*(y - f->y);
  f->px = (1.0 - kxx)*pxx + (-kxy)*pyx;
  f->pxy = (1.0 - kxx)*pxy + (-kxy)*pyy;
  f->py = (-kyx)*pxy + (1.0 - kyy)*pyy;  
}

static void door_filter_motion_update(carmen_dot_door_filter_p f) {

  double ax, ay;
  
  // kalman filter time update with brownian motion model
  ax = carmen_uniform_random(-1.0, 1.0) * f->a;
  ay = carmen_uniform_random(-1.0, 1.0) * f->a;

  //printf("ax = %.4f, ay = %.4f\n", ax, ay);

  f->x += ax;
  f->y += ay;
  f->px = ax*ax*f->px + f->qx;
  f->pxy = ax*ax*f->pxy + f->qxy;
  f->py = ay*ay*f->py + f->qy;
}

static void door_filter_sensor_update(carmen_dot_door_filter_p f, double x, double y) {

  double rx, ry, rxy, rt;
  double pxx, pxy, pyx, pyy, pt;
  double mxx, mxy, myx, myy, mt;
  double kxx, kxy, kyx, kyy, kt;

  pxx = f->px;
  pxy = f->pxy;
  pyx = f->pxy;
  pyy = f->py;
  pt = f->pt; //dbug: + f->qt ?

  rx = default_door_filter_rx*cos(f->t)*cos(f->t) +
    default_door_filter_ry*sin(f->t)*sin(f->t);
  ry = default_door_filter_rx*sin(f->t)*sin(f->t) +
    default_door_filter_ry*cos(f->t)*cos(f->t);
  rxy = default_door_filter_rx*cos(f->t)*sin(f->t) -
    default_door_filter_ry*cos(f->t)*sin(f->t);
  rt = default_door_filter_rt;

  // kalman filter sensor update
  mxx = pxx + rx;
  mxy = pxy + rxy;
  myx = pyx + rxy;
  myy = pyy + ry;
  mt = pt + rt;
  if (invert2d(&mxx, &mxy, &myx, &myy) < 0) {
    carmen_die("Error: can't invert matrix M");
    return;
  }
  mt = 1.0/mt;
  kxx = pxx*mxx+pxy*myx;
  kxy = pxx*mxy+pxy*myy;
  kyx = pyx*mxx+pyy*myx;
  kyy = pyx*mxy+pyy*myy;
  kt = pt*mt;
  f->x = f->x + kxx*(x - f->x) + kxy*(y - f->y);
  f->y = f->y + kyx*(x - f->x) + kyy*(y - f->y);
  f->px = (1.0 - kxx)*pxx + (-kxy)*pyx;
  f->pxy = (1.0 - kxx)*pxy + (-kxy)*pyy;
  f->py = (-kyx)*pxy + (1.0 - kyy)*pyy;
  f->pt = (1.0 - kt)*pt;
}

static void add_new_dot_filter(int *cluster_map, int c, int n,
			       double *x, double *y) {
  int i, id;

  for (id = 0; id < num_filters; id++) {
    for (i = 0; i < num_filters; i++)
      if (filters[i].id == id)
	break;
    if (i == num_filters)
      break;
  }

  for (i = 0; i < n && cluster_map[i] != c; i++);
  if (i == n)
    carmen_die("Error: invalid cluster! (cluster %d not found)\n", c);

  num_filters++;
  filters = (carmen_dot_filter_p) realloc(filters, num_filters*sizeof(carmen_dot_filter_t));
  carmen_test_alloc(filters);

  filters[num_filters-1].id = id;

  filters[num_filters-1].person_filter.x = x[i];
  filters[num_filters-1].person_filter.y = y[i];
  for (i = 0; i < MAX_PERSON_FILTER_VELOCITY_WINDOW; i++) {
    filters[num_filters-1].person_filter.vx[i] = 0.0;
    filters[num_filters-1].person_filter.vy[i] = 0.0;
  }
  filters[num_filters-1].person_filter.vpos = 0;
  filters[num_filters-1].person_filter.vlen = 0;
  filters[num_filters-1].person_filter.hidden_cnt = 0;
  filters[num_filters-1].person_filter.px = default_person_filter_px;
  filters[num_filters-1].person_filter.py = default_person_filter_py;
  filters[num_filters-1].person_filter.pxy = default_person_filter_pxy;
  filters[num_filters-1].person_filter.a = default_person_filter_a;
  filters[num_filters-1].person_filter.qx = default_person_filter_qx;
  filters[num_filters-1].person_filter.qy = default_person_filter_qy;
  filters[num_filters-1].person_filter.qxy = default_person_filter_qxy;
  filters[num_filters-1].person_filter.rx = default_person_filter_rx;
  filters[num_filters-1].person_filter.ry = default_person_filter_ry;
  filters[num_filters-1].person_filter.rxy = default_person_filter_rxy;

  for (i = 0; i < n; i++)
    if (cluster_map[i] == c)
      person_filter_sensor_update(&filters[num_filters-1].person_filter, x[i], y[i]);

  filters[num_filters-1].trash_filter.x = x[i];
  filters[num_filters-1].trash_filter.y = y[i];
  filters[num_filters-1].trash_filter.px = default_trash_filter_px;
  filters[num_filters-1].trash_filter.py = default_trash_filter_py;
  filters[num_filters-1].trash_filter.a = default_trash_filter_a;
  filters[num_filters-1].trash_filter.qx = default_trash_filter_qx;
  filters[num_filters-1].trash_filter.qy = default_trash_filter_qy;
  filters[num_filters-1].trash_filter.qxy = default_trash_filter_qxy;
  filters[num_filters-1].trash_filter.rx = default_trash_filter_rx;
  filters[num_filters-1].trash_filter.ry = default_trash_filter_ry;
  filters[num_filters-1].trash_filter.rxy = default_trash_filter_rxy;

  for (i = 0; i < n; i++)
    if (cluster_map[i] == c)
      trash_filter_sensor_update(&filters[num_filters-1].trash_filter, x[i], y[i]);

  filters[num_filters-1].door_filter.x = x[i];
  filters[num_filters-1].door_filter.y = y[i];
  filters[num_filters-1].door_filter.t =
    bnorm_theta(filters[num_filters-1].person_filter.px,
		filters[num_filters-1].person_filter.py,
		filters[num_filters-1].person_filter.pxy);
  filters[num_filters-1].door_filter.px = default_door_filter_px;
  filters[num_filters-1].door_filter.py = default_door_filter_py;
  filters[num_filters-1].door_filter.pt = default_door_filter_pt;
  filters[num_filters-1].door_filter.pxy = 0;  //dbug?
  filters[num_filters-1].door_filter.a = default_door_filter_a;
  filters[num_filters-1].door_filter.qx = default_door_filter_qx;
  filters[num_filters-1].door_filter.qy = default_door_filter_qy;
  filters[num_filters-1].door_filter.qxy = default_door_filter_qxy;
  filters[num_filters-1].door_filter.qt = default_door_filter_qt;  

  for (i = 0; i < n; i++)
    if (cluster_map[i] == c)
      door_filter_sensor_update(&filters[num_filters-1].door_filter, x[i], y[i]);

  filters[num_filters-1].type = dot_classify(&filters[num_filters-1]);
  filters[num_filters-1].allow_change = 1;
  filters[num_filters-1].sensor_update_cnt = 0;
  filters[num_filters-1].do_motion_update = 0;
  filters[num_filters-1].updated = 1;
  filters[num_filters-1].last_type = filters[num_filters-1].type;
}

static double map_prob(double x, double y) {

  carmen_world_point_t wp;
  carmen_map_point_t mp;

  wp.map = &static_map;
  wp.pose.x = x;
  wp.pose.y = y;

  carmen_world_to_map(&wp, &mp);

  return static_map.map[mp.x][mp.y];
}

static int dot_filter(double x, double y) {

  int i, imax;
  double pmax, p;
  double ux, uy, dx, dy;
  double vx, vy, vxy;
  double theta;

  imax = -1;
  pmax = 0.0;

  for (i = 0; i < num_filters; i++) {

    ux = filters[i].person_filter.x;
    uy = filters[i].person_filter.y;
    vx = filters[i].person_filter.px;
    vy = filters[i].person_filter.py;
    vxy = filters[i].person_filter.pxy;
    dx = x - ux;
    dy = y - uy;

    // check if (x,y) is roughly within 3 stdev's of (E[x],E[y])
    theta = bnorm_theta(vx, vy, vxy);
    rotate2d(&dx, &dy, -theta);
    if (fabs(dx) < 3.0*sqrt(vx/fabs(cos(theta))) && 
	fabs(dy) < 3.0*sqrt(vy/fabs(sin(theta)))) {
      p = bnorm_f(x, y, ux, uy, vx, vy, vxy);
      if (p > pmax) {
	pmax = p;
	imax = i;
      }
    }

    ux = filters[i].trash_filter.x;
    uy = filters[i].trash_filter.y;
    vx = filters[i].trash_filter.px;
    vy = filters[i].trash_filter.py;
    vxy = filters[i].trash_filter.pxy;
    dx = x - ux;
    dy = y - uy;

    // check if (x,y) is roughly within 3 stdev's of (E[x],E[y])
    theta = bnorm_theta(vx, vy, vxy);
    rotate2d(&dx, &dy, -theta);
    if (fabs(dx) < 3.0*sqrt(vx/fabs(cos(theta))) && 
	fabs(dy) < 3.0*sqrt(vy/fabs(sin(theta)))) {
      p = bnorm_f(x, y, ux, uy, vx, vy, vxy);
      if (p > pmax) {
	pmax = p;
	imax = i;
      }
    }
    
    ux = filters[i].door_filter.x;
    uy = filters[i].door_filter.y;
    vx = filters[i].door_filter.px;
    vy = filters[i].door_filter.py;
    vxy = filters[i].door_filter.pxy;
    dx = x - ux;
    dy = y - uy;

    // check if (x,y) is roughly within 3 stdev's of (E[x],E[y])
    theta = bnorm_theta(vx, vy, vxy);
    rotate2d(&dx, &dy, -theta);
    if (fabs(dx) < 3.0*sqrt(vx/fabs(cos(theta))) && 
	fabs(dy) < 3.0*sqrt(vy/fabs(sin(theta)))) {
      p = bnorm_f(x, y, ux, uy, vx, vy, vxy);
      if (p > pmax) {
	pmax = p;
	imax = i;
      }
    }
  }

  if (imax >= 0 && pmax > map_prob(x, y)) {
    filters[imax].person_filter.hidden_cnt = 0;
    person_filter_sensor_update(&filters[imax].person_filter, x, y);
    if (filters[imax].do_motion_update) {
      filters[imax].do_motion_update = 0;
      trash_filter_motion_update(&filters[imax].trash_filter);
      door_filter_motion_update(&filters[imax].door_filter);
      filters[imax].updated = 1;
    }
    if (do_sensor_update || filters[imax].sensor_update_cnt < sensor_update_cnt) {
      filters[imax].sensor_update_cnt++;
      trash_filter_sensor_update(&filters[imax].trash_filter, x, y);
      door_filter_sensor_update(&filters[imax].door_filter, x, y);
      filters[imax].updated = 1;
    }
    filters[imax].type = dot_classify(&filters[imax]);

    if (filters[imax].type == CARMEN_DOT_PERSON)
      filters[imax].updated = 1;
    
    return 1;
  }

  return 0;
}

static inline int is_in_map(int x, int y) {

  return (x >= 0 && y >= 0 && x < static_map.config.x_size &&
	  y < static_map.config.y_size);
}

// returns 1 if point is in map
static int map_filter(double x, double y) {

  carmen_world_point_t wp;
  carmen_map_point_t mp;
  double d;
  int md, i, j;

  wp.map = &static_map;
  wp.pose.x = x;
  wp.pose.y = y;

  carmen_world_to_map(&wp, &mp);

  d = map_diff_threshold/static_map.config.resolution;
  md = carmen_round(d);

  for (i = mp.x-md; i <= mp.x+md; i++)
    for (j = mp.y-md; j <= mp.y+md; j++)
      if (is_in_map(i, j) && dist(i-mp.x, j-mp.y) <= d &&
	  static_map.map[i][j] >= map_occupied_threshold)
	return 1;

  return 0;
}

static int cluster(int *cluster_map, int cluster_cnt, int current_reading,
		   double *x, double *y) {
  
  int i, r;
  int nmin;
  double dmin;
  double d;

  if (cluster_cnt == 0) {
    cluster_map[current_reading] = 1;
    //printf("putting reading %d at (%.2f %.2f) in cluster 1\n", current_reading, *x, *y);
    return 1;
  }

  r = current_reading;
  dmin = new_cluster_threshold;
  nmin = -1;

  // nearest neighbor
  for (i = 0; i < r; i++) {
    if (cluster_map[i]) {
      d = dist(x[i]-x[r], y[i]-y[r]);
      if (d < dmin) {
	dmin = d;
	nmin = i;
      }
    }
  }

  if (nmin < 0) {
    cluster_cnt++;
    cluster_map[r] = cluster_cnt;
  }
  else
    cluster_map[r] = cluster_map[nmin];

  //  printf("putting reading %d at (%.2f %.2f) in cluster %d\n", current_reading, *x, *y,
  //	 cluster_map[r]);

  return cluster_cnt;
}

static void delete_filter(int i) {

  publish_dot_msg(&filters[i], 1);

  if (i < num_filters-1)
    memmove(&filters[i], &filters[i+1], (num_filters-i-1)*sizeof(carmen_dot_filter_t));
  num_filters--;
}

static void kill_people() {

  int i;

  for (i = 0; i < num_filters; i++)
    if (++filters[i].person_filter.hidden_cnt >= kill_hidden_person_cnt &&
	filters[i].type == CARMEN_DOT_PERSON)
      delete_filter(i);
}

static void filter_motion() {

  int i;

  for (i = 0; i < num_filters; i++) {
    if (person_filter_velocity(&filters[i].person_filter) >=
	person_filter_velocity_threshold) {
      filters[i].type = CARMEN_DOT_PERSON;
      filters[i].allow_change = 0;
    }
    person_filter_motion_update(&filters[i].person_filter);
    if (filters[i].type == CARMEN_DOT_PERSON)
      filters[i].updated = 1;
  }
}

static void laser_handler(carmen_robot_laser_message *laser) {

  int i, c, n;
  static int cluster_map[500];
  static int cluster_cnt;
  static double x[500], y[500];

  if (static_map.map == NULL || odom.timestamp == 0.0)
    return;

  carmen_localize_correct_laser(laser, &odom);

  for (i = 0; i < num_filters; i++) {
    filters[i].updated = 0;
    filters[i].last_type = filters[i].type;
  }

  kill_people();
  filter_motion();

  if (dist(last_sensor_update_odom.x - odom.globalpos.x,
	   last_sensor_update_odom.y - odom.globalpos.y) >= sensor_update_dist) {
    do_sensor_update = 1;
    for (i = 0; i < num_filters; i++)
      filters[i].do_motion_update = 1;
  }
  else
    do_sensor_update = 0;
  
  if (do_sensor_update) {
    last_sensor_update_odom.x = odom.globalpos.x;
    last_sensor_update_odom.y = odom.globalpos.y;
  }

  cluster_cnt = 0;
  for (i = 0; i < laser->num_readings; i++) {
    if (laser->range[i] < laser_max_range) {
      x[i] = laser->x + cos(laser->theta + (i-90)*M_PI/180.0) * laser->range[i];
      y[i] = laser->y + sin(laser->theta + (i-90)*M_PI/180.0) * laser->range[i];
      if (!dot_filter(x[i], y[i]) && !map_filter(x[i], y[i]))
	cluster_cnt = cluster(cluster_map, cluster_cnt, i, x, y);
      else
	cluster_map[i] = 0;
    }
  }

  //printf("cluster_cnt = %d\n", cluster_cnt);

  for (c = 1; c <= cluster_cnt; c++) {
    n = 0;
    for (i = 0; i < laser->num_readings; i++)
      if (cluster_map[i] == c)
	n++;
    //printf("cluster %d has %d readings\n", c, n);
    if (n >= new_filter_threshold)
      add_new_dot_filter(cluster_map, c, laser->num_readings, x, y);
  }

  for (i = 0; i < num_filters; i++) {
    if (filters[i].type != filters[i].last_type) {
      n = filters[i].type;
      filters[i].type = filters[i].last_type;
      publish_dot_msg(&filters[i], 1);
      filters[i].type = n;
    }
    if (filters[i].updated)
      publish_dot_msg(&filters[i], 1);
  }

  for (i = 0; i < num_filters; i++) {
    printf("vel = %.4f, ", person_filter_velocity(&filters[i].person_filter));
    if (filters[i].type == CARMEN_DOT_PERSON) {
      printf("PERSON (x=%.2f, y=%.2f) (vx=%.4f, vy=%.4f, vxy=%.4f)\n",
	     filters[i].person_filter.x, filters[i].person_filter.y,
	     filters[i].person_filter.px, filters[i].person_filter.py,
	     filters[i].person_filter.pxy);
    }
    else if (filters[i].type == CARMEN_DOT_TRASH)
      printf("TRASH (x=%.2f, y=%.2f) (vx=%.4f, vy=%.4f, vxy=%.4f)\n",
	     filters[i].trash_filter.x, filters[i].trash_filter.y,
	     filters[i].trash_filter.px, filters[i].trash_filter.py,
	     filters[i].trash_filter.pxy);
    else
      printf("DOOR (x=%.2f, y=%.2f, t=%.2f) (vx=%.4f, vy=%.4f, vxy=%.4f, vt=%.4f)\n",
	     filters[i].door_filter.x, filters[i].door_filter.y,
	     filters[i].door_filter.t, filters[i].door_filter.px,
	     filters[i].door_filter.py, filters[i].door_filter.pxy,
	     filters[i].door_filter.pt);
  }
  printf("\n");
}

static void publish_person_msg(carmen_dot_filter_p f, int delete) {

  static carmen_dot_person_msg msg;
  static int first = 1;
  IPC_RETURN_TYPE err;
  
  if (first) {
    strcpy(msg.host, carmen_get_tenchar_host_name());
    first = 0;
  }

  msg.person.id = f->id;
  msg.person.x = f->person_filter.x;
  msg.person.y = f->person_filter.y;
  msg.person.vx = f->person_filter.px;
  msg.person.vy = f->person_filter.py;
  msg.person.vxy = f->person_filter.pxy;
  msg.delete = delete;
  msg.timestamp = carmen_get_time_ms();

  err = IPC_publishData(CARMEN_DOT_PERSON_MSG_NAME, &msg);
  carmen_test_ipc_exit(err, "Could not publish", CARMEN_DOT_PERSON_MSG_NAME);
}

static void publish_trash_msg(carmen_dot_filter_p f, int delete) {

  static carmen_dot_trash_msg msg;
  static int first = 1;
  IPC_RETURN_TYPE err;
  
  if (first) {
    strcpy(msg.host, carmen_get_tenchar_host_name());
    first = 0;
  }

  msg.trash.id = f->id;
  msg.trash.x = f->trash_filter.x;
  msg.trash.y = f->trash_filter.y;
  msg.trash.vx = f->trash_filter.px;
  msg.trash.vy = f->trash_filter.py;
  msg.trash.vxy = f->trash_filter.pxy;
  msg.delete = delete;
  msg.timestamp = carmen_get_time_ms();

  err = IPC_publishData(CARMEN_DOT_TRASH_MSG_NAME, &msg);
  carmen_test_ipc_exit(err, "Could not publish", CARMEN_DOT_TRASH_MSG_NAME);
}

static void publish_door_msg(carmen_dot_filter_p f, int delete) {

  static carmen_dot_door_msg msg;
  static int first = 1;
  IPC_RETURN_TYPE err;
  
  if (first) {
    strcpy(msg.host, carmen_get_tenchar_host_name());
    first = 0;
  }

  msg.door.id = f->id;
  msg.door.x = f->door_filter.x;
  msg.door.y = f->door_filter.y;
  msg.door.theta = f->door_filter.t;
  msg.door.vx = f->door_filter.px;
  msg.door.vy = f->door_filter.py;
  msg.door.vxy = f->door_filter.pxy;
  msg.door.vtheta = f->door_filter.pt;
  msg.delete = delete;
  msg.timestamp = carmen_get_time_ms();

  err = IPC_publishData(CARMEN_DOT_DOOR_MSG_NAME, &msg);
  carmen_test_ipc_exit(err, "Could not publish", CARMEN_DOT_DOOR_MSG_NAME);
}

static void publish_dot_msg(carmen_dot_filter_p f, int delete) {

  switch (f->type) {
  case CARMEN_DOT_PERSON:
    publish_person_msg(f, delete);
    break;
  case CARMEN_DOT_TRASH:
    publish_trash_msg(f, delete);
    break;
  case CARMEN_DOT_DOOR:
    publish_door_msg(f, delete);
    break;
  }
}

static void respond_all_people_msg(MSG_INSTANCE msgRef) {

  static carmen_dot_all_people_msg msg;
  static int first = 1;
  IPC_RETURN_TYPE err;
  int i, n;
  
  if (first) {
    strcpy(msg.host, carmen_get_tenchar_host_name());
    msg.people = NULL;
    first = 0;
  }

  for (n = i = 0; i < num_filters; i++)
    if (filters[i].type == CARMEN_DOT_PERSON)
      n++;

  msg.num_people = n;
  msg.people = (carmen_dot_person_p)realloc(msg.people, n*sizeof(carmen_dot_person_t));
  carmen_test_alloc(msg.people);

  for (n = i = 0; i < num_filters; i++) {
    if (filters[i].type == CARMEN_DOT_PERSON) {
      msg.people[n].id = filters[i].id;
      msg.people[n].x = filters[i].person_filter.x;
      msg.people[n].y = filters[i].person_filter.y;
      msg.people[n].vx = filters[i].person_filter.px;
      msg.people[n].vy = filters[i].person_filter.py;
      msg.people[n].vxy = filters[i].person_filter.pxy;
      n++;
    }
  }

  msg.timestamp = carmen_get_time_ms();

  err = IPC_respondData(msgRef, CARMEN_DOT_ALL_PEOPLE_MSG_NAME, &msg);
  carmen_test_ipc(err, "Could not respond",
		  CARMEN_DOT_ALL_PEOPLE_MSG_NAME);
}

static void respond_all_trash_msg(MSG_INSTANCE msgRef) {

  static carmen_dot_all_trash_msg msg;
  static int first = 1;
  IPC_RETURN_TYPE err;
  int i, n;
  
  if (first) {
    strcpy(msg.host, carmen_get_tenchar_host_name());
    msg.trash = NULL;
    first = 0;
  }

  for (n = i = 0; i < num_filters; i++)
    if (filters[i].type == CARMEN_DOT_TRASH)
      n++;

  msg.num_trash = n;
  msg.trash = (carmen_dot_trash_p)realloc(msg.trash, n*sizeof(carmen_dot_trash_t));
  carmen_test_alloc(msg.trash);

  for (n = i = 0; i < num_filters; i++) {
    if (filters[i].type == CARMEN_DOT_TRASH) {
      msg.trash[n].id = filters[i].id;
      msg.trash[n].x = filters[i].trash_filter.x;
      msg.trash[n].y = filters[i].trash_filter.y;
      msg.trash[n].vx = filters[i].trash_filter.px;
      msg.trash[n].vy = filters[i].trash_filter.py;
      msg.trash[n].vxy = filters[i].trash_filter.pxy;
      n++;
    }
  }

  msg.timestamp = carmen_get_time_ms();

  err = IPC_respondData(msgRef, CARMEN_DOT_ALL_TRASH_MSG_NAME, &msg);
  carmen_test_ipc(err, "Could not respond",
		  CARMEN_DOT_ALL_TRASH_MSG_NAME);
}

static void respond_all_doors_msg(MSG_INSTANCE msgRef) {

  static carmen_dot_all_doors_msg msg;
  static int first = 1;
  IPC_RETURN_TYPE err;
  int i, n;
  
  if (first) {
    strcpy(msg.host, carmen_get_tenchar_host_name());
    msg.doors = NULL;
    first = 0;
  }

  for (n = i = 0; i < num_filters; i++)
    if (filters[i].type == CARMEN_DOT_DOOR)
      n++;

  msg.num_doors = n;
  msg.doors = (carmen_dot_door_p)realloc(msg.doors, n*sizeof(carmen_dot_door_t));
  carmen_test_alloc(msg.doors);

  for (n = i = 0; i < num_filters; i++) {
    if (filters[i].type == CARMEN_DOT_DOOR) {
      msg.doors[n].id = filters[i].id;
      msg.doors[n].x = filters[i].door_filter.x;
      msg.doors[n].y = filters[i].door_filter.y;
      msg.doors[n].theta = filters[i].door_filter.t;
      msg.doors[n].vx = filters[i].door_filter.px;
      msg.doors[n].vy = filters[i].door_filter.py;
      msg.doors[n].vxy = filters[i].door_filter.pxy;
      msg.doors[n].vtheta = filters[i].door_filter.pt;
      n++;
    }
  }

  msg.timestamp = carmen_get_time_ms();

  err = IPC_respondData(msgRef, CARMEN_DOT_ALL_DOORS_MSG_NAME, &msg);
  carmen_test_ipc(err, "Could not respond",
		  CARMEN_DOT_ALL_DOORS_MSG_NAME);
}

static void dot_query_handler
(MSG_INSTANCE msgRef, BYTE_ARRAY callData,
 void *clientData __attribute__ ((unused))) {

  FORMATTER_PTR formatter;
  IPC_RETURN_TYPE err;
  carmen_dot_query query;

  formatter = IPC_msgInstanceFormatter(msgRef);
  err = IPC_unmarshallData(formatter, callData, &query, sizeof(carmen_dot_query));
  IPC_freeByteArray(callData);
  carmen_test_ipc_return(err, "Could not unmarshall",
			 IPC_msgInstanceName(msgRef));

  switch(query.type) {
  case CARMEN_DOT_PERSON:
    respond_all_people_msg(msgRef);
    break;
  case CARMEN_DOT_TRASH:
    respond_all_trash_msg(msgRef);
    break;
  case CARMEN_DOT_DOOR:
    respond_all_doors_msg(msgRef);
    break;
  }
}

static void reset_handler
(MSG_INSTANCE msgRef __attribute__ ((unused)), BYTE_ARRAY callData,
 void *clientData __attribute__ ((unused))) {

  IPC_freeByteArray(callData);

  reset();
}

static void ipc_init() {

  IPC_RETURN_TYPE err;

  err = IPC_defineMsg(CARMEN_DOT_PERSON_MSG_NAME, IPC_VARIABLE_LENGTH, 
	 	      CARMEN_DOT_PERSON_MSG_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_DOT_PERSON_MSG_NAME);

  err = IPC_defineMsg(CARMEN_DOT_TRASH_MSG_NAME, IPC_VARIABLE_LENGTH, 
	 	      CARMEN_DOT_TRASH_MSG_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_DOT_TRASH_MSG_NAME);

  err = IPC_defineMsg(CARMEN_DOT_DOOR_MSG_NAME, IPC_VARIABLE_LENGTH, 
	 	      CARMEN_DOT_DOOR_MSG_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_DOT_DOOR_MSG_NAME);

  err = IPC_defineMsg(CARMEN_DOT_ALL_PEOPLE_MSG_NAME, IPC_VARIABLE_LENGTH, 
	 	      CARMEN_DOT_ALL_PEOPLE_MSG_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_DOT_ALL_PEOPLE_MSG_NAME);

  err = IPC_defineMsg(CARMEN_DOT_ALL_TRASH_MSG_NAME, IPC_VARIABLE_LENGTH, 
	 	      CARMEN_DOT_ALL_TRASH_MSG_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_DOT_ALL_TRASH_MSG_NAME);

  err = IPC_defineMsg(CARMEN_DOT_ALL_DOORS_MSG_NAME, IPC_VARIABLE_LENGTH, 
	 	      CARMEN_DOT_ALL_DOORS_MSG_FMT);
  carmen_test_ipc_exit(err, "Could not define", CARMEN_DOT_ALL_DOORS_MSG_NAME);

  err = IPC_subscribe(CARMEN_DOT_QUERY_NAME, dot_query_handler, NULL);
  carmen_test_ipc_exit(err, "Could not subcribe", CARMEN_DOT_QUERY_NAME);
  IPC_setMsgQueueLength(CARMEN_DOT_QUERY_NAME, 100);

  err = IPC_subscribe(CARMEN_DOT_RESET_MSG_NAME, reset_handler, NULL);
  carmen_test_ipc_exit(err, "Could not subcribe", CARMEN_DOT_RESET_MSG_NAME);
  IPC_setMsgQueueLength(CARMEN_DOT_RESET_MSG_NAME, 100);

  carmen_robot_subscribe_frontlaser_message(NULL,
					    (carmen_handler_t)laser_handler,
					    CARMEN_SUBSCRIBE_LATEST);
  carmen_localize_subscribe_globalpos_message(&odom, NULL,
					      CARMEN_SUBSCRIBE_LATEST);
}

static void person_filter_velocity_window_handler() {

  if (person_filter_velocity_window > MAX_PERSON_FILTER_VELOCITY_WINDOW)
    person_filter_velocity_window = MAX_PERSON_FILTER_VELOCITY_WINDOW;
}

static void params_init(int argc, char *argv[]) {

  carmen_param_t param_list[] = {
    {"robot", "front_laser_max", CARMEN_PARAM_DOUBLE, &laser_max_range, 1, NULL},
    {"dot", "person_filter_a", CARMEN_PARAM_DOUBLE, &default_person_filter_a, 1, NULL},
    {"dot", "person_filter_px", CARMEN_PARAM_DOUBLE, &default_person_filter_px, 1, NULL},
    {"dot", "person_filter_py", CARMEN_PARAM_DOUBLE, &default_person_filter_py, 1, NULL},
    {"dot", "person_filter_pxy", CARMEN_PARAM_DOUBLE, &default_person_filter_pxy, 1, NULL},
    {"dot", "person_filter_qx", CARMEN_PARAM_DOUBLE, &default_person_filter_qx, 1, NULL},
    {"dot", "person_filter_qy", CARMEN_PARAM_DOUBLE, &default_person_filter_qy, 1, NULL},
    {"dot", "person_filter_qxy", CARMEN_PARAM_DOUBLE, &default_person_filter_qxy, 1, NULL},
    {"dot", "person_filter_rx", CARMEN_PARAM_DOUBLE, &default_person_filter_rx, 1, NULL},
    {"dot", "person_filter_ry", CARMEN_PARAM_DOUBLE, &default_person_filter_ry, 1, NULL},
    {"dot", "person_filter_rxy", CARMEN_PARAM_DOUBLE, &default_person_filter_rxy, 1, NULL},
    {"dot", "trash_filter_px", CARMEN_PARAM_DOUBLE, &default_trash_filter_px, 1, NULL},
    {"dot", "trash_filter_py", CARMEN_PARAM_DOUBLE, &default_trash_filter_py, 1, NULL},
    {"dot", "trash_filter_a", CARMEN_PARAM_DOUBLE, &default_trash_filter_a, 1, NULL},
    {"dot", "trash_filter_qx", CARMEN_PARAM_DOUBLE, &default_trash_filter_qx, 1, NULL},
    {"dot", "trash_filter_qy", CARMEN_PARAM_DOUBLE, &default_trash_filter_qy, 1, NULL},
    {"dot", "trash_filter_qxy", CARMEN_PARAM_DOUBLE, &default_trash_filter_qxy, 1, NULL},
    {"dot", "trash_filter_rx", CARMEN_PARAM_DOUBLE, &default_trash_filter_rx, 1, NULL},
    {"dot", "trash_filter_ry", CARMEN_PARAM_DOUBLE, &default_trash_filter_ry, 1, NULL},
    {"dot", "trash_filter_rxy", CARMEN_PARAM_DOUBLE, &default_trash_filter_rxy, 1, NULL},
    {"dot", "door_filter_px", CARMEN_PARAM_DOUBLE, &default_door_filter_px, 1, NULL},
    {"dot", "door_filter_py", CARMEN_PARAM_DOUBLE, &default_door_filter_py, 1, NULL},
    {"dot", "door_filter_pt", CARMEN_PARAM_DOUBLE, &default_door_filter_pt, 1, NULL},
    {"dot", "door_filter_a", CARMEN_PARAM_DOUBLE, &default_door_filter_a, 1, NULL},
    {"dot", "door_filter_qx", CARMEN_PARAM_DOUBLE, &default_door_filter_qx, 1, NULL},
    {"dot", "door_filter_qy", CARMEN_PARAM_DOUBLE, &default_door_filter_qy, 1, NULL},
    {"dot", "door_filter_qxy", CARMEN_PARAM_DOUBLE, &default_door_filter_qxy, 1, NULL},
    {"dot", "door_filter_qt", CARMEN_PARAM_DOUBLE, &default_door_filter_qt, 1, NULL},
    {"dot", "door_filter_rx", CARMEN_PARAM_DOUBLE, &default_door_filter_rx, 1, NULL},
    {"dot", "door_filter_ry", CARMEN_PARAM_DOUBLE, &default_door_filter_ry, 1, NULL},
    {"dot", "door_filter_rt", CARMEN_PARAM_DOUBLE, &default_door_filter_rt, 1, NULL},
    {"dot", "new_filter_threshold", CARMEN_PARAM_INT, &new_filter_threshold, 1, NULL},
    {"dot", "new_cluster_threshold", CARMEN_PARAM_DOUBLE, &new_cluster_threshold, 1, NULL},
    {"dot", "map_diff_threshold", CARMEN_PARAM_DOUBLE, &map_diff_threshold, 1, NULL},
    {"dot", "map_occupied_threshold", CARMEN_PARAM_DOUBLE, &map_occupied_threshold, 1, NULL},
    {"dot", "person_filter_velocity_window", CARMEN_PARAM_INT, &person_filter_velocity_window,
     1, (carmen_param_change_handler_t)person_filter_velocity_window_handler},
    {"dot", "person_filter_velocity_threshold", CARMEN_PARAM_DOUBLE,
     &person_filter_velocity_threshold, 1, NULL},
    {"dot", "kill_hidden_person_cnt", CARMEN_PARAM_INT, &kill_hidden_person_cnt, 1, NULL},
    {"dot", "sensor_update_dist", CARMEN_PARAM_DOUBLE, &sensor_update_dist, 1, NULL},
    {"dot", "sensor_update_cnt", CARMEN_PARAM_INT, &sensor_update_cnt, 1, NULL}
  };

  carmen_param_install_params(argc, argv, param_list,
	 		      sizeof(param_list) / sizeof(param_list[0]));
}

void shutdown_module(int sig) {

  sig = 0;

  exit(0);
}

int main(int argc, char *argv[]) {

  //int c;

  carmen_initialize_ipc(argv[0]);
  carmen_param_check_version(argv[0]);
  carmen_randomize(&argc, &argv);

  signal(SIGINT, shutdown_module);

  odom.timestamp = 0.0;
  static_map.map = NULL;

  printf("getting gridmap...");
  fflush(0);
  carmen_map_get_gridmap(&static_map);
  printf("done\n");

  params_init(argc, argv);
  ipc_init();

  carmen_terminal_cbreak(0);

  while (1) {
    sleep_ipc(0.01);
    /****************
    c = getchar();
    if (c != EOF) {
      switch (c) {
      case ' ':
	get_background_range = 1;
	printf("\nCollecting background range...");
	fflush(0);
	break;
      case 'p':
	display_position = !display_position;
      }
    }
    ****************/
  }

  return 0;
}