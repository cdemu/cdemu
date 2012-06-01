#!/usr/bin/env python
# -*- coding: utf-8 -*-

from os.path import *

from apport.hookutils import *

def add_info(report, ui=None):
    # Add daemon log file and wrapper scripts
    attach_file_if_exists(report, expanduser ('~/.cdemu-daemon.log'), key='DaemonLog')
    attach_file_if_exists(report, '/usr/lib/cdemu-daemon/cdemu-daemon-session.sh', key='DaemonSessionWrapper')
    attach_file_if_exists(report, '/usr/lib/cdemu-daemon/cdemu-daemon-system.sh', key='DaemonSystemWrapper')

    # Report to launchpad bug tracker
    if not apport.packaging.is_distro_package(report['Package'].split()[0]):
        report['CrashDB'] = 'cdemu_daemon'

