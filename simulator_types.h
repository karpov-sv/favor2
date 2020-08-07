#ifndef SIMULATOR_TYPES_H
#define SIMULATOR_TYPES_H

/* Various object types */
enum simulated_event_type {
    SIMULATED_SATELLITE,
    SIMULATED_METEOR,
    SIMULATED_FLASH
};

/* Generic object class */
typedef struct simulated_event_str {
    LIST_HEAD(struct simulated_event_str);

    enum simulated_event_type type;

    double x;
    double y;
} simulated_event_str;

/* Derived classes */
typedef struct simulated_satellite_str {
    LIST_HEAD(struct simulated_satellite_str);

    enum simulated_event_type type;

    double x;
    double y;
    double mag;

    double vx;
    double vy;
    double ax;
    double ay;

    double A;
    double phase;
    double period;
} simulated_satellite_str;

typedef struct simulated_meteor_str {
    LIST_HEAD(struct simulated_meteor_str);

    enum simulated_event_type type;

    double x;
    double y;
    double mag;

    double vx;
    double vy;

    double duration;
    double t;
} simulated_meteor_str;

typedef struct simulated_flash_str {
    LIST_HEAD(struct simulated_flash_str);

    enum simulated_event_type type;

    double x;
    double y;
    double mag;
    double flux;

    double duration;
    double t;
} simulated_flash_str;

#endif /* SIMULATOR_TYPES_H */
