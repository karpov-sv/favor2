#!/usr/bin/env python

import psycopg2
import BaseHTTPServer
from SocketServer import ThreadingMixIn

import os
import sys
import time

import json
import urlparse
import traceback
import posixpath
import fitsimage

def guess_mime_type(path):
    base, ext = posixpath.splitext(path)

    return {'.fits':'application/octet-stream',
            '.fit':'application/octet-stream',
            '.fts':'application/octet-stream',
            '.png':'image/png',
            '.jpg':'image/jpeg',
            '.gif':'image/gif'}.get(ext.lower(), 'application/octet-stream')

class MultiThreadedHTTPServer(ThreadingMixIn, BaseHTTPServer.HTTPServer):
    pass

class BBHandler(BaseHTTPServer.BaseHTTPRequestHandler):
    def send_json_response(self, code=200, **kwargs):
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        json.dump(kwargs, self.wfile)

    def send_text_response(self, text="", code=200):
        self.send_response(code)
        self.send_header('Content-Type', 'text/plain')
        self.end_headers()
        self.wfile.write(text)

    def send_html_response(self, text="", code=200):
        self.send_response(code)
        self.send_header('Content-Type', 'text/html')
        self.end_headers()
        self.wfile.write(text)

    def send_file(self, filename, path="./"):
        fullname = posixpath.join(path, filename)

        mime = guess_mime_type(fullname)
        length = os.path.getsize(fullname)

        f = open(fullname, "r")
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Disposition', 'attachment; filename=' + posixpath.split(filename)[-1])
        self.send_header('Content-Length', length)
        self.end_headers()
        self.wfile.write(f.read())
        f.close()

    def send_preview(self, filename, size=500, path="./"):
        fullname = posixpath.join(path, filename)

        img = fitsimage.FitsImage(fullname, contrast="percentile", contrast_opts={'max_percent':99.5}, scale="linear")
        img.thumbnail((size,size), resample=fitsimage.Image.ANTIALIAS)

        self.send_response(200)
        self.send_header('Content-Type', 'image/jpeg')
        self.end_headers()

        img.save(self.wfile, "JPEG", quality=95)

    def serve_GET(self):
        q = urlparse.urlparse(self.path)
        args = urlparse.parse_qs(q.query)

        # print q
        # print self.headers

        def get_arg(name, default='', required=False):
            if not args.has_key(name):
                if required:
                    raise Exception("Required argument is missing: " + name)
                else:
                    return default

            result = args.get(name, default)

            if type(result) == list:
                result = result[0]

            return result

        # Targets
        if q.path == '/api/create_target':
            ra = float(get_arg('ra', required=True))
            dec = float(get_arg('dec', required=True))
            name = get_arg('tn', required=True)
            obj_type = get_arg('type', '')

            id = gloria.create_target(name=name, ra=ra, dec=dec, type=obj_type)

            self.send_json_response(ret=1, id=id, info="New target created")
        elif q.path == '/api/delete_target':
            id = int(get_arg('id', required=True))
            # Temporary, as it cascades to delete all other structures
            #gloria.delete_target(id=id)
            self.send_json_response(ret=1, info="Target " + str(id) + " deleted")
        elif q.path == '/api/list_targets':
            self.send_json_response(ret=1, targets=gloria.list_targets(), info="Targets in DB")

        # Plans
        elif q.path == '/api/schedule':
            (sid, uuid) = gloria.schedule_target(id = int(get_arg('id', required=True)),
                                                 exposure = float(get_arg('exposure', 10)),
                                                 min_altitude = float(get_arg('min_altitude', 30)),
                                                 max_moon_altitude = float(get_arg('max_moon_altitude', 30)),
                                                 min_moon_distance = float(get_arg('min_moon_distance', 30)),
                                                 filter = get_arg('filter', ''),
                                                 uuid = get_arg('uuid', ''))
            self.send_json_response(ret=1, id=sid, uuid=uuid, info="Target scheduled")
        elif q.path == '/api/approve':
            id = int(get_arg('id', required=True))
            gloria.schedule_approve(id=id)
            self.send_json_response(ret=1, info="Plan " + str(id) + " approved")
        elif q.path == '/api/confirm':
            id = int(get_arg('id', required=True))
            gloria.schedule_confirm(id=id)
            self.send_json_response(ret=1, info="Plan " + str(id) + " confirmed")
        elif q.path == '/api/accept':
            id = int(get_arg('id', required=True))
            gloria.schedule_accept(id=id)
            self.send_json_response(ret=1, info="Plan " + str(id) + " accepted")
        elif q.path == '/api/reject':
            id = int(get_arg('id', required=True))
            gloria.schedule_reject(id=id)
            self.send_json_response(ret=1, info="Plan " + str(id) + " rejected")
        elif q.path == '/api/complete':
            id = int(get_arg('id', required=True))
            gloria.schedule_complete(id=id)
            self.send_json_response(ret=1, info="Plan " + str(id) + " completed")
        elif q.path == '/api/cancel':
            id = int(get_arg('id', required=True))
            gloria.schedule_cancel(id=id)
            self.send_json_response(ret=1, info="Plan " + str(id) + " cancelled")
        elif q.path == '/api/delete':
            id = int(get_arg('id', required=True))
            gloria.schedule_delete(id=id)
            self.send_json_response(ret=1, info="Plan " + str(id) + " deleted")
        elif q.path == '/api/list':
            status = get_arg('status')
            id = get_arg('id')
            self.send_json_response(ret=1, plans=gloria.schedule_list(status=status,id=id), info="Plans in DB")
        elif q.path == '/api/status':
            id = int(get_arg('id', required=True))
            self.send_json_response(ret=1, id=id, status=gloria.plan_status(id=id), info="Status of plan with id = " + str(id))

        # Images
        elif q.path == '/api/list_images':
            id = int(get_arg('id', 0, required=False))
            if id > 0:
                images = gloria.plan_images(id=id)
            else:
                images = gloria.plan_images()

            for image in images:
                if image['filename']:
                    image['url'] = 'http://%s/api/get_image?id=%d' % (self.headers.get('Host'), image['id'])
                    image['preview'] = 'http://%s/api/preview_image?id=%d' % (self.headers.get('Host'), image['id'])

            if id > 0:
                self.send_json_response(ret=1, images=images, info="Images for plan with id = " + str(id))
            else:
                self.send_json_response(ret=1, images=images, info="Images for all plans")
        elif q.path == '/api/add_image':
            id = int(get_arg('id', required=True))
            filename = get_arg('filename')
            url = get_arg('url')
            preview_url = get_arg('preview_url')
            img_type = get_arg('type')
            if not filename and not url:
                raise Exception("Either filename or url should be given for image " + id)
            self.send_json_response(ret=1, id=gloria.image_add(id=id, filename=filename, url=url, preview_url=preview_url, type=img_type), info="Image added to DB")
        elif q.path == '/api/get_image':
            id = int(get_arg('id', 0, required=True))
            self.send_file(filename=gloria.plan_image(id=id), path=gloria.imagepath)
        elif q.path == '/api/preview_image':
            id = int(get_arg('id', 0, required=True))
            size = int(get_arg('size', 500, required=False))

            self.send_preview(filename=gloria.plan_image(id=id), path=gloria.imagepath, size=size)

        # Free-form summary on empty request
        elif q.path == '/':
            self.send_html_response(self.summary())

        # Unknown request
        else:
            self.send_json_response(200, ret=-1, error="Unknown request: " + q.path)

    def do_GET(self):
        try:
            self.serve_GET()
        except Exception, ex:
            self.send_json_response(200, ret=-1, error=ex.message)
            #print >> sys.stderr, 'exception for path', self.path, ': ', ex
            traceback.print_exc()

    def summary(self):
        targets = gloria.list_targets()

        colors = {'advertised' : 'blue',
                  'approved' : 'green',
                  'confirmed' : 'darkgreen',
                  'rejected' : 'red',
                  'accepted' : 'lightgreen',
                  'completed' : 'magenta',
                  'cancelled' : 'darkred'}

        text = "<pre>"
        text += "Targets:\n"
        for t in gloria.list_targets():
            text += "id: %d name: %s ra: %g dec: %g type: %s\n" % (t['id'], t['name'], t['ra'], t['dec'], t['type'])

        text += "\n"
        text += "Plans:\n"
        for t in gloria.schedule_list():
            text += ("id: <a href='/api/list?id=%d'>%d</a> target_id: %d exposure: %g filter: %s uuid: %s status: <font color='%s'>%s</font>\n" %
                     (t['id'], t['id'], t['target_id'], t['exposure'], t['filter'], t['uuid'], colors.get(t['status']), t['status']))

        text += "\n"
        text += "Images:\n"
        for t in gloria.plan_images():
            if t['filename']:
                text += "id: %d plan_id: %d type: %s filename: <a href='/api/get_image?id=%d'>%s</a> <a href='/api/preview_image?id=%d'>preview</a>\n" % (t['id'], t['plan_id'], t['type'], t['id'], t['filename'], t['id'])
            else:
                text += "id: %d plan_id: %d type: %s url: <a href='%s'>%s</a>" % (t['id'], t['plan_id'], t['type'], t['url'], t['url'])
                if t['preview']:
                    text += " <a href='%s'>preview</a>" % t['preview']
                text += "\n"

        text += "</pre>"

        return text

