#!/usr/bin/env python3
#
# Utility script for comparing two libMirage-loaded optical disc images
# Copyright (C) 2026 Rok Mandeljc
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

import argparse
import getpass
import sys
import time

import colorama

import gi

if True:
    gi.require_version("GLib", "2.0")
    gi.require_version("Mirage", "3.2")

from gi.repository import GLib
from gi.repository import Mirage


def get_password(message):
    print(message)
    if sys.stdin.isatty():
        return getpass.getpass("Password: ")
    else:
        print("No TTY to ask for password!")

    return None


def buffer_to_hex(buffer):
    return " ".join([
        f"{v:02X}" for v in buffer
    ])


def _fuzzy_compare_sector_type(value_a, value_b):
    def _check_mixed_mode2(a, b):
        return (
            a == Mirage.SectorType.MODE2_MIXED and
            b in {Mirage.SectorType.MODE2_FORM1, Mirage.SectorType.MODE2_FORM2}
        )

    return _check_mixed_mode2(value_a, value_b) or _check_mixed_mode2(value_b, value_a)


def _value_name_medium_type(value):
    _names = {
        Mirage.MediumType.CD: 'CD',
        Mirage.MediumType.DVD: 'DVD',
        Mirage.MediumType.BD: 'BD',
        Mirage.MediumType.HD: 'HD',
        Mirage.MediumType.HDD: 'HDD',
    }
    return _names.get(value, None)


def _value_name_session_type(value):
    _names = {
        Mirage.SessionType.CDDA: 'CDDA',
        Mirage.SessionType.CDROM: 'CDROM',
        Mirage.SessionType.CDI: 'CDI',
        Mirage.SessionType.CDROM_XA: 'CDROM_XA',
    }
    return _names.get(value, None)


def _value_name_sector_type(value):
    _names = {
        Mirage.SectorType.MODE0: 'MODE0',
        Mirage.SectorType.AUDIO: 'AUDIO',
        Mirage.SectorType.MODE1: 'MODE1',
        Mirage.SectorType.MODE2: 'MODE2',
        Mirage.SectorType.MODE2_FORM1: 'MODE2_FORM1',
        Mirage.SectorType.MODE2_FORM2: 'MODE2_FORM2',
        Mirage.SectorType.MODE2_MIXED: 'MODE2_MIXED',
        Mirage.SectorType.RAW: 'RAW',
        Mirage.SectorType.RAW_SCRAMBLED: 'RAW_SCRAMBLED',
    }
    return _names.get(value, None)


def _value_name_track_flags(value):
    _flags = {
        Mirage.TrackFlag.FOURCHANNEL: 'FOURCHANNEL',
        Mirage.TrackFlag.COPYPERMITTED: 'COPYPERMITTED',
        Mirage.TrackFlag.PREEMPHASIS: 'PREEMPHASIS',
    }
    applicable_names = [
        flag_name for flag_value, flag_name in _flags.items()
        if value & flag_value != 0
    ]
    if not applicable_names:
        return None
    return "|".join(applicable_names)


def compare_attribute(object_a, object_b, get_method, name, fuzzy_compare_fn=None, value_name_fn=None):
    value_a = get_method(object_a)
    value_b = get_method(object_b)

    # Compare
    fuzzy = False
    ok = value_a == value_b

    if not ok and fuzzy_compare_fn is not None:
        ok = fuzzy_compare_fn(value_a, value_b)
        fuzzy = ok

    # Try to get descriptive names for enum-based values
    value_name_a = value_name_fn(value_a) if value_name_fn is not None else None
    value_name_b = value_name_fn(value_b) if value_name_fn is not None else None

    # Construct output string
    output = []

    output += [
        colorama.Fore.BLUE,
        f"{name}: ",
        colorama.Style.RESET_ALL,
        f"{value_a}",
        "" if value_name_a is None else f" ({value_name_a})",
    ]

    if ok:
        if fuzzy:
            # Fuzzy match; display the second value with added [OK]
            output += [
                f" ~= {value_b}",
                "" if value_name_b is None else f" ({value_name_b})",
                colorama.Fore.GREEN,
                " [OK]",
            ]
        else:
            # Strict match; just display status
            output += [
                colorama.Fore.GREEN,
                " [OK]",
            ]
    else:
        # Mismatch; display the second value in red
        output += [
            colorama.Fore.RED,
            f" != {value_b}",
            "" if value_name_b is None else f" ({value_name_b})",
        ]
    output += [colorama.Style.RESET_ALL]

    print("".join(output))

    return ok


