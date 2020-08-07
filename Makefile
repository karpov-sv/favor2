# Makefile for standalone tools and FAVOR library (for external usage, static and shared)

CC       = gcc $(PROFILE) -std=gnu89
PROFILE  = -pg
DEBUG    = -DDEBUG
#DEBUG    = -DDEBUG -DPROFILE_PERFORMANCE
GDEBUG   = -g
OPTIMIZE = -O2 -msse4 -ffast-math -fopenmp
ANDOR    = -DANDOR
ANDOR_FAKE = -DANDOR_FAKE
#TORTORA  = -DTORTORA
#LICENSE  = -DUSE_LICENSE
INCLUDES = -I.
THREADED = -D_REENTRANT -D_GNU_SOURCE -DCORO_UCONTEXT -D_XOPEN_SOURCE
FLAGS    = -Wall -fPIC -fms-extensions -fno-strict-aliasing
CFLAGS   = $(FLAGS) $(THREADED) $(GDEBUG) $(DEBUG) $(OPTIMIZE) $(INCLUDES) $(ANDOR) $(ANDOR_FAKE) $(TORTORA) $(LICENSE)
LDLIBS  = -L. -lm -lcfitsio -lpthread -ljpeg -lgomp

ifeq ($(shell uname), Linux)
LDLIBS  += -ltcmalloc -lcrypt
endif

# PostgreSQL part
LDLIBS += -L`pg_config --libdir` -lpq
INCLUDES += -I`pg_config --includedir`

# GSL part
LDLIBS += -lgsl -lgslcblas

# GLib part
CFLAGS += `pkg-config --cflags glib-2.0`
LDLIBS += `pkg-config --libs glib-2.0`

# Lua part
CFLAGS += `pkg-config --silence-errors --cflags lua || pkg-config --silence-errors --cflags lua5.1 || pkg-config --silence-errors --cflags lua5.2`
LDLIBS += `pkg-config --silence-errors --libs lua || pkg-config --silence-errors --libs lua5.1 || pkg-config --silence-errors --libs lua5.2`

TARGETS = \
	channel \
	hw \
	mount \
	beholder \
	scheduler_server \
	logger \
	allsky \
	meteo_server \
	dbserver \
	coadd \
	crop \
	dome \
	can \
	extract \
	fake_hw \
	fake_grabber \
	fake_weather \
	fake_dome \
	fake_mount \
	test \
	test_license \
	test_match \
	test_field \
	test_simulator \
	test_udp \
	test_dataserver \
	test_celostate \
	test_realtime \

ifdef TORTORA
TARGETS += \
	t_rem \
	t_hw \
	t_beholder \

endif

LIBFAVOR_OBJS = \
	utils.o \
	time_str.o \
	coords.o \
	image.o \
		image_keywords.o \
		image_fits.o \
		image_jpeg.o \
		image_binary.o \
		image_udp.o \
		image_clean.o \
		image_background.o \
	records.o \
	stars.o \
	sextractor.o \
	db.o \
	kdtree.o \
	match.o \
	field.o \
	simulator.o \
	random.o \
	datafile.o \
	dataserver.o \
	average.o \
	matrix.c \
	realtime.o \
		realtime_types.o \
		realtime_find.o \
		realtime_predict.o \
		realtime_classify.o \
		realtime_adjust.o \
	server.o \
	queue.o \
	command.o \
	command_queue.o \
	script.o \
	cJSON.o \
	popen_noshell.o \
	license.o \
	psf.o \
	mpfit.o \
	csa/csa.o \
		csa/svd.o \

CHANNEL_OBJS = \
	channel.o \
		channel_realtime.o \
		channel_secondscale.o \
		channel_raw.o \
		channel_db.o \
		channel_grabber.o \
		channel_celostate.o \

BEHOLDER_OBJS = \
	beholder.o \

TEST_PROCESSING_OBJS = \
	test_processing.o \
		channel_realtime.o \
		channel_secondscale.o \
		channel_raw.o \

SCHEDULER_OBJS = \
	scheduler.o \
		libastro/moon.o \
		libastro/sun.o \
		libastro/vsop87.o \
		libastro/vsop87_data.o \
		libastro/mjd.o \
		libastro/eq_ecl.o \
		libastro/obliq.o \

EXTRACT_OBJS = \
	extract.o \
		extract_peaks.o \

# ANDOR part
ifdef ANDOR
ifeq ($(shell uname), Linux)
ifdef ANDOR_FAKE
LIBFAVOR_OBJS += andor_fake.o
else
LDLIBS  += -latcore
TARGETS += acquire test_andor
LIBFAVOR_OBJS += andor.o
endif
else
LIBFAVOR_OBJS += andor_fake.o
endif
endif

all: depend $(TARGETS) test_processing

$(TARGETS): $(LIBFAVOR_OBJS)

channel: $(CHANNEL_OBJS)

beholder: $(BEHOLDER_OBJS)

test_processing: $(TEST_PROCESSING_OBJS) $(LIBFAVOR_OBJS)

scheduler_server: $(SCHEDULER_OBJS)
test_scheduler: $(SCHEDULER_OBJS)

t_beholder: $(SCHEDULER_OBJS)

extract: $(EXTRACT_OBJS)

libfavor.a:
	$(AR) cru $@ $^

libfavor.so:
	$(CC) -shared -fPIC -o $@ $^

clean:
	rm -f *~ *.o */*.o $(TARGETS) $(SCHEDULER_OBJS)

depend:
	@echo "# DO NOT DELETE THIS LINE -- make depend depends on it." >Makefile.depend
	@makedepend *.c -fMakefile.depend -I/usr/local/include 2>/dev/null

-include Makefile.depend # DO NOT DELETE
# DO NOT DELETE
