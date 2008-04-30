#!/bin/bash
#
# This is a little script which takes care of listing CDEmu related information from sysfs.
# To enable verbose output add the "-v" parameter on the command line.
#


# shall we be verbose?
if [ "$1" == "-v" ]; then
	VERBOSE="1"
else
	VERBOSE="0"
fi

# first we need to find the sysfs directory
SYSFS_DIR=`mount -t sysfs | cut -d " " -f 3`
if [ "${SYSFS_DIR}" != "" ]; then
	test "${VERBOSE}" == "1" && echo "Found sysfs mounted at ${SYSFS_DIR}."
else
	echo "Did not find mounted sysfs. Aborting."
	exit 1
fi

# next we search for any vhba device directories
CDEMU_DEV_DIRS=`find "${SYSFS_DIR}/devices/platform/vhba/" -iname "vendor" -printf "%h " 2>/dev/null`
NUM_CDEMU_DEVS=`echo ${CDEMU_DEV_DIRS} | wc -w`

if [ "${NUM_CDEMU_DEVS}" != "0" ]; then
	echo "Found ${NUM_CDEMU_DEVS} VHBA devices."
else
	echo "Did not find any VHBA devices. Aborting."
	exit 2
fi

echo

# now it is time to print out some information
for dev in ${CDEMU_DEV_DIRS}; do
	test "${VERBOSE}" == "1" && echo "Device: ${dev}"

	blockdev=`find "${dev}" -iname "block*" -printf "%f" | cut -d ":" -f 2`
	echo "Block device: ${blockdev}"

	blocknode=`cat "${dev}/block:${blockdev}/dev"`
	test "${VERBOSE}" == "1" && echo "  Major and minor numbers: ${blocknode}"

	gendev=`basename \`readlink "${dev}/generic"\``
	echo "Generic device: ${gendev}"

	gennode=`cat "${dev}/generic/dev"`
	test "${VERBOSE}" == "1" && echo "  Major and minor numbers: ${gennode}"

	echo
done

