#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "image.h"
#include "lists.h"
#include "time_str.h"

typedef struct {
    /* Size of the simulated image */
    int width;
    int height;

    /* Position and size on the sky */
    coords_str coords;
    double sr;

    /* Time and frame number */
    time_str time;
    u_int64_t frame_number;

    /* Read-out noise and amplification */
    double readout;
    double amplify;

    /* Simulation parameters */

    /* Photometric calibration */
    double C;
    double Cb;
    double Cv;
    double bg;
    int saturation;

    /* PSF settings */
    double fwhm;
    double moffat_beta;

    /* Satellite parameters */
    double satellite_probability;
    double satellite_mag;
    double satellite_mag_sigma;
    double satellite_velocity_sigma;
    double satellite_period_min;
    double satellite_period_max;

    /* Meteor parameters */
    double meteor_probability;
    double meteor_mag;
    double meteor_mag_sigma;
    double meteor_velocity_mean;
    double meteor_velocity_sigma;
    double meteor_duration;
    double meteor_duration_sigma;

    /* Flash parameters */
    double flash_probability;
    double flash_mag;
    double flash_mag_sigma;
    double flash_duration;
    double flash_duration_sigma;

    /* Simulated events */
    struct list_head events;

    /* Different data layers */
    double *background_plane;
    double *stars_plane;
    double *events_plane;
    double *noise_plane;
} simulator_str;

simulator_str *simulator_create(int , int );
void simulator_delete(simulator_str *);

void simulator_set_params(simulator_str *, double , double , double , double , double );
void simulator_set_field(simulator_str *, double , double , double );

void simulator_reset_events(simulator_str *);

void simulator_update_bg(simulator_str *);
void simulator_update_stars(simulator_str *);

image_str *simulator_get_image();

void simulator_iterate(simulator_str *);

#endif /* SIMULATOR_H */
