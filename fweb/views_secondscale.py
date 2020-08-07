from django.http import HttpResponse
from django.template.response import TemplateResponse
from django.shortcuts import redirect

from models import Records

from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
from matplotlib.dates import DateFormatter
from matplotlib.ticker import ScalarFormatter
from matplotlib.patches import Ellipse

from resolve import resolve

from utils import permission_required_or_403, assert_permission

favor2_filters = {0:'gray', 1:'b', 2:'g', 3:'r', 4:'gray', 5:'b', 6:'g', 7:'r'}

@permission_required_or_403('fweb.access_data')
def secondscale_info(request, ra=0, dec=0, sr=0.01, size=1000):
    if request.method == 'POST':
        coords = request.POST.get('coords')
        sr = request.POST.get('sr')
        sr = float(sr) if sr else 0.01
        name, ra, dec = resolve(coords)

        return redirect('/secondscale/coords/%g/%g/%g' %(ra, dec, sr))

    return TemplateResponse(request, 'secondscale_info.html', context={'ra':ra, 'dec':dec, 'sr':sr})

@permission_required_or_403('fweb.access_data')
def secondscale_lightcurve(request, ra=0, dec=0, sr=0.01, size=1000, type='mag'):
    type = type or 'mag'

    if request.method == 'POST':
        coords = request.POST.get('coords')
        sr = float(request.POST.get('sr'))
        name, ra, dec = resolve(coords)

        return redirect('/secondscale/lightcurve/coords/%g/%g/%g' %(ra, dec, sr))

    if ra and dec:
        ra = float(ra)
        dec = float(dec)
        sr = float(sr) if sr else 0.01

    records = Records.objects.extra(where=["q3c_radial_query(ra, dec, %s, %s, %s)"],
                                    params = (ra, dec, sr)).order_by('time')

    x = []
    y = []
    y1 = []
    y2 = []
    f = []

    for r in records:
        x.append(r.time)
        if type == 'mag':
            y.append(r.mag)
            y1.append(r.mag - r.mag_err)
            y2.append(r.mag + r.mag_err)
        else:
            y.append(r.flux)
            y1.append(r.flux - r.flux_err)
            y2.append(r.flux + r.flux_err)

        f.append(favor2_filters.get(r.filter.id))

    dpi = 72
    figsize = (size/dpi, 600/dpi)

    fig = Figure(facecolor='white', figsize=figsize)
    ax = fig.add_subplot(111)
    ax.autoscale()

    #ax.plot(x, y, ".")
    ax.scatter(x, y, c=f, marker='.', edgecolors=f)
    ax.vlines(x, y1, y2, colors=f)

    if records: # It is failing if no points are plotted
        ax.xaxis.set_major_formatter(DateFormatter('%Y.%m.%d %H:%M:%S'))

    ax.set_xlabel("Time, UT")

    if type == 'mag':
        ax.set_ylabel("Magnitude")
        ax.invert_yaxis()
    else:
        ax.set_ylabel("Flux, ADU")

    ax.set_title("Lightcurve at (%g, %g), radius %g" % (ra, dec, sr))

    fig.autofmt_xdate()

    # 10% margins on both axes
    ax.margins(0.1, 0.1)

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response

@permission_required_or_403('fweb.access_data')
def secondscale_map(request, ra=0, dec=0, sr=0.01, size=600):
    if request.method == 'POST':
        coords = request.POST.get('coords')
        sr = float(request.POST.get('sr'))
        name, ra, dec = resolve(coords)

        return redirect('/secondscale/map/coords/%g/%g/%g' %(ra, dec, sr))

    if ra and dec:
        ra = float(ra)
        dec = float(dec)
        sr = float(sr) if sr else 0.01

    records = Records.objects.extra(where=["q3c_radial_query(ra, dec, %s, %s, %s)"],
                                    params = (ra, dec, sr)).order_by('time')

    x = []
    y = []
    mag = []
    f = []

    for r in records:
        x.append(r.ra)
        y.append(r.dec)
        mag.append(r.mag)
        f.append(favor2_filters.get(r.filter.id))

    dpi = 72
    figsize = (size/dpi, size/dpi)

    fig = Figure(facecolor='white', figsize=figsize)
    ax = fig.add_subplot(111)

    formatter = ScalarFormatter(useOffset=False)
    ax.xaxis.set_major_formatter(formatter)
    ax.yaxis.set_major_formatter(formatter)

    ax.scatter(x, y, c=f)

    ax.set_xlabel("RA")
    ax.set_ylabel("Dec")

    ax.set_title("Map at (%g, %g), radius %g" % (ra, dec, sr))

    # 10% margins on both axes
    ax.margins(0.1, 0.1)

    canvas = FigureCanvas(fig)
    response = HttpResponse(content_type='image/png')
    canvas.print_png(response)

    return response