def compare_sectors(sector_a, sector_b, sector_address, check_main_data_only):
    sector_address_a = sector_a.get_address()
    sector_address_b = sector_b.get_address()

    def print_error(message):
        full_message = "".join([
            colorama.Fore.RED,
            f"Failed to compare sectors at address {sector_address}: ",
            colorama.Style.RESET_ALL,
            message
        ])
        print(full_message)

    if sector_address != sector_address_a or sector_address_a != sector_address_b:
        print_error(
            f"Sector address mismatch: expected {sector_address}, "
            f"found {sector_address_a} and {sector_address_b}"
        )
        return False

    # NOTE: sector type should be fully resolved here, so we directly compare
    # the two values.
    sector_type_a = sector_a.get_sector_type()
    sector_type_b = sector_b.get_sector_type()

    if sector_type_a != sector_type_b:
        print_error(f"Sector type mismatch: {sector_type_a} vs {sector_type_b}")
        return False

    # Compare sector data, broken down by parts for easier debugging

    # Sync pattern: all sector types except AUDIO
    if not check_main_data_only and sector_type_a != Mirage.SectorType.AUDIO:
        valid_a, buffer_a = sector_a.get_sync()
        valid_b, buffer_b = sector_b.get_sync()
        if buffer_a != buffer_b:
            print_error(f"Sync pattern data mismatch ({len(buffer_a)} bytes)!")
            print("")
            print("First image:")
            print(buffer_to_hex(buffer_a))
            print("")
            print("Second image:")
            print(buffer_to_hex(buffer_b))
            return False

    # Header: all sector types except AUDIO
    if not check_main_data_only and sector_type_a != Mirage.SectorType.AUDIO:
        valid_a, buffer_a = sector_a.get_header()
        valid_b, buffer_b = sector_b.get_header()
        if buffer_a != buffer_b:
            print_error(f"Header data mismatch ({len(buffer_a)} bytes)!")
            print("")
            print("First image:")
            print(buffer_to_hex(buffer_a))
            print("")
            print("Second image:")
            print(buffer_to_hex(buffer_b))
            return False

    # Subheader: only Mode 2 Form 1/2 sectors
    SECTORS_WITH_SUBHEADER = {
        Mirage.SectorType.MODE2_FORM1,
        Mirage.SectorType.MODE2_FORM2,
    }
    if not check_main_data_only and sector_type_a in SECTORS_WITH_SUBHEADER:
        valid_a, buffer_a = sector_a.get_subheader()
        valid_b, buffer_b = sector_b.get_subheader()
        if buffer_a != buffer_b:
            print_error(f"Subheader data mismatch ({len(buffer_a)} bytes)!")
            print("")
            print("First image:")
            print(buffer_to_hex(buffer_a))
            print("")
            print("Second image:")
            print(buffer_to_hex(buffer_b))
            return False

    # User data: always available
    if True:
        valid_a, buffer_a = sector_a.get_data()
        valid_b, buffer_b = sector_b.get_data()
        if buffer_a != buffer_b:
            print_error(f"Main sector data mismatch ({len(buffer_a)} bytes)!")
            print("")
            print("First image:")
            print(buffer_to_hex(buffer_a))
            print("")
            print("Second image:")
            print(buffer_to_hex(buffer_b))
            return False

    # EDC/ECC: only Mode 1 and Mode 2 Form 1/2 sectors
    SECTORS_WITH_EDC = {
        Mirage.SectorType.MODE1,
        Mirage.SectorType.MODE2_FORM1,
        Mirage.SectorType.MODE2_FORM2,
    }
    if not check_main_data_only and sector_type_a in SECTORS_WITH_EDC:
        valid_a, buffer_a = sector_a.get_edc_ecc()
        valid_b, buffer_b = sector_b.get_edc_ecc()
        if buffer_a != buffer_b:
            print_error(f"EDC/ECC data mismatch ({len(buffer_a)} bytes)!")
            print("")
            print("First image:")
            print(buffer_to_hex(buffer_a))
            print("")
            print("Second image:")
            print(buffer_to_hex(buffer_b))
            return False

    # Subchannel - validate both 16-byte Q and raw 96-byte PW
    if True:
        valid_a, buffer_a = sector_a.get_subchannel(Mirage.SectorSubchannelFormat.Q)
        valid_b, buffer_b = sector_b.get_subchannel(Mirage.SectorSubchannelFormat.Q)
        if buffer_a != buffer_b:
            print_error(f"P/Q subchannel data mismatch ({len(buffer_a)} bytes)!")
            print("")
            print("First image:")
            print(buffer_to_hex(buffer_a))
            print("")
            print("Second image:")
            print(buffer_to_hex(buffer_b))
            return False

    if True:
        valid_a, buffer_a = sector_a.get_subchannel(Mirage.SectorSubchannelFormat.PW)
        valid_b, buffer_b = sector_b.get_subchannel(Mirage.SectorSubchannelFormat.PW)
        if buffer_a != buffer_b:
            print_error(f"P/Q subchannel data mismatch ({len(buffer_a)} bytes)!")
            print("")
            print("First image:")
            print(buffer_to_hex(buffer_a))
            print("")
            print("Second image:")
            print(buffer_to_hex(buffer_b))
            return False

    return True


