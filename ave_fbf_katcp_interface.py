#!/usr/bin/env python
"""
Basic KATCP interface for beamformer data capture.

Author: R van Rooyen
Date: 2014-01-03
Modified: 2014-07-12 Separate capture start and stop into 2 functions rx-beam and rx-stop
Modified: 2014-06-29 Modify rx functions to cope with dual beam capture
Modified: 2014-02-20 Update to incorporate bf_decode changes for bf1 capture
"""

# The *with* keyword is standard in Python 2.6, but has to be explicitly imported in Python 2.5
from __future__ import with_statement

from optparse import OptionParser
import datetime, time
import logging, os, sys
import katcp, spead
import Queue
import subprocess

from katcp.kattypes import request, return_reply, Float, Int, Str, Bool

logging.basicConfig(level=logging.ERROR,
                    stream=sys.stderr,
                    format="%(asctime)s - %(name)s - %(filename)s:%(lineno)s - %(levelname)s - %(message)s")

DEBUG=False

class BeamformServer(katcp.DeviceServer):

    ## Interface version information.
    VERSION_INFO = ("Beamformer katcp interface deamon", 0, 1)
    ## Device server build / instance information.
    BUILD_INFO = ("fbf_katcp_interface", 0, 1, 'rc2')


    #pylint: disable-msg=R0904
    def setup_sensors(self):
        pass

    def __init__(self, *args, **kwargs):
        super(BeamformServer, self).__init__(*args, **kwargs)
        self.output = None
        self.data   = None
        self.rx = None
        self.ig = None
        self.cmdenv = {}
        self.proc = None

    @request(Str(default='/data2'), Int(0), Int(0), include_msg=True)
    @return_reply()
    def request_rx_init(self, sock, orgmsg, prefix, fullband, transpose):
        """Initiates receiver on kat-dc2.karoo. Also, verify that output directory is available."""
        self.output = prefix
        self.data   = os.path.join(self.output,'latest') # default output directory
        #expect to see empty default dir /data1/latest
        if not os.path.isdir(self.data):
            try: os.mkdir(self.data, 0777)
            except Exception as e: return ("fail", "... %s" % e)
        elif os.listdir(self.data): return ("fail", 'Directory \'%s\' not empty. Exiting' % (self.data))
        #set environment variables
        os.environ['PREFIX'] = self.data
        os.environ['FULLBAND'] = str(fullband)
        os.environ['TRANPOSE'] = str(transpose)
        return ('ok',)

    @request(Int(default=7150), include_msg=True)
    @return_reply()
    def request_rx_meta_init(self, sock, orgmsg, port):
        """Start meta data receiver and capture observation metadata. Input: port"""
        meta_dest_port = port
        self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'rx port meta: %d\n' % meta_dest_port),orgmsg)
        self.rx = spead.TransportUDPrx(port=meta_dest_port, pkt_count=1024, buffer_size=51200000)
        self.ig = spead.ItemGroup()
        time.sleep(1)
        return ('ok',)

    @request(Str(default=''), include_msg=True)
    @return_reply()
    def request_rx_meta(self, sock, orgmsg, meta_str):
        """Start meta data receiver and capture observation metadata. Input: port"""
        if DEBUG: self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'Capturing heaps...\n'),orgmsg)
        sync_time = False
        timestamp_scale = False
        done = False
        for heap in spead.iterheaps(self.rx):
          if DEBUG: self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'Got heap: %d\n' % self.ig.heap_cnt),orgmsg)
          self.ig.update(heap)
          if DEBUG: self.reply_inform(sock, katcp.Message.inform(orgmsg.name,"PROCESSING HEAP cnt(%i) @ %.4f\n" % (heap.heap_cnt, time.time())),orgmsg)
          for name in self.ig.keys():
            item = self.ig.get_item(name)
            if name == 'sync_time':
              if DEBUG: self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'%s = %d\n' % (str(item), self.ig['sync_time'])),orgmsg)
              sync_time = True
            if name == 'scale_factor_timestamp':
              timestamp_scale = True
            if sync_time and timestamp_scale:
              done=True
              break
          if done: break
        ## Stop meta data receiver
        self.rx.stop()
        if DEBUG: self.reply_inform(sock, katcp.Message.inform(orgmsg.name,"Files and sockets closed.\n"),orgmsg)

        with open(os.path.join(self.data,'obs_info.dat'),'w') as f:
          f.write('seconds since sync = sync_time + <packet timestamp>/scale_factor_timestamp\n')
          f.write('sync_time: %d\n' % self.ig['sync_time'])
          f.write('scale_factor_timestamp: %f\n' % self.ig['scale_factor_timestamp'])
          if len(meta_str) > 0:
              for item in meta_str.split(';'):
                  f.write('%s\n' % item)
        return ('ok',)

    @request(Int(default=7150), Str(''), include_msg=True)
    @return_reply()
    def request_rx_beam(self, sock, orgmsg, port, pol):
        """Start beamformer data receiver and capture observation data. Input: port, polarization"""
        print "TESTING 12 123"
        if self.proc: raise RuntimeError('Process instance already running')
        udp_dest_port = port
        import os
        self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'rx port: %d\n' % udp_dest_port),orgmsg)
        ## initiate remote data receiver
        if DEBUG: self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'opening subprocess\n'),orgmsg)
        self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'rx pol: %s\n' % pol),orgmsg)

        if pol == 'v':
            cpus = '3,5,7,11,13,15'
            os.environ['DATA_SPEAD_ID'] = "45057"
        else:
            cpus = '2,4,6,10,12,14'
            os.environ['DATA_SPEAD_ID'] = "45056"
        self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'rx cpus: %s\n' % cpus),orgmsg)
        cmdarray  = [
                   'taskset',
                   '-c', cpus,
                   'speadrx',
                   '-w4',
                   '-d', '/home/kat/Chris/incoherent_beamformer/spead_callback/ave_beamformer.so',
                   '-p', str(udp_dest_port)
                   ]

        # print cmdarray
        self.proc = subprocess.Popen(cmdarray)
        if DEBUG: self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'wait on subprocess\n'),orgmsg)
        time.sleep(1)
        if DEBUG: self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'%s\n' % datetime.datetime.now().strftime("%Y%m%d-%H:%M:%S")),orgmsg)
        return ('ok',)

    @request(include_msg=True)
    @return_reply()
    def request_rx_stop(self, sock, orgmsg):
        """Kills all speadrx instances and stops data receivers."""
