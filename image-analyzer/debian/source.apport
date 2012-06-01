#!/usr/bin/env python
# -*- coding: utf-8 -*-

from apport.hookutils import *

def add_info(report, ui=None):
    # Report to launchpad bug tracker
    if not apport.packaging.is_distro_package(report['Package'].split()[0]):
        report['CrashDB'] = 'cdemu_daemon'

