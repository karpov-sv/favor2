#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils.h"
#include "simulator.h"
#include "simulator_types.h"
#include "stars.h"
#include "random.h"

#define FASTGAUSS

#ifdef FASTGAUSS
#include "simulator_gauss.h"
#endif /* FASTGAUSS */

simulator_str *simulator_create(int width, int height)
{
    simulator_str *sim = (simulator_str *)malloc(sizeof(simulator_str));

    sim->width = width;
    sim->height = height;

    sim->coords = coords_empty();

    sim->time = time_current();
    sim->frame_number = 0;

    sim->C = -16.8;
    sim->Cb = -0.16;
    sim->Cv = 0.90;

    sim->bg = 300; /* RMS = 17.3 */

    sim->saturation = 4095;

    sim->readout = 1.5;
    sim->amplify = 1;

    sim->fwhm = 2;
    sim->moffat_beta = 1.2;

    sim->satellite_probability = 0.1;
    sim->satellite_mag = 9;
    sim->satellite_mag_sigma = 3;
    sim->satellite_velocity_sigma = 5;
    sim->satellite_period_min = 10;
    sim->satellite_period_max = 100;

    sim->meteor_probability = 0.1;
    sim->meteor_mag = 5;
    sim->meteor_mag_sigma = 3;
    sim->meteor_velocity_mean = 100;
    sim->meteor_velocity_sigma = 20;
    sim->meteor_duration = 5;
    sim->meteor_duration_sigma = 1;

    sim->flash_probability = 0.1;
    sim->flash_mag = 9;
    sim->flash_mag_sigma = 4;
    sim->flash_duration = 10;
    sim->flash_duration_sigma = 3;

    init_list(sim->events);

    sim->background_plane = (double *)calloc(sim->width*sim->height, sizeof(double));
    sim->stars_plane = (double *)calloc(sim->width*sim->height, sizeof(double));
    sim->events_plane = (double *)calloc(sim->width*sim->height, sizeof(double));
    sim->noise_plane = (double *)calloc(sim->width*sim->height, sizeof(double));

    simulator_update_bg(sim);

    random_initialize(0);

    return sim;
}

void simulator_delete(simulator_str *sim)
{
    if(!sim)
        return;

    free_list(sim->events);

    if(sim->background_plane)
        free(sim->background_plane);
    if(sim->stars_plane)
        free(sim->stars_plane);
    if(sim->events_plane)
        free(sim->events_plane);
    if(sim->noise_plane)
        free(sim->noise_plane);

    free(sim);
}

void simulator_set_params(simulator_str *sim, double C, double Cb, double Cv, double bg, double fwhm)
{
    sim->C = C;
    sim->Cb = Cb;
    sim->Cv = Cv;

    sim->bg = bg;

    sim->fwhm = fwhm;

    simulator_update_bg(sim);
    simulator_update_stars(sim);
}

/* pixscale is in arcseconds per pixel */
void simulator_set_field(simulator_str *sim, double ra0, double dec0, double pixscale)
{
    sim->coords.CRPIX1 = sim->width/2;
    sim->coords.CD11 = pixscale/3600;
    sim->coords.CD12 = 0;
    sim->coords.CRPIX2 = sim->height/2;
    sim->coords.CD21 = 0;
    sim->coords.CD22 = -pixscale/3600;

    sim->coords.ra0 = ra0;
    sim->coords.dec0 = dec0;

    simulator_update_stars(sim);
}

void simulator_reset_events(simulator_str *sim)
{
    free_list(sim->events);
}

void simulator_update_bg(simulator_str *sim)
{
    int d;

    for(d = 0; d < sim->width*sim->height; d++)
        sim->background_plane[d] = sim->bg;
}

