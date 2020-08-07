#ifndef SEXTRACTOR_H
#define SEXTRACTOR_H

#include "image.h"
#include "lists.h"
#include "records.h"

void sextractor_get_stars_list_aper(image_str *, int , struct list_head *, double );
void sextractor_get_stars_list(image_str *, int , struct list_head *);

double sextractor_get_fwhm(image_str *);

#endif /* SEXTRACTOR_H */