def compare_discs(disc_a, disc_b, check_sector_data, check_pregaps, check_main_data_only):
    ok = True

    print(colorama.Fore.BLUE + "*** Phase 1: disc layout verification... ***" + colorama.Style.RESET_ALL)
    print("")

    print(colorama.Fore.BLUE + "Comparing disc layout..." + colorama.Style.RESET_ALL)

    ok &= compare_attribute(disc_a, disc_b, Mirage.Disc.get_medium_type, "  Medium type",
                            value_name_fn=_value_name_medium_type)
    ok &= compare_attribute(disc_a, disc_b, Mirage.Disc.layout_get_first_session, "  First session number")
    ok &= compare_attribute(disc_a, disc_b, Mirage.Disc.get_number_of_sessions, "  Number of sessions")
    ok &= compare_attribute(disc_a, disc_b, Mirage.Disc.get_number_of_tracks, "  Number of tracks")
    ok &= compare_attribute(disc_a, disc_b, Mirage.Disc.layout_get_first_track, "  First track number")
    ok &= compare_attribute(disc_a, disc_b, Mirage.Disc.layout_get_start_sector, "  Start sector")
    ok &= compare_attribute(disc_a, disc_b, Mirage.Disc.layout_get_length, "  Layout length")

    # Compare sessions
    print("")
    num_sessions = min(disc_a.get_number_of_sessions(), disc_b.get_number_of_sessions())
    for idx in range(num_sessions):
        print(colorama.Fore.BLUE + f"Comparing session at index {idx}..." + colorama.Style.RESET_ALL)

        session_a = disc_a.get_session_by_index(idx)
        session_b = disc_b.get_session_by_index(idx)

        ok &= compare_attribute(session_a, session_b, Mirage.Session.layout_get_session_number, "  Session number")
        ok &= compare_attribute(session_a, session_b, Mirage.Session.get_session_type, "  Session type",
                                value_name_fn=_value_name_session_type)
        ok &= compare_attribute(session_a, session_b, Mirage.Session.layout_get_first_track, "  First track number")
        ok &= compare_attribute(session_a, session_b, Mirage.Session.layout_get_start_sector, "  Start sector")
        ok &= compare_attribute(session_a, session_b, Mirage.Session.layout_get_length, "  Length")
        ok &= compare_attribute(session_a, session_b, Mirage.Session.get_leadout_length, "  Lead-out length")

        ok &= compare_attribute(session_a, session_b, Mirage.Session.get_mcn, "  MCN")

        # TODO: languages
        print("")

    # Compare tracks
    num_tracks = min(disc_a.get_number_of_tracks(), disc_b.get_number_of_tracks())
    for idx in range(num_tracks):
        print(colorama.Fore.BLUE + f"Comparing track at index {idx}..." + colorama.Style.RESET_ALL)

        track_a = disc_a.get_track_by_index(idx)
        track_b = disc_b.get_track_by_index(idx)

        ok &= compare_attribute(track_a, track_b, Mirage.Track.layout_get_session_number, "  Session number")
        ok &= compare_attribute(track_a, track_b, Mirage.Track.layout_get_track_number, "  Track number")
        ok &= compare_attribute(track_a, track_b, Mirage.Track.layout_get_start_sector, "  Start sector")
        ok &= compare_attribute(track_a, track_b, Mirage.Track.get_track_start, "  Track start")
        ok &= compare_attribute(track_a, track_b, Mirage.Track.layout_get_length, "  Length")
        ok &= compare_attribute(track_a, track_b, Mirage.Track.get_sector_type, "  Sector type",
                                fuzzy_compare_fn=_fuzzy_compare_sector_type,  # Additional fuzzy matching!
                                value_name_fn=_value_name_sector_type)
        ok &= compare_attribute(track_a, track_b, Mirage.Track.get_flags, "  Flags",
                                value_name_fn=_value_name_track_flags)
        ok &= compare_attribute(track_a, track_b, Mirage.Track.get_adr, "  Adr")
        ok &= compare_attribute(track_a, track_b, Mirage.Track.get_ctl, "  Ctl")
        ok &= compare_attribute(track_a, track_b, Mirage.Track.get_isrc, "  ISRC")

        # TODO: indices, languages
        print("")

    if not ok:
        print(colorama.Fore.RED + "Failed to validate disc layout!" + colorama.Style.RESET_ALL)
        return False

    # Compare sector data
    # Split this by tracks, to make it easier to follow
    if check_sector_data:
        print(colorama.Fore.BLUE + "Phase 2: verification of sector data..." + colorama.Style.RESET_ALL)
        print("")

        for idx in range(num_tracks):
            track_a = disc_a.get_track_by_index(idx)

            start_sector = track_a.layout_get_start_sector()
            end_sector = start_sector + track_a.layout_get_length()
            pregap_length = track_a.get_track_start()

            if not check_pregaps:
                start_sector += pregap_length

            output = [
                colorama.Fore.BLUE,
                f"Verifying track at index {idx}: {start_sector} ~ {end_sector} (pregap length: {pregap_length})...",
                colorama.Style.RESET_ALL,
            ]
            print("".join(output))

            output = [
                colorama.Fore.BLUE,
                f"Track at index {idx}: ",
                colorama.Style.RESET_ALL,
            ]

            sector_a = Mirage.Sector()
            sector_b = Mirage.Sector()

            ok = True
            for sector_address in range(start_sector, end_sector):
                # Use read_sector() instead of get_sector() to re-use the
                # pre-allocated sector objects.
                disc_a.read_sector(sector_address, sector_a)
                disc_b.read_sector(sector_address, sector_b)

                if not compare_sectors(sector_a, sector_b, sector_address, check_main_data_only):
                    ok = False
                    break

            if ok:
                output += [
                    colorama.Fore.GREEN,
                    " [OK]",
                    colorama.Style.RESET_ALL,
                ]
            else:
                output += [
                    colorama.Fore.RED,
                    " [FAILED]",
                    colorama.Style.RESET_ALL,
                ]

            print("".join(output))
            print("")

        if not ok:
            print(colorama.Fore.RED + "Failed to validate sector data!" + colorama.Style.RESET_ALL)
            return False

    print(colorama.Fore.GREEN + "Done!" + colorama.Style.RESET_ALL)
    return True


