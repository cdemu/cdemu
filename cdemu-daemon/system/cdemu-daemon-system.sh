#!/bin/sh
# Starts the CDEmu daemon instance on D-Bus *system* bus. Optional
# configuration (number of devices, audio driver, log file) are read
# from /etc/sysconfig/cdemu-daemon

# Default settings
NUM_DEVICES=2
AUDIO_DRIVER=null
LOG_FILE=/tmp/cdemu-daemon.log

# Read the settings
CONFIG_FILE=/etc/sysconfig/cdemu-daemon

if [ -f ${CONFIG_FILE} ]; then
    . ${CONFIG_FILE};
fi

# Start the daemon
cdemud --ctl-device=/dev/vhba_ctl --bus=system --num-devices=${NUM_DEVICES} --audio-driver=${AUDIO_DRIVER} --logfile=${LOG_FILE}
