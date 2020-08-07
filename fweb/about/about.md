title: About

About the Mini-MegaTORTORA
====

[![](/static/media/photo_dome_small.jpg){: .img-thumbnail}](/static/media/photo_dome.jpg){: .pull-left style="padding: 10px;"}

The Mini-MegaTORTORA (MMT) system is a novel multi-purpose wide-field monitoring instrument built for and owned by the Kazan Federal University, presently operated under an agreement between Kazan Federal University and Special Astrophysical Observatory, Russia.

It includes a set of nine individual channels installed in pairs on five equatorial mounts.  Every channel has a celostate mirror installed before the Canon EF85/1.2 objective for a rapid (faster than 1 second) adjusting of the objective direction in a limited range (approximately 10 degrees to any direction). This allows for either mosaicking the larger field of view, or for pointing all the channels in one direction. In the latter regime, a set of color (Johnson's B, V or R) and polarimetric (three different directions) filters may be inserted before the objective to maximize the information acquired for the observed region of the sky (performing both three-color photometry and polarimetry).

The channels are equipped with an Andor Neo sCMOS detector having 2560x2160
pixels 6.4um each. Field of view of a channel is roughly 9x11 degrees with
angular resolution of roughly 16'' per pixel.
The detector is able to operate with exposure times as small as 0.03 s, in our
work we use 0.1 s exposures providing us with 10 frames per second.

[![](/static/media/mmt_jun30_small.jpg){: .img-thumbnail}](/static/media/mmt_jun30.jpg){: .pull-right style="padding: 10px;"}

The Mini-MegaTORTORA (MMT) instrument is operational since Jun 2014 and shows the
performance close to the expected. We hope it will be useful for studying
various phenomena on the sky, both astrophysical and artificial in origin.
We expect it to be used for studying faint meteoric streams crossing Earth
orbit, for detecting new comets and asteroids, for finding flashes of flaring
stars and novae, studying variable stars of various classes, detecting transits
of exoplanets, searching for bright supernovae and optical counterparts of
gamma-ray bursts.

The novelty of the MMT is its ability to re-configure itself from a wide-field
to narrower-field instrument, which may open new ways of studying the sky, as
it may, in principle, autonomously perform thorough study of objects it
discovers - to simultaneously acquire three-color photometry and polarimetry of
them.

### MMT operation ###

Mini-MegaTORTORA started its operation in Jun 2014, and routinely monitor the
sky since then. The observations are governed by the dedicated dynamic
scheduler optimized for performing the sky survey. The scheduler works by
selecting the next pointing for Mini-MegaTORTORA by simultaneously optimizing
the following parameters: distances from the Sun, Moon and the horizon should
be maximized, distances from the current pointings of Swift and Fermi
satellites should be minimized, and the number of frames already acquired on a
given sky position that night should be minimized. In this way more or less
uniform survey of the whole sky hemisphere is being performed while maximizing
the probability of observations of gamma-ray bursts. As an un-optimized
extension, the scheduler also supports the observations of pre-selected
targets given by their coordinates, which may be performed in various regimes
supported by Mini-MegaTORTORA (wide-field monitoring of a given region of the
sky with or without filters, narrow-field multicolor imaging or polarimetry
with lower temporal resolution, etc).

Moreover, the scheduler and central control system supports various types of
follow-up observations triggered by external messages and typically
corresponding to transient events occurred outside the current Mini-MegaTORTORA
field of view. It will try to rapidly repoint and observe the localizations of
Swift BAT and XRT triggers in either multi-color or polarimetric mode, typically large
error boxes of Fermi GBM in wide-field mode, etc. The large size of
Mini-MegaTORTORA field of view in wide-field regime makes such observations
very promising for rapid pin-pointing of possible optical transients
corresponding to triggers with bad accuracy of initial localization.

### Data analysis ###

The main regime of Mini-MegaTORTORA operation is the wide-field monitoring with
high temporal resolution and with no photometric filters installed. In this
regime, every channel acquires 10 frames per second, which corresponds to 110
megabytes of data per second. To analyze it, we implemented the real-time fast
differential imaging pipeline intended for the detection of rapidly varying or
moving transient objects - flashes, meteor trails, satellite passes etc. It is
analogous to the pipeline of FAVOR and TORTORA cameras, and is based on building an
iteratively-updated comparison image of current field of view using numerically
efficient running median algorithm, as well as threshold image using running
similarly constructed *median absolute deviation* estimate, and then
comparison of every new frame with them, extracting candidate transient objects
and analyzing lists of these objects from the consecutive frames. It then
filters out noise events, extracts the meteor trails by their generally
elongated shape on a single frame, collects the events correspinding to moving
objects into focal plane trajectories, etc.


Every 100 frames acquired by a channel are being summed together, yielding
"average" frames with 10 s effective exposure and better detection
limit. Using these frames, the astrometric calibration is being performed using
locally installed [Astrometry.Net](http://astrometry.net) code. Also the
rough photometric calibration is being done. These calibrations, updated every
10 seconds, are used for measuring the positions and magnitudes of transients
detected by the real-time differential imaging pipeline. The "average" frames
are stored permanently (in contrast to "raw" full-resolution data which is
typically erased in a day or two after acquisition) and may be used later for
studying the variability on time scales longer than 10 s.

The Mini-MegaTORTORA typically observes every sky field continuousy for a 10000
seconds before moving to the next pointing. Before and after observing the
field with high temporal resolution, the system acquires deeper "survey"
images with 60 seconds exposure in white light in order to study the
variability of objects down to 14-15 magnitude on even longer time scales;
typically, every point of the northern sky is covered by one or more such
images every observational night.

As a first step of analysis of these survey data, we implemented the transient
detection pipeline based on comparison of positions of objects detected on
our images with Guide Star Catalogue v2.3.2, as well as with Minor Planet
Center database. This pipeline routinely detects tens of knwn asteroids every
night, and sometimes - the flares of dwarf novae and other transients.

The full-scale photometric pipeline for survey images are still in preparation,
as the precise photometry of these frames turned to be quite difficult task due
to large size of point spread function of a Canon objective with extended wings
harbouring up to 40% of light. This leads to the severe photometric errors in
typical stellar fields, significantly crowded even outside the Galaxy
plane. Now we are implementing the PSF-fitting code optimized for the accurate
measurement of Mini-MegaTORTORA survey images and hope to finish in 2016.
