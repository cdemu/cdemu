# cdemu-bash-completion.sh: bash completion for cdemu client
# Copyright (C) 2011-2014 Henrik Stokseth
# Copyright (C) 2014 Rok Mandeljc
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
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
# MA 02110-1301, USA.
#


# **********************************************************************
# *                          Utility functions                         *
# **********************************************************************
# Arguments:
#  $1 = list of values
#  $2 = test value
#
# This function checks if array in $1 contains value in $2 or not, and
# prints "y" or "n", respectively.
_contains ()
{
    local array=$1
    local target=$2

    for element in ${array[@]}; do
        if [ "${element}" = "${target}" ]; then
            echo "y"
            return
        fi
    done

    echo "n"
    return
}


# **********************************************************************
# *                                load                                *
# **********************************************************************
_cdemu_load ()
{
    local flag_options="-h --help --dvd-report-css"
    local argument_options="--password --encoding"

    # Is previous word an option?
    case "${prev}" in
        "--password")
            # User must enter password; no completion
            return 0
            ;;
        "--encoding")
            local encodings=$(iconv --list | sed -e 's/\/\/$//')
            COMPREPLY=( $(compgen -W "${encodings}" -- ${cur}) )
            return 0
            ;;
    esac

    # Grab list of devices from daemon and append "any"
    local devices="$(cdemu status 2>/dev/null | tail -n +3 | cut -d ' ' -f 1) any"
    local device_index=-1

    # Device is set if any of words after command word is a valid device
    # key-word and the word before it is not an option with argument
    for ((i=${command_index}+1; i < ${#words[@]}; i++)); do
        local word=${words[i]}
        if [ $(_contains "${devices[@]}" "${word}") = "y" -a $(_contains "${argument_options}" "${words[i-1]}") = "n" ]; then
            device_index=${i}
            break
        fi
    done

    if [ ${device_index} -eq -1 ]; then
        COMPREPLY=( $(compgen -W "${flag_options} ${argument_options} ${devices}" -- ${cur}) )
    elif [ ${device_index} -ge ${cword} ]; then
        COMPREPLY=( $(compgen -W "${flag_options} ${argument_options}" -- ${cur}) )
    else
        _filedir
    fi

    return 0
}


# **********************************************************************
# *                            create-blank                            *
# **********************************************************************
_cdemu_create_blank ()
{
    local flag_options="-h --help"
    local argument_options="--param"

    # Is previous word an option?
    case "${prev}" in
        "--param")
            # User must enter parameter; no completion
            return 0
            ;;
        "--writer-id")
            local writers="$(cdemu enum-writers 2>/dev/null | tail -n +2 | cut -d ':' -f 1)"
            COMPREPLY=( $(compgen -W "${writers}" -- ${cur}) )
            return 0
            ;;
        "--medium-type")
            local medium_types="cdr74 cdr80 cdr90 cdr99 dvd+r"
            COMPREPLY=( $(compgen -W "${medium_types}" -- ${cur}) )
            return 0
            ;;
    esac

    # Grab list of devices from daemon and append "any"
    local devices="$(cdemu status 2>/dev/null | tail -n +3 | cut -d ' ' -f 1) any"
    local device_index=-1

    local writer_id_valid=0
    local medium_type_valid=0

    # Device is set if any of words after command word is a valid device
    # key-word and the word before it is not an option with argument. At
    # the same time, check if --writer-id and --medium-type are given.
    for ((i=${command_index}+1; i < ${#words[@]}; i++)); do
        local word=${words[i]}

        if [ "${word}" = "--writer-id" ]; then
            writer_id_valid=1
        fi

        if [ "${word}" = "--medium-type" ]; then
            medium_type_valid=1
        fi

        if [ $(_contains "${devices[@]}" "${word}") = "y" -a $(_contains "${argument_options[@]} --writer-id --medium-type" "${words[i-1]}") = "n" ]; then
            device_index=${i}
            break
        fi
    done

    local mandatory_arguments=""
    if [ ${writer_id_valid} -eq 0 ]; then
        mandatory_arguments="$mandatory_arguments --writer-id"
    fi
    if [ ${medium_type_valid} -eq 0 ]; then
        mandatory_arguments="$mandatory_arguments --medium-type"
    fi

    if [ ${device_index} -eq -1 ]; then
        COMPREPLY=( $(compgen -W "${flag_options} ${argument_options} ${mandatory_arguments} ${devices}" -- ${cur}) )
    elif [ ${device_index} -ge ${cword} ]; then
        COMPREPLY=( $(compgen -W "${flag_options} ${argument_options} ${mandatory_arguments}" -- ${cur}) )
    else
        _filedir
    fi

    return 0
}

# **********************************************************************
# *                           generic command                          *
# **********************************************************************
# Arguments:
#  $1 = list of valid auto-completion values for positional argument
#  $2 = list of valid auto-completion values for post-positional argument
#
# This function handles three classes of cdemu commands:
#  a) functions without any additional arguments (e.g., cdemu status);
#     in this case, no arguments should be passed to the function
#  b) functions where we have a positional argument at the end
#     (e.g., cdemu unload all); in this case, $1 should contain valid
#     values for positional argument. This signature should be also used
#     for option getting (e.g., cdemu daemon-debug-mask 0) and option
#     setting functions where we cannot auto-complete the option value
#     (e.g., cdemu daemon-debug-mask 0 0x01, or cdemu device-id 0 A B C D).
#  c) functions where we have a positional argument at the end, but we
#     can also auto-complete the value of option that comes after positional
#     argument; in this case $2 should contain the valid values. This
#     signature can be used for option setting functions where option is
#     boolean (e.g., cdemu dpm-emulation 0 true).
#
# In all three above cases, it is assumed that commands take no additonal
# argument parameters, and that only flag parameter it "-h"/"--help", which
# must appear before the positional argument.
_cdemu_command_generic ()
{
    local flag_options="-h --help"
    local argument_options=""

    # No positional arguments given?
    if [ $# -eq 0 ]; then
        COMPREPLY=( $(compgen -W "${flag_options} ${argument_options}" -- ${cur}) )
        return 0
    fi

    # Check if first positional keyword is set; i.e., if any of the words
    # after the command key-word is a valid positional key-word and is not
    # a value to argument option
    local positional="$1"
    local positional_index=-1
    for ((i=${command_index}+1; i < ${#words[@]}; i++)); do
        local word=${words[i]}
        if [ $(_contains "${positional[@]}" "${word}") = "y" -a $(_contains "${argument_options}" "${words[i-1]}") = "n" ]; then
            positional_index=${i}
            break
        fi
    done

    if [ ${positional_index} -eq -1 ]; then
        COMPREPLY=( $(compgen -W "${flag_options} ${argument_options} ${positional}" -- ${cur}) )
    elif [ ${positional_index} -ge ${cword} ]; then
        COMPREPLY=( $(compgen -W "${flag_options} ${argument_options} $2" -- ${cur}) )
    else
        COMPREPLY=( $(compgen -W "$2" -- ${cur}) )
    fi

    return 0
}


# **********************************************************************
# *                                Main                                *
# **********************************************************************
_cdemu()
{
    local cur prev prev2 cword words
    COMPREPLY=()
    _get_comp_words_by_ref cur prev cword words
    prev2="${words[cword - 2]}"

    # Check if we have an active command before word-to-be-completed
    local commands="load create-blank unload status add-device remove-device device-mapping daemon-debug-mask library-debug-mask dpm-emulation tr-emulation bad-sector-emulation device-id enum-parsers enum-writers enum-filter-streams enum-daemon-debug-masks enum-library-debug-masks enum-writer-parameters version"
    local command_index=-1

    for ((i=1; i < ${#words[@]}; i++)); do
        local word=${words[i]}
        for command in ${commands[@]}; do
            if [ "${command}" = "${word}" ]; then
                command_index=${i}
                break
            fi
        done
    done

    # No active command or before command keyword
    if [ ${command_index} -ge ${cword} -o ${command_index} -eq -1 ]; then
        local options="-h --help -b --bus -v --version"

        # Auto-complete --bus option
        case "${prev}" in
            "-b" | "--bus")
                local bus_types="session system"
                COMPREPLY=( $(compgen -W "${bus_types}" -- ${cur}) )
                return 0
                ;;
        esac

        # Complete with options or commands
        if [ ${command_index} -eq -1 ]; then
            COMPREPLY=( $(compgen -W "${options} ${commands}" -- ${cur}) )
        else
            COMPREPLY=( $(compgen -W "${options}" -- ${cur}) )
        fi

        return 0
    fi

    # A command is active, so process its specific inputs
    case "${words[${command_index}]}" in
        "load")
            _cdemu_load
            ;;
        "create-blank")
            _cdemu_create_blank
            ;;
        "unload")
            local devices="$(cdemu status 2>/dev/null | tail -n +3 | cut -d ' ' -f 1) all"
            _cdemu_command_generic "${devices}"
            ;;
        "status" | "add-device" | "remove-device" | "device-mapping" | "version")
            _cdemu_command_generic
            ;;
        "daemon-debug-mask" | "library-debug-mask" | "device-id")
            local devices="$(cdemu status 2>/dev/null | tail -n +3 | cut -d ' ' -f 1) all"
            _cdemu_command_generic "${devices}"
            ;;
        "dpm-emulation" | "tr-emulation" | "bad-sector-emulation")
            local devices="$(cdemu status 2>/dev/null | tail -n +3 | cut -d ' ' -f 1) all"
            _cdemu_command_generic "${devices}" "true false"
            ;;
        "enum-parsers" | "enum-writers" | "enum-filter-streams" | "enum-daemon-debug-masks" | "enum-library-debug-masks")
            _cdemu_command_generic
            ;;
        "enum-writer-parameters")
            local writers="$(cdemu enum-writers 2>/dev/null | tail -n +2 | cut -d ':' -f 1)"
            _cdemu_command_generic "${writers}"
            ;;
    esac

    return 0
}


complete -F _cdemu cdemu
