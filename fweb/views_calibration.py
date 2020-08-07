from django.http import HttpResponse
from django.template.response import TemplateResponse

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from matplotlib.dates import DateFormatter

import os
import posixpath

import settings

def get_file(filename, concatenate = False):
    lines = []

    if os.path.exists(filename):
        with open(filename, 'r') as file:
            lines = file.readlines()

    if concatenate:
        return " ".join(lines)

    return lines

def calibration(request):
    focus = get_file(posixpath.join(settings.BASE, 'hw.focus'), concatenate=True)

    return TemplateResponse(request, 'calibration.html', context={'focus':focus})

def get_celostate_calib(filename):
    pos = []
    dist = []
    delta_ra = []
    delta_dec = []

    if os.path.exists(filename):
        with open(filename, 'r') as file:
            l = file.readlines()

            for line in l:
                pos1, dist1, dra1, ddec1 = line.split()

                pos.append(pos1)
                dist.append(dist1)
                delta_ra.append(dra1)
                delta_dec.append(ddec1)

    return {'pos':pos, 'dist':dist, 'delta_ra':delta_ra, 'delta_dec':delta_dec}

def calibration_celostate(request, type='dist'):
    c1 = get_celostate_calib(posixpath.join(settings.BASE, 'celostate1.txt'))
    c2 = get_celostate_calib(posixpath.join(settings.BASE, 'celostate2.txt'))

    fig = Figure(facecolor='white')
    ax = fig.add_subplot(111)

    if type == 'dist':
        ax.set_title("Celostate calibration: distance from field center")

        ax.set_ylabel("Distance, degrees")

        ax.plot(c2['pos'], c2['dist'], 'o-', color='red', label='RA mirror')
        ax.plot(c1['pos'], c1['dist'], 'o-', color='green', label='Dec mirror')
    elif type == 'shift':
        ax.set_title("Celostate calibration: RA/Dec shifts")

        ax.set_ylabel("Shift, degrees")

        ax.plot(c2['pos'], c2['delta_ra'], 'o-', color='red', label='RA mirror / RA')
        ax.plot(c2['pos'], c2['delta_dec'], 'o--', color='red', label='RA mirror / Dec')

        ax.plot(c1['pos'], c1['delta_ra'], 'o--', color='green', label='Dec mirror / RA')
        ax.plot(c1['pos'], c1['delta_dec'], 'o-', color='green', label='Dec mirror / Dec')

    ax.set_xlabel("Celostate position")

    ax.legend(loc=9).draw_frame(False)

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response