static double simulator_draw_star(simulator_str *sim, double *plane, double x0, double y0, double flux)
{
    double exact_flux = 0;
    double beta = sim->moffat_beta;
    double r0 = sim->fwhm/1.5;
    int x0i = round(x0);
    int y0i = round(y0);
    double Imin = 0.01*sqrt(sim->bg);
    int rmax = r0*sqrt(pow(flux*(beta - 1)/M_PI/r0/r0/Imin, 1./beta) - 1);
    int y;

    inline double moffat(double r)
    {
        return flux*(beta - 1)/M_PI/r0/r0*pow(1 + r*r/r0/r0, -beta);
    }

    if(flux <= 0)
        return 0;

    for(y = MAX(y0i - rmax, 0); y < MIN(y0i + rmax, sim->height); y++){
        int dx = ceil(sqrt(rmax*rmax - (y - y0i)*(y - y0i)));
        int x;

        for(x = MAX(x0i - dx, 0); x <= MIN(x0i + dx, sim->width - 1); x++){
            double value = moffat(sqrt((x - x0)*(x - x0) + (y - y0)*(y - y0)));

            plane[y*sim->width + x] += value;
            exact_flux += value;
        }
    }

    return exact_flux;
}

void simulator_update_stars(simulator_str *sim)
{
     /* Half-diagonal */
    double sr0 = 0.5*coords_distance(&sim->coords, 0, 0, sim->width - 1, sim->height - 1);
    struct list_head stars;
    star_str *star = NULL;

    if(coords_is_empty(&sim->coords))
        return;

    tycho2_get_stars_list(sim->coords.ra0, sim->coords.dec0, sr0, 100000, &stars);

    memset(sim->stars_plane, 0, sizeof(double)*sim->width*sim->height);

    foreach(star, stars){
        double x = 0;
        double y = 0;
        double flux = pow(10, -0.4*(sim->C + sim->Cb*star->b + sim->Cv*star->v));

        coords_get_x_y(&sim->coords, star->ra, star->dec, &x, &y);

        simulator_draw_star(sim, sim->stars_plane, x, y, flux);
    }

    free_list(stars);
}

image_str *simulator_get_image(simulator_str *sim)
{
    image_str *image = image_create(sim->width, sim->height);
    int d;

    image->coords = sim->coords;
    image->time = sim->time;
    image_keyword_add_int64(image, "FRAMENUMBER", sim->frame_number, NULL);
    image_keyword_add_double(image, "EXPOSURE", 0.13, NULL);
    image_keyword_add_double(image, "PIXSCALE", coords_get_pixscale(&sim->coords), NULL);
    {
        double ra0 = 0;
        double dec0 = 0;

        coords_get_ra_dec(&sim->coords, 0.5*sim->width, 0.5*sim->height, &ra0, &dec0);

        image_keyword_add_double(image, "RA0", ra0, NULL);
        image_keyword_add_double(image, "DEC0", dec0, NULL);
    }

    for(d = 0; d < image->width*image->height; d++){
        double value = sim->background_plane[d] + sim->stars_plane[d] + sim->events_plane[d];
        double value10 = 0;
        double value01 = 0;
        double value11 = 0;

        if(d > 0)
            value10 = sim->noise_plane[d - 1];
        if(d > sim->width)
            value01 = sim->noise_plane[d - sim->width];
        if(d > sim->width + 1)
            value11 = sim->noise_plane[d - sim->width - 1];

        /* Add quasi-poisson spatially correlated noise to the image */
        sim->noise_plane[d] = random_gauss(sqrt(value)) + 0.35*(value10 + 0*value11 + value01);

        value += sim->noise_plane[d];

        value = sim->amplify*value + fabs(random_gauss(sim->readout))*(0.7 + 0.6*(d % 2));

        if(value <= sim->saturation)
            image->data[d] = value;
        else {
#if 0
            int excess = value - sim->saturation;
            int estep = 1;
#endif
            image->data[d] = sim->saturation;
#if 0
            /* Code to redistribute extra counts across nearest pixels */
            /* FIXME: this part is very slow for bright objects */
            while(excess > 0){
                int y = floor(d/sim->width);
                int x = d - y*sim->height;

                while(x >=0 && x < sim->width && y >=0 && y < sim->width){
                    double r1 = 3*random_double();
                    double r2 = 3*random_double();

                    if(PIXEL(image, x, y) + estep <= sim->saturation){
                        PIXEL(image, x, y) += estep;
                        break;
                    }

                    x += r1 < 1 ? -1 : (r1 > 2 ? 1 : 0);
                    y += r2 < 1 ? -1 : (r2 > 2 ? 1 : 0);
                }

                excess -= estep;
            }
#endif
        }
    }

    return image;
}

