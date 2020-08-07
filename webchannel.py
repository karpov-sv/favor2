#!/usr/bin/env python

from twisted.internet.protocol import Protocol, ReconnectingClientFactory
from twisted.internet.task import LoopingCall
from twisted.web.server import Site
from twisted.web.resource import Resource
from twisted.web.static import File

from twistedauth import wrap_with_auth as Auth

from webchannel_plots import *

from moxa import resetAllMoxas

import sys
import urlparse
import json

from PIL import Image
from StringIO import StringIO

from gui import immp
from mount import resolve

class Channel:
    def __init__(self):
        self.channel_status = {}
        self.hw_status = {}
        self.image = None

class Mount:
    def __init__(self):
        self.status = {}

class Beholder:
    def __init__(self):
        self.status = {}
        self.nchannels = 0
        self.nmounts = 0
        self.current_frames = {}
        self.current_id = 0

class Scheduler:
    def __init__(self):
        self.status = {}
        self.suggestion = {}

# Socket part

class Favor2Protocol(Protocol):
    refresh = 1

    def connectionMade(self):
        self.buffer = ''
        self.is_binary = False
        self.binary_length = 0

        self.factory.clients.append(self)

        LoopingCall(self.requestStatus).start(self.refresh)

    def connectionLost(self, reason):
        self.factory.clients.remove(self)

    def message(self, string):
        #print ">>", string
        self.transport.write(string)
        self.transport.write("\0")

    def dataReceived(self, data):
        self.buffer = self.buffer + data

        while len(self.buffer):
            if self.is_binary:
                if len(self.buffer) >= self.binary_length:
                    bdata = self.buffer[:self.binary_length]
                    self.buffer = self.buffer[self.binary_length:]

                    self.processBinary(bdata)
                    self.is_binary = False
                else:
                    break
            else:
                try:
                    token, self.buffer = self.buffer.split('\0', 1)
                    self.processMessage(token)
                except ValueError:
                    break

    def processMessage(self, string):
        print ">", string

    def processBinary(self, data):
        print "binary>", len(data), "bytes"

    def requestStatus(self):
        pass

class ChannelProtocol(Favor2Protocol):
    def connectionMade(self):
        self.factory.object.image = None
        Favor2Protocol.connectionMade(self)

    def connectionLost(self, reason):
        self.factory.object.image = None
        Favor2Protocol.connectionLost(self, reason)

    def requestStatus(self):
        self.message("get_channel_status")
        self.message("get_current_frame")

    def processMessage(self, string):
        command = immp.parse(string)

        if command.name() == 'current_frame':
            self.is_binary = True
            self.binary_length = int(command.kwargs.get('length', 0))
            self.image_format = command.kwargs.get('format', '')
        elif command.name() == 'channel_status':
            self.factory.object.channel_status = command.kwargs

    def processBinary(self, data):
        self.factory.object.image = data
        self.factory.object.image_format = self.image_format

class HWProtocol(Favor2Protocol):
    def requestStatus(self):
        self.message("get_hw_status")

    def processMessage(self, string):
        command = immp.parse(string)

        if command.name() == 'hw_status':
            self.factory.object.hw_status = command.kwargs

class MountProtocol(Favor2Protocol):
    def requestStatus(self):
        self.message("get_mount_status")

    def processMessage(self, string):
        command = immp.parse(string)

        if command.name() == 'mount_status':
            self.factory.object.status = command.kwargs

class BeholderProtocol(Favor2Protocol):
    frame_refresh = 1

    def connectionMade(self):
        Favor2Protocol.connectionMade(self) # Superclass method
        self.factory.object.nchannels = 0
        self.factory.object.current_frames = {}

        LoopingCall(self.requestFrames).start(self.frame_refresh)

    def requestStatus(self):
        self.message("get_beholder_status")

    def requestFrames(self):
        for i in xrange(self.factory.object.nchannels):
            self.message("get_current_frame id=%d" % (i + 1))

    def processMessage(self, string):
        command = immp.parse(string)

        if command.name() == 'beholder_status':
            self.factory.object.status = command.kwargs
            nchannels = int(command.kwargs.get('nchannels', 0))
            nmounts = int(command.kwargs.get('nmounts', 0))

            if nchannels != self.factory.object.nchannels:
                self.factory.object.nchannels = nchannels
                self.factory.object.current_frames = {}
                for i in xrange(nchannels):
                    self.factory.object.current_frames[i+1] = ""

            self.factory.object.nmounts = nmounts
        elif command.name() == 'current_frame':
            self.is_binary = True
            self.binary_length = int(command.kwargs.get('length', 0))
            self.factory.object.current_id = int(command.kwargs.get('id', 0))
        elif command.name() == 'get_current_frame_error':
            self.factory.object.current_frames[int(command.kwargs.get('id', 0))] = ''

    def processBinary(self, data):
        self.factory.object.current_frames[self.factory.object.current_id] = data

