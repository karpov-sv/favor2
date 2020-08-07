#ifndef MATCH_H
#define MATCH_H

#include "coords.h"
#include "lists.h"
#include "image.h"

/* Accepts two lists - with record_str for objects and with star_str for
   catalogue stars, as well as rough coordinates of the center, which will be
   used for computing standard coordinates for stars. */
coords_str match_coords(struct list_head *, struct list_head *, double , double );
/* The same, but also returns the positional uncertainty */
coords_str match_coords_get_sigma(struct list_head *, struct list_head *, double , double , double *);

/* Blind match - call astrometry.net code from ./astrometry/bin/ on the image provided */
coords_str blind_match_coords(image_str *);
coords_str blind_match_coords_from_list(struct list_head *, int , int);
coords_str blind_match_coords_from_list_and_refine(struct list_head *, double , int , int);

/* Refine coordinates uding astrometry.net */
int coords_refine_from_list(coords_str *, struct list_head *, struct list_head *, double , int , int );
void image_coords_refine(image_str *, double );

#endif /* MATCH_H */