/* Constructors */
void simulator_make_satellite(simulator_str *sim)
{
    simulated_satellite_str *ev = (simulated_satellite_str *)malloc(sizeof(simulated_satellite_str));
    double r1 = 4*random_double();
    double r2 = random_double();
    double r3 = random_gauss(sim->satellite_velocity_sigma);
    double r4 = random_gauss(sim->satellite_velocity_sigma);

    ev->type = SIMULATED_SATELLITE;

    if(r1 < 1){
        /* Starts at left edge */
        ev->x = 0;
        ev->y = sim->height*r2;
        ev-> vx = fabs(r3);
        ev-> vy = r4;
    } else if(r1 < 2){
        /* Starts at right edge */
        ev->x = sim->width - 1;
        ev->y = sim->height*r2;
        ev-> vx = -fabs(r3);
        ev-> vy = r4;
    } else if(r1 < 3){
        /* Starts at down edge */
        ev->x = sim->width*r2;
        ev->y = 0;
        ev-> vx = r3;
        ev-> vy = fabs(r4);
    } else {
        /* Starts at top edge */
        ev->x = sim->width*r2;
        ev->y = sim->height - 1;
        ev->vx = r3;
        ev->vy = -fabs(r4);
    }

    ev->ax = 0;
    ev->ay = 0;

    ev->ax = 0.001*(random_double(ev->vy) - 0.5);
    ev->ay = 0.001*(random_double(ev->vx) - 0.5);

    ev->mag = sim->satellite_mag + random_gauss(sim->satellite_mag_sigma);

    ev->A = abs(random_gauss(1));
    ev->phase = 2*M_PI*random_double();
    ev->period = sim->satellite_period_min + random_double()*(sim->satellite_period_max - sim->satellite_period_min);

    add_to_list(sim->events, ev);
}

void simulator_make_meteor(simulator_str *sim)
{
    simulated_meteor_str *ev = (simulated_meteor_str *)malloc(sizeof(simulated_meteor_str));
    double r1 = 4*random_double();
    double r2 = random_double();
    double speed = sim->meteor_velocity_mean + random_gauss(sim->meteor_velocity_sigma);
    double angle = 2*M_PI*random_double();
    double r3 = speed*sin(angle);
    double r4 = speed*cos(angle);

    ev->type = SIMULATED_METEOR;

    if(r1 < 1){
        /* Starts at left edge */
        ev->x = 0;
        ev->y = sim->height*r2;
        ev-> vx = fabs(r3);
        ev-> vy = r4;
    } else if(r1 < 2){
        /* Starts at right edge */
        ev->x = sim->width - 1;
        ev->y = sim->height*r2;
        ev-> vx = -fabs(r3);
        ev-> vy = r4;
    } else if(r1 < 3){
        /* Starts at down edge */
        ev->x = sim->width*r2;
        ev->y = 0;
        ev-> vx = r3;
        ev-> vy = fabs(r4);
    } else {
        /* Starts at top edge */
        ev->x = sim->width*r2;
        ev->y = sim->height - 1;
        ev-> vx = -fabs(r3);
        ev-> vy = r4;
    }

    ev->mag = sim->meteor_mag + random_gauss(sim->meteor_mag_sigma);
    ev->duration = sim->meteor_duration + random_gauss(sim->meteor_duration_sigma);
    if(ev->duration < 1)
        ev->duration = sim->meteor_duration;

    ev->t = (random_double() - 0.5)*3*ev->duration;

    add_to_list(sim->events, ev);
}