class Gloria:
    imagepath = "./"

    def __init__(self, db="gloria", images="./"):
        self.conn = psycopg2.connect("dbname=" + db)
        self.conn.autocommit = True
        self.imagepath = images

    def query(self, string="", data=(), simplify=True):
        cur = self.conn.cursor()
        cur.execute(string, data)
        try:
            result = cur.fetchall()
            # Simplify the result if it is simple
            if simplify and len(result) == 1:
                if len(result[0]) == 1:
                    return result[0][0]
                else:
                    return result[0]
            else:
                return result
        except:
            # Nothing returned from the query
            return None

    # Targets - i.e. named objects with ra/dec/type
    def create_target(self, name="unknown", ra=0, dec=0, type=""):
        if ra < 0 or ra > 360 or dec < -90 or dec > 90:
            raise Exception("Coordinates out of range")
        return self.query("INSERT INTO targets (name, ra, dec, type) VALUES (%s, %s, %s, %s) RETURNING id;", (name, ra, dec, type))

    def delete_target(self, id=-1):
        if not self.query("DELETE FROM targets WHERE id = %s RETURNING id;", (int(id),)):
            raise Exception("No targets with id = " + str(id))

    def list_targets(self):
        tgts = self.query("SELECT id,name,ra,dec,type FROM targets ORDER BY id;", simplify=False)
        return [{'id':r[0], 'name':r[1], 'ra':r[2], 'dec':r[3], 'type':r[4]} for r in tgts]

    # Plans
    def schedule_target(self, id=-1, exposure=10, min_altitude=30, max_moon_altitude=30, min_moon_distance=30, filter="", uuid=""):
        if exposure < 0:
            raise Exception("Exposure should be positive")

        if uuid:
            sid = self.query("INSERT INTO plans (target_id, exposure, status, min_altitude, max_moon_altitude, min_moon_distance, filter, uuid) VALUES (%s, %s, get_status_id('advertised'), %s, %s, %s, %s, %s) RETURNING id;",
                             (id, exposure, min_altitude, max_moon_altitude, min_moon_distance, filter, uuid))
        else:
            sid = self.query("INSERT INTO plans (target_id, exposure, status, min_altitude, max_moon_altitude, min_moon_distance, filter) VALUES (%s, %s, get_status_id('advertised'), %s, %s, %s, %s) RETURNING id;",
                             (id, exposure, min_altitude, max_moon_altitude, min_moon_distance, filter))

        if not sid:
            raise Exception("No such target")

        uuid = self.schedule_get_uuid(sid)

        return sid, uuid

    def schedule_approve(self, id=-1):
        if not self.query("UPDATE plans SET status = get_status_id('approved') WHERE id = %s RETURNING id;", (int(id),)):
            raise Exception("No plans with id = " + str(id))

    def schedule_confirm(self, id=-1):
        if not self.query("UPDATE plans SET status = get_status_id('confirmed') WHERE id = %s RETURNING id;", (int(id),)):
            raise Exception("No plans with id = " + str(id))

    def schedule_accept(self, id=-1):
        if not self.query("UPDATE plans SET status = get_status_id('accepted') WHERE id = %s RETURNING id;", (int(id),)):
            raise Exception("No plans with id = " + str(id))

    def schedule_reject(self, id=-1):
        if not self.query("UPDATE plans SET status = get_status_id('rejected') WHERE id = %s RETURNING id;", (int(id),)):
            raise Exception("No plans with id = " + str(id))

    def schedule_complete(self, id=-1):
        if not self.query("UPDATE plans SET status = get_status_id('completed') WHERE id = %s RETURNING id;", (int(id),)):
            raise Exception("No plans with id = " + str(id))

    def schedule_cancel(self, id=-1):
        if not self.query("UPDATE plans SET status = get_status_id('cancelled') WHERE id = %s RETURNING id;", (int(id),)):
            raise Exception("No plans with id = " + str(id))

    def schedule_delete(self, id=-1):
        if not self.query("DELETE FROM plans WHERE id = %s RETURNING id;", (int(id),)):
            raise Exception("No plans with id = " + str(id))

    def schedule_list(self, status='', id=0):
        if status:
            plans = self.query("SELECT p.id,p.target_id,p.exposure,get_status(p.status), p.min_altitude, p.max_moon_altitude, p.min_moon_distance, p.filter, t.name, t.ra, t.dec, p.uuid FROM plans p, targets t WHERE p.target_id = t.id AND status=get_status_id(%s) ORDER BY id;", (status,), simplify=False)
        elif id:
            plans = self.query("SELECT p.id,p.target_id,p.exposure,get_status(p.status), p.min_altitude, p.max_moon_altitude, p.min_moon_distance, p.filter, t.name, t.ra, t.dec, p.uuid FROM plans p, targets t WHERE p.target_id = t.id AND p.id=%s ORDER BY id;", (id,), simplify=False)
        else:
            plans = self.query("SELECT p.id,p.target_id,p.exposure,get_status(p.status), p.min_altitude, p.max_moon_altitude, p.min_moon_distance, p.filter, t.name, t.ra, t.dec, p.uuid FROM plans p, targets t WHERE p.target_id = t.id ORDER BY id;", simplify=False)

        return [{'id':r[0], 'target_id':r[1], 'exposure':r[2], 'status':r[3], 'min_altitude':r[4], 'max_moon_altitude':r[5], 'min_moon_distance':r[6], 'filter':r[7], 'name':r[8], 'ra':r[9], 'dec':r[10], 'uuid':r[11]} for r in plans]

    def schedule_get_uuid(self, id=-1):
        uuid = self.query("SELECT uuid FROM plans WHERE id = %s;", (int(id),))
        return uuid

    def plan_status(self, id=-1):
        status = self.query("SELECT get_status(status) FROM plans WHERE id = %s;", (int(id),))
        if not status:
            raise Exception("No plans with id = " + str(id))
        return status

    def plan_images(self, id=-1):
        if(id > 0):
            images = self.query("SELECT id,plan_id,filename,url,preview_url,type FROM images WHERE plan_id = %s ORDER BY id;", (int(id),), simplify=False)
        else:
            images = self.query("SELECT id,plan_id,filename,url,preview_url,type FROM images ORDER BY id;", simplify=False)

        return [{'id':r[0], 'plan_id':r[1], 'filename':r[2], 'url':r[3], 'preview':r[4], 'type':r[5]} for r in images]

    def plan_image(self, id=-1):
        filename = self.query("SELECT filename FROM images WHERE id = %s;", (int(id),))
        if not filename:
            raise Exception("No images with id = " + str(id))
        return filename

    def image_add(self, id=-1, filename='', url='', preview_url='', type='raw'):
        if int(id) > 0:
            if filename:
                return (self.query("SELECT id FROM images WHERE plan_id = %s AND filename = %s AND type = %s", (int(id), filename, type)) or
                        self.query("INSERT INTO images (plan_id, filename, url, preview_url, type) VALUES (%s, %s, %s, %s, %s) RETURNING id;", (int(id), filename, url, preview_url, type)))
            elif url:
                return (self.query("SELECT id FROM images WHERE plan_id = %s AND url=%s AND type = %s", (int(id), url, type)) or
                        self.query("INSERT INTO images (plan_id, filename, url, preview_url, type) VALUES (%s, %s, %s, %s, %s) RETURNING id;", (int(id), filename, url, preview_url, type)))
        else:
            raise Exception("Malformed plan id or filename")

    def summary(self):
        targets = self.list_targets()

        text = "Targets:\n"
        for t in self.list_targets():
            text += "id: %d name: %s ra: %g dec: %g type: %s\n" % (t['id'], t['name'], t['ra'], t['dec'], t['type'])

        text += "\n"
        text += "Plans:\n"
        for t in self.schedule_list():
            text += ("id: %d target_id: %d exposure: %g status: %s min_alt: %g max_moon_alt: %g min_moon_dist: %d filter: %s\n" %
                     (t['id'], t['target_id'], t['exposure'], t['status'],
                      t['min_altitude'], t['max_moon_altitude'], t['min_moon_distance'],
                      t['filter']))

        text += "\n"
        text += "Images:\n"
        for t in self.plan_images():
            text += "id: %d plan_id: %d filename: %s url: %s type: %s\n" % (t['id'], t['plan_id'], t['filename'], t['url'], t['type'])

        return text

if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option('-p', '--port', help='listening port, defaults to 8880', action='store', dest='port', default=8880)
    parser.add_option('-i', '--images', help='path to image storage, defaults to ./images/', action='store', dest='images', default='./images')
    parser.add_option('-d', '--dbname', help='database name, defaults to gloria', action='store', dest='dbname', default='gloria')

    (options,args) = parser.parse_args()

    gloria = Gloria(db=options.dbname, images = options.images)

    httpd = MultiThreadedHTTPServer(("", int(options.port)), BBHandler)
    print "GLORIA bridge started at port", options.port, "using database", options.dbname, "to serve images from", options.images
    httpd.serve_forever()
