#!/usr/bin/env python
# -*- coding: utf-8 -*-

from os.path import *

from apport.hookutils import *

def add_info(report, ui=None):
    # Add config files
    attach_file_if_exists(report, expanduser ('~/.cdemu-client'), key='LocalConfig')
    attach_file_if_exists(report, '/etc/cdemu-client.conf', key='SystemConfig')

    # Report to launchpad bug tracker
    if not apport.packaging.is_distro_package(report['Package'].split()[0]):
        report['CrashDB'] = 'cdemu_daemon'