def libmirage_log_handler(log_domain, log_level, message):
    print(message)


# In contemporary versions of PyGObject, Mirage.DebugMask is an instance of `enum.EnumType`, and we could iterate
# over it to obtain the entries and their (short) names. In older versions, this is more complicated, and so for
# compatibility reasons, it is easier to keep a manually defined dictionary of short enum names. This way, we also
# do not need to worry about Mirage.DebugMask.ERROR and Mirage.DebugMask.WARNING.
_DEBUG_MASK_ENTRIES = {
    "PARSER": Mirage.DebugMask.PARSER,
    "DISC": Mirage.DebugMask.DISC,
    "SESSION": Mirage.DebugMask.SESSION,
    "TRACK": Mirage.DebugMask.TRACK,
    "SECTOR": Mirage.DebugMask.SECTOR,
    "FRAGMENT": Mirage.DebugMask.FRAGMENT,
    "CDTEXT": Mirage.DebugMask.CDTEXT,
    "STREAM": Mirage.DebugMask.STREAM,
    "IMAGE_ID": Mirage.DebugMask.IMAGE_ID,
    "WRITER": Mirage.DebugMask.WRITER,
}


def debug_mask_to_string(mask):
    mask_names = [
        name for name, value in _DEBUG_MASK_ENTRIES.items()
        if mask & value != 0
    ]
    return '|'.join(mask_names)


def debug_mask_from_string(mask_str):
    # Allow both short names as exposed by GI bindings, and original names with "MIRAGE_DEBUG_" prefix.
    _mapping = _DEBUG_MASK_ENTRIES.copy()
    _mapping.update({
        f"MIRAGE_DEBUG_{name}": value for name, value in _DEBUG_MASK_ENTRIES.items()
    })

    mask = 0
    tokens = mask_str.split('|')
    for token in tokens:
        mask |= _mapping[token]
    return mask


