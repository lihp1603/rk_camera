#!/bin/sh
basepath=$(cd `dirname $0`; pwd)
cam_iq="/etc/cam_iq"
cam_tune="/tmp/isptune"
if [ ! -d "$cam_iq" ]; then
  mkdir "$cam_iq"
fi

if [ ! -d "$cam_tune" ]; then
  mkdir "$cam_tune"
fi

cp $basepath/xml/cam_default.xml $cam_iq
cp $basepath/xml/capcmd.xml /tmp
cp $basepath/CameraHal/build/lib/libcam_hal.so /lib/
cp $basepath/ext_lib/libion.so /lib/
