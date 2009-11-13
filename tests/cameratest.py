#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
# ChangeLog:
# 2008-02-20 - Initial Version


import os
import gc
import sys
import time
import threading

import gobject

import gst
import gst.interfaces

loop = gobject.MainLoop()

#pipeline = "omx_camera vstab=1 mode=1 vnf=1 name=cam cam.src ! queue ! fakesink sync=false cam.vidsrc ! video/x-raw-yuv, format=(fourcc)UYVY, width=720, height=480, framerate=30/1 ! queue ! fakesink sync=false"
pipeline = '''
omx_camera name=cam
  cam.src    ! queue ! fakesink sync=false async=false
  cam.imgsrc ! image/jpeg, width=720, height=480 ! queue ! fakesink sync=false async=false
  cam.vidsrc ! video/x-raw-yuv, format=(fourcc)UYVY, width=720, height=480, framerate=30/1 ! queue ! fakesink sync=false async=false
'''

bin = gst.parse_launch(pipeline)


i = 0

def on_timeout():
  global i
  if i == 0:
    print "setting state to paused"
    bin.set_state(gst.STATE_PAUSED)
  elif i == 1:
    print "switching to video mode"
    cam.set_property('mode', 'video')
    print "setting state to playing"
    bin.set_state(gst.STATE_PLAYING)
  elif i == 2:
    print "setting state to paused"
    bin.set_state(gst.STATE_PAUSED)
  elif i == 3:
    print "switching to image mode"
    cam.set_property('mode', 'image')
    print "setting state to playing"
    bin.set_state(gst.STATE_PLAYING)
  i = i + 1
  return False


def on_message(bus, message):
  global bin
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
    if elem == bin:
      print "State Change complete.. triggering next step"
      gobject.timeout_add(10000, on_timeout)


bus = bin.get_bus()
bus.enable_sync_message_emission()
bus.add_signal_watch()
bus.connect('message', on_message)


cam = None;

for elem in bin:
  if elem.get_name().startswith("cam"):
    cam = elem
    break

print "setting state to playing"
bin.set_state(gst.STATE_PLAYING)


loop.run()