def parse_debug_mask(mask_str):
    # First, try to parse as an integer
    try:
        return int(mask_str, 0)
    except ValueError:
        pass

    try:
        return debug_mask_from_string(mask_str)
    except Exception as e:
        raise ValueError(f"Failed to parse debug mask string {mask_str!r}") from e


def main():
    parser = argparse.ArgumentParser(
        prog='Compare Optical Disc Images',
        description='Compares two libMirage-loaded optical disc images',
    )

    parser.add_argument(
        "--first-image",
        type=str,
        action='append',
        metavar="filename",
        help="Filename of the first disc image. This option might be specified several times for multi-file images.",
    )
    parser.add_argument(
        "--second-image",
        type=str,
        action='append',
        metavar="filename",
        help="Filename of the second disc image. This option might be specified several times for multi-file images.",
    )

    parser.add_argument(
        "--first-image-debug-mask",
        type=lambda x: parse_debug_mask(x),
        metavar="debug_mask",
        default=0,
        help="Debug mask to be used when loading and processing the first image.",
    )
    parser.add_argument(
        "--second-image-debug-mask",
        type=lambda x: parse_debug_mask(x),
        metavar="debug_mask",
        default=0,
        help="Debug mask to be used when loading and processing the first image.",
    )

    parser.add_argument(
        "--check-sector-data",
        action='store_true',
        help="Check and compare sector data.",
    )
    parser.add_argument(
        "--check-pregaps",
        action='store_true',
        help="Compare sector data in pregaps.",
    )
    parser.add_argument(
        "--check-main-data-only",
        action='store_true',
        help="Compare only main sector data.",
    )

    args = parser.parse_args()

    first_image_filenames = args.first_image
    second_image_filenames = args.second_image
    first_image_debug_mask = args.first_image_debug_mask
    second_image_debug_mask = args.second_image_debug_mask

    check_sector_data = args.check_sector_data
    check_pregaps = args.check_pregaps
    check_main_data_only = args.check_main_data_only

    colorama.just_fix_windows_console()

    if not first_image_filenames:
        raise SystemExit("No image filenames provided for first image!")

    if not second_image_filenames:
        raise SystemExit("No image filenames provided for second image!")

    print("Program options:")
    print(" * first image:")
    print(f"    - debug mask: 0x{first_image_debug_mask:08X} ({debug_mask_to_string(first_image_debug_mask)})")
    print(f"    * filename(s) ({len(first_image_filenames)}):")
    for filename in first_image_filenames:
        print(f"       - {filename}")
    print(" * second image:")
    print(f"    - debug mask: 0x{second_image_debug_mask:08X} ({debug_mask_to_string(second_image_debug_mask)})")
    print(f"    * filename(s) ({len(second_image_filenames)}):")
    for filename in second_image_filenames:
        print(f"       - {filename}")
    print(f" - check sector data: {check_sector_data}")
    print(f" - check data in pregaps: {check_pregaps}")
    print(f" - check only main channel data: {check_main_data_only}")
    print("")

    Mirage.initialize()

    LOG_DOMAIN = "libMirage"
    GLib.log_set_handler(
        LOG_DOMAIN,
        GLib.LogLevelFlags.LEVEL_MASK | GLib.LogLevelFlags.FLAG_FATAL | GLib.LogLevelFlags.FLAG_RECURSION,
        libmirage_log_handler,
    )

    # Create separate context for each image, so that debug mask can be set independently
    print("Loading first image...")

    context1 = Mirage.Context()
    context1.set_debug_domain(LOG_DOMAIN)
    context1.set_debug_name("image1")
    context1.set_debug_mask(first_image_debug_mask)
    context1.set_password_function(lambda: get_password("The first image is encrypted."))

    first_disc = context1.load_image(first_image_filenames)

    print("Loading second image...")

    context2 = Mirage.Context()
    context2.set_debug_domain(LOG_DOMAIN)
    context2.set_debug_name("image2")
    context2.set_debug_mask(second_image_debug_mask)
    context2.set_password_function(lambda: get_password("The second image is encrypted."))

    second_disc = context2.load_image(second_image_filenames)

    # Compare
    print("")
    start_time = time.time()
    compare_discs(first_disc, second_disc, check_sector_data, check_pregaps, check_main_data_only)
    elapsed = time.time() - start_time
    print(f"Comparison time: {elapsed:.1f} second(s)")

    Mirage.shutdown()


if __name__ == '__main__':
    main()
