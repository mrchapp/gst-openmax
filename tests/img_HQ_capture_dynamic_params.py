#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# This is a simple script to test robustness in image capture ..  possibly 
# some parts could be common code w/ other test scripts.. but it is quick-n-dirty
# and would be eventually replaced by camerabin.
# 
# Usage:
# 
#         output jpg file -------------------------------------------+
#                                                                    |
#         number of cycles --------------------------------+         |
#                                                          |         |
#         input device ----------------------+             |         |
#                                            |             |         |
#                                   |-----------------| |-----|      v 
# img_HQ_capture_dynamic_params.py   primary/secondary   43200  outputfile.jpg
#


import os
import gc
import sys
import time


input_device = sys.argv[1]
cycles       = int(sys.argv[2])
filename     = sys.argv[3]

#preview_w   = int(sys.argv[1])
#preview_h   = int(sys.argv[2])
#preview_f   = sys.argv[3]
#image_w     = int(sys.argv[4])
#image_h     = int(sys.argv[5])
#image_f     = sys.argv[6]
#filename    = sys.argv[7]
#cam_params  = sys.argv[8]



focus     = [0, 1, 2, 3]
awb       = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
iso       = [0, 100, 200, 400, 800, 1000, 1600]
bright    = [30, 34, 38, 42, 46, 50, 54, 58, 62, 66, 70]
zoom      = [100, 200, 300, 400, 500, 600, 700, 800]


if input_device == "secondary":
  image_w    = [2592, 1296, 864, 2048]
  image_h    = [1944,  972, 648, 1536]
else:
  image_w    = [4032, 3648, 3264, 2584, 2048, 640]
  image_h    = [3024, 2736, 2448, 1936, 1536, 480]

preview_w  = [ 640,  320,  640]
preview_h  = [ 480,  240,  480]
preview_f  = ["NV12", "UYVY"]


focus_idx  = 0
awb_idx    = 0
iso_idx    = 0
bright_idx = 0
zoom_idx   = 0

image_w_idx   = 0
image_h_idx   = 0
preview_w_idx = 0
preview_h_idx = 0
preview_f_idx = 0


awb_interval    = 1
bright_interval = 5
iso_interval    = 7
zoom_interval   = 11
focus_interval  = 19

res_image_interval     = 23
color_preview_interval = 47
res_preview_interval   = 93



cycles_count = 0



def configure():
  global total_time
  global cycles_count
  
  global focus_idx
  global awb_idx
  global iso_idx
  global bright_idx
  global zoom_idx

  global image_w_idx
  global image_h_idx
  global preview_w_idx
  global preview_h_idx
  global preview_f_idx

  global focus
  global awb
  global iso
  global bright
  global zoom

  global image_w
  global image_h
  global preview_w
  global preview_h
  global preview_f


  #Change focus
  if cycles_count % focus_interval == 0:
    focus_idx = (focus_idx + 1) % len(focus)
    #print "[%d] set focus: %d" % (cycles_count, focus[focus_idx])

  #Change awb
  if cycles_count % awb_interval == 0:
    awb_idx = (awb_idx + 1) % len(awb)
    #print "[%d] set awb: %d" % (cycles_count, awb[awb_idx])

  #Change iso
  if cycles_count % iso_interval == 0:
    iso_idx = (iso_idx + 1) % len(iso)
    #print "[%d] set iso: %d" % (cycles_count, iso[iso_idx])

  #Change bright 
  if cycles_count % bright_interval == 0:
    bright_idx = (bright_idx + 1) % len(bright)
    #print "[%d] set brightness: %d" % (cycles_count, bright[bright_idx])

  #Change zoom 
  if cycles_count % zoom_interval == 0:
    zoom_idx = (zoom_idx + 1) % len(zoom)
    #print "[%d] set zoom: %d" % (cycles_count, zoom[zoom_idx])

  #Change image resolution
  if cycles_count % res_image_interval == 0:
    image_w_idx = (image_w_idx + 1) % len(image_w)
    image_h_idx = image_w_idx
    #print "[%d] set image resolution: %d x %d" % (cycles_count, image_w[image_w_idx], image_h[image_h_idx] )

  #Change preview resolution
  if cycles_count % res_preview_interval == 0:
    preview_w_idx = (preview_w_idx + 1) % len(preview_w)
    preview_h_idx = preview_w_idx
    #print "[%d] set preview resolution: %d x %d" % (cycles_count, preview_w[preview_w_idx], preview_h[preview_h_idx] )

  #Change preview color format 
  if cycles_count % color_preview_interval == 0:
    preview_f_idx = (preview_f_idx + 1) % len(preview_f)
    #print "[%d] set preview_f: %s" % (cycles_count, preview_f[preview_f_idx])

  return False



for i in range(1,cycles):
    cycles_count=i
    configure()

    preview_arguments=" %s %s %s "  %  (preview_w[preview_w_idx], preview_h[preview_h_idx],  preview_f[preview_f_idx])
    image_arguments=" %s %s image/jpeg "  %  (image_w[image_w_idx], image_h[image_h_idx])
    camera_params=" device=%s focus=%s awb=%s iso_speed=%s bright=%s zoom=%s ldc=true nsf=on" % (input_device, focus[focus_idx], awb[awb_idx], iso[iso_idx], bright[bright_idx], zoom[zoom_idx])
    pipeline_arguments=" %s %s %s  %s "  %  (preview_arguments, image_arguments, filename, camera_params)

    print "[%d] >>>:./img_HQ_capture_script.py %s" % (cycles_count, pipeline_arguments)
    os.system("./img_HQ_capture_script.py %s" % pipeline_arguments)

    i+=1


	
	
