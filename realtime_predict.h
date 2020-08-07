#ifndef REALTIME_PREDICT_H
#define REALTIME_PREDICT_H

#include "realtime.h"
#include "realtime_types.h"
#include "time_str.h"

typedef struct prediction_str {
    /* Position and size of the prediction */
    double x;
    double y;
    double dx;
    double dy;
    /* Has the motion been detected? */
    int is_motion;
} prediction_str;

prediction_str realtime_predict(object_str *, time_str , int, double );
prediction_str realtime_predict_weighted(object_str *, time_str , int, double , double );
double realtime_prediction_size(prediction_str );

inline int object_prediction_check(prediction_str , double , double , double , double , double );
inline int object_prediction_check_record(prediction_str , record_str *, double , double );
/* Check out-of-frame condition */
int object_prediction_is_out_of_frame(prediction_str , average_image_str *);
/* Size of prediction region */
inline double object_prediction_size(prediction_str );
/* Distance of the record from prediction center */
inline double object_prediction_record_distance(prediction_str , record_str *);

#endif /* REALTIME_PREDICT_H */
