#!/usr/bin/env python

from astropy.io import fits as pyfits
import tempfile, shutil, os, posixpath

class Sextractor:
    def __init__(self, binary='sex', psfex='psfex'):
        self.binary = binary
        self.psfex_binary = psfex
        self.objects = []

        self.nnw = """NNW
# Neural Network Weights for the SExtractor star/galaxy classifier (V1.3)
# inputs:    9 for profile parameters + 1 for seeing.
# outputs:    ``Stellarity index'' (0.0 to 1.0)
# Seeing FWHM range: from 0.025 to 5.5'' (images must have 1.5 < FWHM < 5 pixels)
# Optimized for Moffat profiles with 2<= beta <= 4.\n\n\
 3 10 10  1\n\n\
-1.56604e+00 -2.48265e+00 -1.44564e+00 -1.24675e+00 -9.44913e-01 -5.22453e-01  \
4.61342e-02  8.31957e-01  2.15505e+00  2.64769e-01
 3.03477e+00  2.69561e+00  3.16188e+00  3.34497e+00  3.51885e+00  3.65570e+00  \
3.74856e+00  3.84541e+00  4.22811e+00  3.27734e+00

-3.22480e-01 -2.12804e+00  6.50750e-01 -1.11242e+00 -1.40683e+00 -1.55944e+00 \
-1.84558e+00 -1.18946e-01  5.52395e-01 -4.36564e-01 -5.30052e+00
 4.62594e-01 -3.29127e+00  1.10950e+00 -6.01857e-01  1.29492e-01  1.42290e+00  \
2.90741e+00  2.44058e+00 -9.19118e-01  8.42851e-01 -4.69824e+00
-2.57424e+00  8.96469e-01  8.34775e-01  2.18845e+00  2.46526e+00  8.60878e-02 \
-6.88080e-01 -1.33623e-02  9.30403e-02  1.64942e+00 -1.01231e+00
 4.81041e+00  1.53747e+00 -1.12216e+00 -3.16008e+00 -1.67404e+00 -1.75767e+00 \
-1.29310e+00  5.59549e-01  8.08468e-01 -1.01592e-02 -7.54052e+00
 1.01933e+01 -2.09484e+01 -1.07426e+00  9.87912e-01  6.05210e-01 -6.04535e-02 \
-5.87826e-01 -7.94117e-01 -4.89190e-01 -8.12710e-02 -2.07067e+01
-5.31793e+00  7.94240e+00 -4.64165e+00 -4.37436e+00 -1.55417e+00  7.54368e-01  \
1.09608e+00  1.45967e+00  1.62946e+00 -1.01301e+00  1.13514e-01
 2.20336e-01  1.70056e+00 -5.20105e-01 -4.28330e-01  1.57258e-03 -3.36502e-01 \
-8.18568e-02 -7.16163e+00  8.23195e+00 -1.71561e-02 -1.13749e+01
 3.75075e+00  7.25399e+00 -1.75325e+00 -2.68814e+00 -3.71128e+00 -4.62933e+00 \
-2.13747e+00 -1.89186e-01  1.29122e+00 -7.49380e-01  6.71712e-01
-8.41923e-01  4.64997e+00  5.65808e-01 -3.08277e-01 -1.01687e+00  1.73127e-01 \
-8.92130e-01  1.89044e+00 -2.75543e-01 -7.72828e-01  5.36745e-01
-3.65598e+00  7.56997e+00 -3.76373e+00 -1.74542e+00 -1.37540e-01 -5.55400e-01 \
-1.59195e-01  1.27910e-01  1.91906e+00  1.42119e+00 -4.35502e+00

-1.70059e+00 -3.65695e+00  1.22367e+00 -5.74367e-01 -3.29571e+00  2.46316e+00  \
5.22353e+00  2.42038e+00  1.22919e+00 -9.22250e-01 -2.32028e+00


 0.00000e+00
 1.00000e+00"""

    def write_nnw(self, dir='.'):
        filename = posixpath.join(dir, 'default.nnw')

        with open(filename, 'w') as file:
            file.write(self.nnw)

        return filename

    def write_params(self, params=[], dir='.'):
        filename = posixpath.join(dir, 'default.param')

        with open(filename, 'w') as file:
            file.writelines(["%s\n" % p for p in params])

        return filename

    def run(self, filename=None, image=None, params = ['X_IMAGE', 'Y_IMAGE', 'FLUX_AUTO', 'FLAGS'], options={}, parse=False, outcat=None, workdir=None):
        if not workdir:
            dir = tempfile.mkdtemp('sextractor')
        else:
            dir = workdir

        if not options.has_key('PARAMETERS_NAME'):
            options['PARAMETERS_NAME'] = self.write_params(params, dir=dir)

        # options['STARNNW_NAME'] = self.write_nnw(dir=dir)

        catname = posixpath.join(dir, 'out.cat')
        options['CATALOG_TYPE'] = 'FITS_LDAC'
        options['CATALOG_NAME'] = catname

        if not filename and image is not None:
            filename = posixpath.join(dir, 'image.fits')
            pyfits.writeto(filename, image, clobber=True)

        # Construct the comand
        command = "%s %s" % (self.binary, filename)

        if options:
            command = command + " " + " ".join(["-%s %s" % (str(i), str(k)) for i,k in options.iteritems()])

        # Run the command
        os.system(command + " >/dev/null 2>/dev/null")

        # Parse the result
        self.table = pyfits.getdata(catname, -1)

        if parse:
            self.objects = []

            for row in self.table:
                object = {}
                for param in self.table.names:
                    object[param] = row[param]

                self.objects.append(object)

        if outcat:
            try:
                shutil.move(catname, outcat)
            except:
                pass

        # Cleanup
        if not workdir and dir != '.':
            shutil.rmtree(dir, ignore_errors=True)
            pass

        return self.objects

    def psfex(self, filename=None, options={}):
        # Construct the comand
        command = "%s %s" % (self.psfex_binary, filename)

        if options:
            command = command + " " + " ".join(["-%s %s" % (str(i), str(k)) for i,k in options.iteritems()])

        print command

        # Run the command
        os.system(command)

if __name__ == '__main__':
    pass
