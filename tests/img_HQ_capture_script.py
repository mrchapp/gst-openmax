#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# This is a simple script to test image capture..  possibly some parts could
# be common code w/ other test scripts.. but it is quick-n-dirty and would
# be eventually replaced by camerabin, so I didn't feel like making it fancy
# 
# Usage:
# 
#         camera parameters (e.g. vstab=false vnf=off scene=4 awb=5 ) ------------------------+
#                                                                                             |
#         output jpg file (use multifilesyntax for burst mode) ------------+                  |
#                                                                          |                  |
#         img capture port w/h/format -------------------+                 |                  |
#                                                        |                 |                  |
#         preview port w/h/fourcc ---+                   |                 |                  |
#                                    |                   |                 |                  |
#                              |------------||--------------------|        v         |-----------------|    
#     img_HQ_capture_script.py  640 480 UYVY  1920 1080 image/jpeg   resultfile.jpg   omx_camera params
#

import os
import gc
import sys
import time
import threading

import gobject

import gst
import gst.interfaces

loop = gobject.MainLoop()

preview_w   = int(sys.argv[1])
preview_h   = int(sys.argv[2])
preview_f   = sys.argv[3]
image_w     = int(sys.argv[4])
image_h     = int(sys.argv[5])
image_f     = sys.argv[6]
filename    = sys.argv[7]
cam_params  = sys.argv[8]

pipeline = gst.parse_launch('''
omx_camera name=cam output-buffers=4 shutter=off image-output-buffers=1 mode=image  %s
  cam.src ! ( queue ! video/x-raw-yuv-strided, format=(fourcc)%s, width=%d, height=%d, framerate=30/1, buffer-count-requested=6 ! v4l2sink device=/dev/video2   sync=false )
  cam.imgsrc ! ( name=imgbin queue ! %s, width=%d, height=%d ! multifilesink location=%s )
''' % (cam_params, preview_f, preview_w, preview_h, image_f, image_w, image_h, filename))


def shutdown():
  print "finishing up"
  pipeline.send_event(gst.event_new_eos())
  cam.set_property('focus', 'off')
  pipeline.set_state(gst.STATE_NULL)
  loop.quit()
  return False

def start_capture():
  print "switching to image HQ mode"
  cam.set_property('mode', 'image')
  cam.set_property('shutter', 'full-press')
  pipeline.add(imgbin)
  cam.get_pad('imgsrc').link(imgbin.get_pad('ghost1'))
  gobject.timeout_add(7000, shutdown)
  return False


def on_message(bus, message):
  global pipeline
  t = message.type
  if t == gst.MESSAGE_ERROR:
    err, debug = message.parse_error()
    print "Error: %s" % err, debug
  elif t == gst.MESSAGE_EOS:
    print "eos"
  elif t == gst.MESSAGE_STATE_CHANGED:
    oldstate, newstate, pending = message.parse_state_changed()
    elem = message.src
    print "State Changed: %s: %s --> %s" % (elem, oldstate.value_name, newstate.value_name)
    if elem == pipeline:
      if newstate == gst.STATE_PLAYING:
        print "State Change complete.. continue in preview for 7s"
        gobject.timeout_add(7000, start_capture)
        focus_value=int(cam.get_property('focus'))
        print "camera focus=%s " % (focus_value) 
        if focus_value == 1:
          cam.set_property('focus', 'auto')

bus = pipeline.get_bus()
bus.enable_sync_message_emission()
bus.add_signal_watch()
bus.connect('message', on_message)


cam = None

for elem in pipeline:
  name = elem.get_name()
  if name.startswith('cam'):
    cam = elem
  elif name.startswith('imgbin'):
    imgbin = elem

cam.set_property('mode', 'preview')
pipeline.remove(imgbin)

print "setting state to playing"
ret = pipeline.set_state(gst.STATE_PLAYING)
print "setting pipeline to PLAYING: %s" % ret.value_name

ret = imgbin.set_state(gst.STATE_PLAYING)
print "setting imgbin to PLAYING: %s" % ret.value_name

loop.run()

print "b-bye"


