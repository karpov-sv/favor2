from favor2 import Favor2
from postprocess import postprocessor
from StringIO import StringIO
import sys
from satellites_db import process_satellite_dir,update_satcat

from celery import Celery

app = Celery('favor2_celery', backend='db+postgresql://karpov@mmt0/favor2web', broker='amqp://guest@mmt0//')
app.conf.update(CELERY_ACCEPT_CONTENT = ['json'])
app.conf.update(CELERY_TASK_SERIALIZER = 'json')
app.conf.update(CELERY_RESULT_SERIALIZER = 'json')
app.conf.update(CELERY_DEFAULT_QUEUE = 'beholder')
app.conf.update(CELERY_TRACK_STARTED = True)
app.conf.update(CELERY_ACKS_LATE = True)

@app.task
def processObjects(id=0, deltat=0, night='', state='all', reprocess=False, clean=True, preview=False):
    pp = postprocessor()

    # Use default if not provided
    if deltat > 0:
        pp.deltat = deltat

    pp.clean = clean
    pp.preview = preview

    s = StringIO()
    __stdout = sys.stdout
    sys.stdout = s

    pp.processObjects(id=id, reprocess=reprocess, state=state, night=night)

    sys.stdout = __stdout

    return s.getvalue()

@app.task
def mergeMeteors(night='all'):
    pp = postprocessor()

    s = StringIO()
    __stdout = sys.stdout
    sys.stdout = s

    pp.mergeMeteors(night=night)
    pp.filterLaserEvents(night=night)

    sys.stdout = __stdout

    return s.getvalue()

@app.task
def filterLaserEvents(night='all'):
    pp = postprocessor()

    s = StringIO()
    __stdout = sys.stdout
    sys.stdout = s

    pp.filterLaserEvents(night=night)

    sys.stdout = __stdout

    return s.getvalue()

@app.task
def filterSatellites(night='all'):
    pp = postprocessor()

    s = StringIO()
    __stdout = sys.stdout
    sys.stdout = s

    pp.filterSatellites(night=night)

    sys.stdout = __stdout

    return s.getvalue()

@app.task
def filterFlashes(night='all'):
    pp = postprocessor()

    s = StringIO()
    __stdout = sys.stdout
    sys.stdout = s

    pp.filterFlashes(night=night)

    sys.stdout = __stdout

    return s.getvalue()

@app.task
def analyzeMeteors(night='all', reprocess=False):
    pp = postprocessor()

    s = StringIO()
    __stdout = sys.stdout
    sys.stdout = s

    pp.analyzeMeteors(night=night, reprocess=reprocess)

    sys.stdout = __stdout

    return s.getvalue()

@app.task
def processMulticolorMeteors(night='all', reprocess=False):
    pp = postprocessor()

    s = StringIO()
    __stdout = sys.stdout
    sys.stdout = s

    pp.processMulticolorMeteors(night=night, reprocess=reprocess)

    sys.stdout = __stdout

    return s.getvalue()

@app.task(time_limit=120)
def runSqlQuery(query='', readonly=True):
    f = Favor2(readonly=readonly)

    res = f.query(query, simplify=False)

    return res

@app.task
def logMessage(text='', id='news'):
    f = Favor2()
    f.log(text, id=id)
    return True

@app.task
def extractStars(night='all', reprocess=False):
    pp = postprocessor()

    s = StringIO()
    __stdout = sys.stdout
    sys.stdout = s

    pp.extractStars(night=night, reprocess=reprocess)

    sys.stdout = __stdout

    return s.getvalue()

@app.task
def findTransients(night='all', reprocess=False):
    pp = postprocessor()

    s = StringIO()
    __stdout = sys.stdout
    sys.stdout = s

    pp.findTransients(night=night, reprocess=reprocess)

    sys.stdout = __stdout

    return s.getvalue()

@app.task
def markReliableTransients(night='all'):
    pp = postprocessor()

    s = StringIO()
    __stdout = sys.stdout
    sys.stdout = s

    pp.markReliableTransients(night=night)

    sys.stdout = __stdout

    return s.getvalue()

@app.task
def uploadSatellites(dir='/data/elka/WEB'):
    s = StringIO()
    __stdout = sys.stdout
    sys.stdout = s

    process_satellite_dir(dir)
    update_satcat()

    sys.stdout = __stdout

    return s.getvalue()

@app.task
def updateSatcat():
    s = StringIO()
    __stdout = sys.stdout
    sys.stdout = s

    update_satcat()

    sys.stdout = __stdout

    return s.getvalue()

@app.task
def refreshViews():
    f = Favor2()

    res = f.query('REFRESH MATERIALIZED VIEW CONCURRENTLY satellites_view')
    return res
