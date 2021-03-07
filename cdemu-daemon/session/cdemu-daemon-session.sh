#!/bin/sh
# Starts the CDEmu daemon instance on D-Bus *session* bus. Optional
# configuration (number of devices, audio driver, log file) are read
# from ~/.config/cdemu-daemon

# Default settings
NUM_DEVICES=1
AUDIO_DRIVER=default
LOG_FILE="${HOME}/.cache/cdemu-daemon.log"

# Read the settings
CONFIG_FILE="${HOME}/.config/cdemu-daemon"

[ -f "${CONFIG_FILE}" ] && . "${CONFIG_FILE}"

# Start the daemon
exec cdemu-daemon --ctl-device=/dev/vhba_ctl --bus=session --num-devices=${NUM_DEVICES} --audio-driver=${AUDIO_DRIVER} --logfile="${LOG_FILE}"