class SchedulerProtocol(Favor2Protocol):
    refresh = 10

    def requestStatus(self):
        self.message("get_scheduler_status")
        self.message("get_pointing_suggestion")

    def processMessage(self, string):
        command = immp.parse(string)

        if command.name() == 'scheduler_status':
            self.factory.object.status = command.kwargs
        if command.name() == 'scheduler_pointing_suggestion':
            self.factory.object.suggestion = command.kwargs

class Favor2ClientFactory(ReconnectingClientFactory):
#    factor = 1 # Disable exponential growth of reconnection delay
    maxDelay = 2

    def __init__(self, factory, object=None):
        self.object = object
        self.protocolFactory = factory
        self.clients = []

    def buildProtocol(self, addr):
        print 'Connected to %s:%d' % (addr.host, addr.port)

        p = self.protocolFactory()
        p.factory = self

        return p

    def clientConnectionLost(self, connector, reason):
        print 'Disconnected from %s:%d' % (connector.host, connector.port)
        ReconnectingClientFactory.clientConnectionLost(self, connector, reason)

    def isConnected(self):
        return len(self.clients) > 0

    def message(self, string):
        for c in self.clients:
            c.message(string)

# Web part

def serve_json(request, **kwargs):
    request.responseHeaders.setRawHeaders("Content-Type", ['application/json'])
    return json.dumps(kwargs)

class WebChannel(Resource):
    isLeaf = True

    def __init__(self, channel):
        self.channel = channel

    def render_GET(self, request):
        q = urlparse.urlparse(request.uri)
        args = urlparse.parse_qs(q.query)

        if q.path == '/channel/status':
            return serve_json(request,
                              channel_connected = self.channel.channel_factory.isConnected(),
                              channel = self.channel.channel_status,
                              hw_connected = self.channel.hw_factory.isConnected(),
                              hw = self.channel.hw_status)
        elif q.path == '/channel/command':
            self.channel.channel_factory.message(args['string'][0])
            return serve_json(request)
        elif q.path == '/channel/hw_command':
            self.channel.hw_factory.message(args['string'][0])
            return serve_json(request)
        elif q.path == '/channel/image.jpg':
            if self.channel.image:
                request.responseHeaders.setRawHeaders("Content-Type", ['image/jpeg'])
                request.responseHeaders.setRawHeaders("Content-Length", [len(self.channel.image)])
                return self.channel.image
            else:
                request.setResponseCode(400)
                return "No images"
        else:
            return q.path

class WebMount(Resource):
    isLeaf = True

    def __init__(self, mount):
        self.mount = mount

    def render_GET(self, request):
        q = urlparse.urlparse(request.uri)
        args = urlparse.parse_qs(q.query)

        if q.path == '/mount/status':
            return serve_json(request,
                              mount_connected = self.mount.factory.isConnected(),
                              mount = self.mount.status)
        elif q.path == '/mount/command':
            command = args['string'][0]

            if command.split()[0] in ['tracking_start', 'tracking_stop', 'stop', 'park', 'repoint', 'fix', 'init', 'set_motors']:
                self.mount.factory.message(command)
            else:
                name,ra,dec = resolve(command)

                if name:
                    self.mount.factory.message("repoint %g %g" % (ra, dec))
                else:
                    self.mount.factory.message(command)

            return serve_json(request)