void simulator_make_flash(simulator_str *sim)
{
    simulated_flash_str *ev = (simulated_flash_str *)malloc(sizeof(simulated_flash_str));

    ev->type = SIMULATED_FLASH;

    ev->x = sim->width*random_double();
    ev->y = sim->height*random_double();
    ev->mag = sim->flash_mag + random_gauss(sim->flash_mag_sigma);
    ev->duration = sim->flash_duration + random_gauss(sim->flash_duration_sigma);
    if(ev->duration < 1)
        ev->duration = sim->flash_duration;

    ev->t = -3*ev->duration;

    add_to_list(sim->events, ev);
}

void simulator_iterate(simulator_str *sim)
{
    double r1 = random_double();
    simulated_event_str *ev;

    sim->time = time_incremented(sim->time, 0.1);

    sim->frame_number ++;

    /* Add new moving event? */
    if(r1 < sim->satellite_probability)
        simulator_make_satellite(sim);
    if(r1 < sim->meteor_probability)
        simulator_make_meteor(sim);
    if(r1 < sim->flash_probability)
        simulator_make_flash(sim);

    /* Clear plane */
    memset(sim->events_plane, 0, sizeof(double)*sim->width*sim->height);

    /* Evolve and draw events */
    foreach(ev, sim->events){
        int is_expired = FALSE;

        /* Dispatch event types */
        if(ev->type == SIMULATED_SATELLITE){
            simulated_satellite_str *s = (simulated_satellite_str *)ev;
            double flux = pow(10, -0.4*(sim->C + (sim->Cb + sim->Cv)*s->mag))*(1 + s->A*sin(s->phase));
            int Nsteps = ceil(sqrt(s->vx*s->vx + s->vy*s->vy));
            int d;

            for(d = 0; d < Nsteps; d++){
                if(flux > 0)
                    simulator_draw_star(sim, sim->events_plane, s->x, s->y, flux/Nsteps);

                s->x += s->vx/Nsteps;
                s->y += s->vy/Nsteps;

                s->vx += s->ax/Nsteps;
                s->vy += s->ay/Nsteps;
            }

            s->phase += 2*M_PI/s->period;

            if(s->x < -sim->fwhm || s->x >= sim->width + sim->fwhm ||
               s->y < -sim->fwhm || s->y >= sim->height + sim->fwhm){
                is_expired = TRUE;
            }
        } else if(ev->type == SIMULATED_METEOR){
            simulated_meteor_str *m = (simulated_meteor_str *)ev;
            double flux0 = pow(10, -0.4*(sim->C + (sim->Cb + sim->Cv)*m->mag));
            int Nsteps = ceil(sqrt(m->vx*m->vx + m->vy*m->vy));
            int d;

            for(d = -5*Nsteps; d < Nsteps; d++){
                double flux = flux0*exp(-pow(m->t + d*1./Nsteps, 2)/pow(m->duration, 2));

                if(d < 0)
                    flux *= exp(d*1./Nsteps);

                simulator_draw_star(sim, sim->events_plane, m->x + d*m->vx/Nsteps, m->y + d*m->vy/Nsteps, flux/Nsteps);
            }

            m->x += m->vx;
            m->y += m->vy;
            m->t += 1;

            if(m->t > 3*m->duration)
                is_expired = TRUE;
        } else if(ev->type == SIMULATED_FLASH){
            simulated_flash_str *f = (simulated_flash_str *)ev;
            double flux0 = pow(10, -0.4*(sim->C + (sim->Cb + sim->Cv)*f->mag));
            double flux = flux0*exp(-pow(f->t, 2)/pow(f->duration, 2));

            f->flux = simulator_draw_star(sim, sim->events_plane, f->x, f->y, flux);
            f->t ++;

            if(f->t > 3*f->duration)
                is_expired = TRUE;
        }

        /* Kill expired events */
        if(is_expired)
            del_from_list_in_foreach_and_run(ev, free(ev));
    }
}