#         if DEBUG: self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'sleeping for %f secs\n' % target_duration),orgmsg)
#         time.sleep(target_duration) # sleep time must be 7 or so seconds longer than target track time
        if DEBUG: self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'%s\n' % datetime.datetime.now().strftime("%Y%m%d-%H:%M:%S")),orgmsg)
        ## teardown of receiver and closing of files
        self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'teardown\n'),orgmsg)
#         time.sleep(10)
        self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'terminating\n'),orgmsg)
        # Killing proc leaves orphans, thus killall
        os.system('killall speadrx')
        time.sleep(10)
        self.proc = None
        return ('ok',)

    @request(include_msg=True)
    @return_reply(Str())
    def request_rx_close(self, sock, orgmsg):
        """Close receiver on kat-dc2.karoo. Also, moves data to dated output directory."""
        ## Stop meta data receiver
        try: self.rx.stop()
        except: pass # no receivers running
        ## Create output directory and point data capture script to it
        output = os.path.join(self.output, datetime.datetime.now().strftime("%Y%m%d-%H_%M_%S"))
        if DEBUG: self.reply_inform(sock, katcp.Message.inform(orgmsg.name,'moving data\n'),orgmsg)
        os.rename(self.data, output)
        return ('ok','Beamformer data written to: %s\n' % output)




if __name__ == "__main__":

    usage = "usage: %prog [options]"
    parser = OptionParser(usage=usage)
    parser.add_option('-a', '--host', dest='host', type="string", default="", metavar='HOST',
                      help='listen to HOST (default="" - all hosts)')
    parser.add_option('-p', '--port', dest='port', type=long, default=7147, metavar='N',
                      help='attach to port N (default=7147)')
    (opts, args) = parser.parse_args()

    print "Server listening on port %d, Ctrl-C to terminate server" % opts.port
    restart_queue = Queue.Queue()
    server = BeamformServer(host=opts.host, port=opts.port)
    server.set_restart_queue(restart_queue)

    server.start()
    print "Started."

    try:
        while True:
            try:
                device = restart_queue.get(timeout=0.5)
            except Queue.Empty:
                device = None
            if device is not None:
                print "Stopping ..."
                device.stop()
                device.join()
                print "Restarting ..."
                device.start()
                print "Started."
    except KeyboardInterrupt:
        print "Shutting down ..."
        server.stop()
        server.join()

# -fin-