class WebBeholder(Resource):
    isLeaf = True

    def __init__(self, beholder, scheduler):
        self.beholder = beholder
        self.scheduler = scheduler

    def render_GET(self, request):
        q = urlparse.urlparse(request.uri)
        args = urlparse.parse_qs(q.query)

        if q.path == '/beholder/status':
            return serve_json(request,
                              beholder_connected = self.beholder.factory.isConnected(),
                              beholder = self.beholder.status)
        elif q.path == '/beholder/command':
            command = args['string'][0]
            cname = command.split()[0]

            if cname == 'repoint_mounts':
                name,ra,dec = resolve(" ".join(command.split()[1:]))
                self.beholder.factory.message("broadcast_mounts repoint ra=%g dec=%g" % (ra, dec))

            elif cname == 'target_create':
                try:
                    newcommand=[]
                    for token in command.split():
                        if token.split('=')[0] == 'coords':
                            name,ra,dec = resolve(urlparse.unquote(token.split('=')[1]))
                            if name:
                                newcommand.append("ra=%lf dec=%lf" % (ra, dec))
                            else:
                                raise Exception("Can't resolve name")
                        else:
                            newcommand.append(token)

                    self.beholder.factory.message("command_scheduler " + " ".join(newcommand))
                except:
                    pass

            elif cname == 'add_interest':
                try:
                    newcommand=[]
                    for token in command.split():
                        if token.split('=')[0] == 'coords':
                            name,ra,dec = resolve(urlparse.unquote(token.split('=')[1]))
                            if name:
                                newcommand.append("ra=%lf dec=%lf" % (ra, dec))
                            else:
                                raise Exception("Can't resolve name")
                        else:
                            newcommand.append(token)

                    self.scheduler.factory.message("add_interest " + " ".join(newcommand))
                except:
                    pass

            elif cname == 'delete_interest':
                self.scheduler.factory.message(command)

            elif cname == 'reset_moxas':
                resetAllMoxas()
            else:
                self.beholder.factory.message(command)

            return serve_json(request)
        elif q.path == '/beholder/current_frame' or q.path == '/beholder/current_frame.jpg':
            id = int(args['id'][0])
            if id > 0 and id <= self.beholder.nchannels and self.beholder.current_frames[id]:
                request.responseHeaders.setRawHeaders("Content-Type", ['image/jpeg'])
                request.responseHeaders.setRawHeaders("Content-Length", [len(self.beholder.current_frames[id])])
                request.responseHeaders.setRawHeaders("Cache-Control", ['no-store, no-cache, must-revalidate, max-age=0'])
                return self.beholder.current_frames[id]
            else:
                request.setResponseCode(400)
                return "No image for channel %d" % (id)
        elif q.path == '/beholder/small_frame' or q.path == '/beholder/small_frame.jpg':
            id = int(args['id'][0])
            if id > 0 and id <= self.beholder.nchannels and self.beholder.current_frames[id]:
                im = Image.open(StringIO(self.beholder.current_frames[id]))
                im.thumbnail([i/2 for i in im.size], resample=Image.ANTIALIAS)
                s = StringIO()
                im.save(s, "JPEG")

                request.responseHeaders.setRawHeaders("Content-Type", ['image/jpeg'])
                request.responseHeaders.setRawHeaders("Content-Length", [s.len])
                request.responseHeaders.setRawHeaders("Cache-Control", ['no-store, no-cache, must-revalidate, max-age=0'])
                return s.getvalue()
            else:
                request.setResponseCode(400)
                return "No image for channel %d" % (id)
        elif q.path == '/beholder/sky':
            s = StringIO()
            beholder_plot_status(s, status=self.beholder.status)

            request.responseHeaders.setRawHeaders("Content-Type", ['image/png'])
            request.responseHeaders.setRawHeaders("Content-Length", [s.len])
            request.responseHeaders.setRawHeaders("Cache-Control", ['no-store, no-cache, must-revalidate, max-age=0'])
            return s.getvalue()

class WebScheduler(Resource):
    isLeaf = True

    def __init__(self, scheduler):
        self.scheduler = scheduler

    def render_GET(self, request):
        q = urlparse.urlparse(request.uri)
        args = urlparse.parse_qs(q.query)

        if q.path == '/scheduler/status':
            return serve_json(request,
                              scheduler_connected = self.scheduler.factory.isConnected(),
                              scheduler = self.scheduler.status,
                              suggestion = self.scheduler.suggestion)
        elif q.path == '/scheduler/command':
            self.scheduler.factory.message(args['string'][0])
            return serve_json(request)

# Main
if __name__ == '__main__':
    channel = Channel()
    mount = Mount()
    beholder = Beholder()
    scheduler = Scheduler()

    channel.channel_factory = Favor2ClientFactory(ChannelProtocol, channel)
    channel.hw_factory = Favor2ClientFactory(HWProtocol, channel)

    mount.factory = Favor2ClientFactory(MountProtocol, mount)

    beholder.factory = Favor2ClientFactory(BeholderProtocol, beholder)
    scheduler.factory = Favor2ClientFactory(SchedulerProtocol, scheduler)

    from twisted.internet import reactor
    reactor.connectTCP('localhost', 5555, channel.channel_factory)
    reactor.connectTCP('localhost', 5556, channel.hw_factory)
    reactor.connectTCP('localhost', 5562, mount.factory)
    reactor.connectTCP('localhost', 5560, beholder.factory)
    reactor.connectTCP('localhost', 5557, scheduler.factory)

    passwd = {'favor':'favor'}

    # Serve files from web
    root = File("web")
    # ...with some other paths handled also
    root.putChild("", File('web/webchannel.html'))
    root.putChild("beholder.html", File('web/webchannel.html'))
    root.putChild("channel.html", File('web/webchannel.html'))
    root.putChild("mount.html", File('web/webchannel.html'))
    root.putChild("scheduler.html", File('web/webchannel.html'))

    root.putChild("channel", WebChannel(channel))
    root.putChild("mount", WebMount(mount))
    root.putChild("beholder", WebBeholder(beholder, scheduler))
    root.putChild("scheduler", WebScheduler(scheduler))

    #site = Site(Auth(root, passwd))
    site = Site(root)

    reactor.listenTCP(8888, site)

    reactor.run()
