/*
 *  CDEmu daemon: Sense keys, ASC/ASCQ combinations
 *  Copyright (C) 2006-2012 Rok Mandeljc
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __CDEMU_SENSE_CONSTANTS_H__
#define __CDEMU_SENSE_CONSTANTS_H__

/* Status codes */
typedef enum {
    GOOD              = 0x00,
    CHECK_CONDITION   = 0x02
} SenseStatus;

/* SENSE KEYs */
typedef enum {
    NO_SENSE          = 0x00,
    RECOVERED_ERROR   = 0x01,
    NOT_READY         = 0x02,
    MEDIUM_ERROR      = 0x03,
    HARDWARE_ERROR    = 0x04,
    ILLEGAL_REQUEST   = 0x05,
    UNIT_ATTENTION    = 0x06,
    DATA_PROTECT      = 0x07,
    BLANK_CHECK       = 0x08,
    VENDOR_SPECIFIC   = 0x09,
    COPY_ABORTED      = 0x0A,
    ABORTED_COMMAND   = 0x0B,
    Obsolete          = 0x0C,
    VOLUME_OVERFLOW   = 0x0D,
    MISCOMPARE        = 0x0E,
    Reserved          = 0x0F
} SenseKey;

/* ASC/ASCQ (Additional Sense Code [Qualifier]) as defined for CD/DVD-ROMs in SPC-3 */
typedef enum {
    ACCESS_DENIED_ACL_LUN_CONFLICT                                 = 0x200B,
    ACCESS_DENIED_ENROLLMENT_CONFLICT                              = 0x2008,
    ACCESS_DENIED_INITIATOR_PENDING_ENROLLED                       = 0x2001,
    ACCESS_DENIED_INVALID_LU_IDENTIFIER                            = 0x2009,
    ACCESS_DENIED_INVALID_MGMT_ID_KEY                              = 0x2003,
    ACCESS_DENIED_INVALID_PROXY_TOKEN                              = 0x200A,
    ACCESS_DENIED_NO_ACCESS_RIGHTS                                 = 0x2002,
    ACK_NAK_TIMEOUT                                                = 0x4B03,
    ASSOCIATED_WRITE_PROTECT                                       = 0x2703,
    ASYMMETRIC_ACCESS_STATE_CHANGED                                = 0x2A06,
    ASYNCHRONOUS_INFORMATION_PROTECTION_ERROR_DETECTED             = 0x4704,
    AUDIO_PLAY_OPERATION_IN_PROGRESS                               = 0x0011,
    AUDIO_PLAY_OPERATION_PAUSED                                    = 0x0012,
    AUDIO_PLAY_OPERATION_STOPPED_DUE_TO_ERROR                      = 0x0014,
    AUDIO_PLAY_OPERATION_SUCCESSFULLY_COMPLETED                    = 0x0013,
    AUXILIARY_MEMORY_OUT_OF_SPACE                                  = 0x5506,
    AUXILIARY_MEMORY_READ_ERROR                                    = 0x1112,
    AUXILIARY_MEMORY_WRITE_ERROR                                   = 0x0C0B,
    BUS_DEVICE_RESET_FUNCTION_OCCURRED                             = 0x2903,
    CANNOT_DECOMPRESS_USING_DECLARED_ALGORITHM                     = 0x110E,
    CANNOT_FORMAT_MEDIUM_INCOMPATIBLE_MEDIUM                       = 0x3006,
    CANNOT_READ_MEDIUM_INCOMPATIBLE_FORMAT                         = 0x3002,
    CANNOT_READ_MEDIUM_UNKNOWN_FORMAT                              = 0x3001,
    CANNOT_WRITE_APPLICATION_CODE_MISMATCH                         = 0x3008,
    CANNOT_WRITE_MEDIUM_INCOMPATIBLE_FORMAT                        = 0x3005,
    CANNOT_WRITE_MEDIUM_UNKNOWN_FORMAT                             = 0x3004,
    CD_CONTROL_ERROR                                               = 0x7300,
    CDB_DECRYPTION_ERROR                                           = 0x2401,
    CHANGED_OPERATING_DEFINITION                                   = 0x3F02,
    CIRC_UNRECOVERED_ERROR                                         = 0x1106,
    CLEANING_CARTRIDGE_INSTALLED                                   = 0x3003,
    CLEANING_FAILURE                                               = 0x3007,
    CLEANING_REQUEST_REJECTED                                      = 0x300A,
    CLEANING_REQUESTED                                             = 0x0017,
    COMMAND_PHASE_ERROR                                            = 0x4A00,
    COMMAND_SEQUENCE_ERROR                                         = 0x2C00,
    COMMANDS_CLEARED_BY_ANOTHER_INITIATOR                          = 0x2F00,
    COMPONENT_DEVICE_ATTACHED                                      = 0x3F04,
    CONDITIONAL_WRITE_PROTECT                                      = 0x2706,
    CANNOT_EXECUTE_SINCE_HOST_CANNOT_DISCONNECT                    = 0x2B00,
    COPY_PROTECTION_KEY_EXCHANGE_FAILURE_AUTHENTICATION_FAILURE    = 0x6F00,
    COPY_PROTECTION_KEY_EXCHANGE_FAILURE_KEY_NOT_ESTABLISHED       = 0x6F02,
    COPY_PROTECTION_KEY_EXCHANGE_FAILURE_KEY_NOT_PRESENT           = 0x6F01,
    COPY_SEGMENT_GRANULARITY_VIOLATION                             = 0x260D,
    COPY_TARGET_DEVICE_DATA_OVERRUN                                = 0x0D05,
    COPY_TARGET_DEVICE_DATA_UNDERRUN                               = 0x0D04,
    COPY_TARGET_DEVICE_NOT_REACHABLE                               = 0x0D02,
    CURRENT_PROGRAM_AREA_IS_EMPTY                                  = 0x2C04,
    CURRENT_PROGRAM_AREA_IS_NOT_EMPTY                              = 0x2C03,
    CURRENT_SESSION_NOT_FIXATED_FOR_APPEND                         = 0x3009,
    DATA_DECRYPTION_ERROR                                          = 0x2605,
    DATA_OFFSET_ERROR                                              = 0x4B05,
    DATA_PHASE_CRC_ERROR_DETECTED                                  = 0x4701,
    DATA_PHASE_ERROR                                               = 0x4B00,
    DECOMPRESSION_CRC_ERROR                                        = 0x110D,
    DEVICE_IDENTIFIER_CHANGED                                      = 0x3F05,
    DEVICE_INTERNAL_RESET                                          = 0x2904,
    DRIVE_REGION_MUST_BE_PERMANENT                                 = 0x6F05,
    REGION_RESET_COUNT_ERROR                                       = 0x6F05,
    ECHO_BUFFER_OVERWRITTEN                                        = 0x3F0F,
    EMPTY_OR_PARTIALLY_WRITTEN_RESERVED_TRACK                      = 0x7204,
    ENCLOSURE_FAILURE                                              = 0x3400,
    ENCLOSURE_SERVICES_CHECKSUM_ERROR                              = 0x3505,
    ENCLOSURE_SERVICES_FAILURE                                     = 0x3500,
    ENCLOSURE_SERVICES_TRANSFER_FAILURE                            = 0x3503,
    ENCLOSURE_SERVICES_TRANSFER_REFUSED                            = 0x3504,
    ENCLOSURE_SERVICES_UNAVAILABLE                                 = 0x3502,
    END_OF_MEDIUM_REACHED                                          = 0x3B0F,
    END_OF_USER_AREA_ENCOUNTERED_ON_THIS_TRACK                     = 0x6300,
    ERASE_FAILURE                                                  = 0x5100,
    ERASE_FAILURE_INCOMPLETE_ERASE_OPERATION_DETECTED              = 0x5101,
    ERROR_DETECTED_BY_THIRD_PARTY_TEMPORARY_INITIATOR              = 0x0D00,
    ERROR_LOG_OVERFLOW                                             = 0x0A00,
    ERROR_READING_ISRC_NUMBER                                      = 0x1110,
    ERROR_READING_UPC_EAN_NUMBER                                   = 0x110F,
    ERROR_TOO_LONG_TO_CORRECT                                      = 0x1102,
    FAILURE_PREDICTION_THRESHOLD_EXCEEDED                          = 0x5D00,
    FAILURE_PREDICTION_THRESHOLD_EXCEEDED_FALSE                    = 0x5DFF,
    FOCUS_SERVO_FAILURE                                            = 0x0902,
    FORMAT_COMMAND_FAILED                                          = 0x3101,
    HARDWARE_WRITE_PROTECTED                                       = 0x2701,
    HEAD_SELECT_FAULT                                              = 0x0904,
    IO_PROCESS_TERMINATED                                          = 0x0006,
    IDLE_CONDITION_ACTIVATED_BY_COMMAND                            = 0x5E03,
    IDLE_CONDITION_ACTIVATED_BY_TIMER                              = 0x5E01,
    ILLEGAL_MODE_FOR_THIS_TRACK                                    = 0x6400,
    IMPLICIT_ASYMMETRIC_ACCESS_STATE_TRANSITION_FAILED             = 0x2A07,
    IMPORT_OR_EXPORT_ELEMENT_ACCESSED                              = 0x2801,
    INCOMPATIBLE_MEDIUM_INSTALLED                                  = 0x3000,
    INCORRECT_COPY_TARGET_DEVICE_TYPE                              = 0x0D03,
    INFORMATION_UNIT_TOO_LONG                                      = 0x0E02,
    INFORMATION_UNIT_TOO_SHORT                                     = 0x0E01,
    INFORMATION_UNIT_iuCRC_ERROR_DETECTED                          = 0x4703,
    INITIATOR_DETECTED_ERROR_MESSAGE_RECEIVED                      = 0x4800,
    INITIATOR_RESPONSE_TIMEOUT                                     = 0x4B06,
    INLINE_DATA_LENGTH_EXCEEDED                                    = 0x260B,
    INQUIRY_DATA_HAS_CHANGED                                       = 0x3F03,
    INSUFFICIENT_ACCESS_CONTROL_RESOURCES                          = 0x5505,
    INSUFFICIENT_REGISTRATION_RESOURCES                            = 0x5504,
    INSUFFICIENT_RESERVATION_RESOURCES                             = 0x5502,
    INSUFFICIENT_RESOURCES                                         = 0x5503,
    INSUFFICIENT_TIME_FOR_OPERATION                                = 0x2E00,
    INTERNAL_TARGET_FAILURE                                        = 0x4400,
    INVALID_ADDRESS_FOR_WRITE                                      = 0x2102,
    INVALID_BITS_IN_IDENTIFY_MESSAGE                               = 0x3D00,
    INVALID_COMMAND_OPERATION_CODE                                 = 0x2000,
    INVALID_ELEMENT_ADDRESS                                        = 0x2101,
    INVALID_FIELD_IN_CDB                                           = 0x2400,
    INVALID_FIELD_IN_COMMAND_INFORMATION_UNIT                      = 0x0E03,
    INVALID_FIELD_IN_PARAMETER_LIST                                = 0x2600,
    INVALID_INFORMATION_UNIT                                       = 0x0E00,
    INVALID_MESSAGE_ERROR                                          = 0x4900,
    INVALID_OPERATION_FOR_COPY_SOURCE_OR_DESTINATION               = 0x260C,
    INVALID_PACKET_SIZE                                            = 0x6401,
    INVALID_PARAMETER_WHILE_PORT_IS_ENABLED                        = 0x260E,
    INVALID_RELEASE_OF_PERSISTENT_RESERVATION                      = 0x2604,
    INVALID_TARGET_PORT_TRANSFER_TAG_RECEIVED                      = 0x4B01,
    IT_NEXUS_LOSS_OCCURRED                                         = 0x2907,
    LEC_UNCORRECTABLE_ERROR                                        = 0x1105,
    LOG_COUNTER_AT_MAXIMUM                                         = 0x5B02,
    LOG_EXCEPTION                                                  = 0x5B00,
    LOG_LIST_CODES_EXHAUSTED                                       = 0x5B03,
    LOG_PARAMETERS_CHANGED                                         = 0x2A02,
    LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE                             = 0x2100,
    LOGICAL_UNIT_COMMUNICATION_CRC_ERROR_UDMA32                    = 0x0803,
    LOGICAL_UNIT_COMMUNICATION_FAILURE                             = 0x0800,
    LOGICAL_UNIT_COMMUNICATION_PARITY_ERROR                        = 0x0802,
    LOGICAL_UNIT_COMMUNICATION_TIMEOUT                             = 0x0801,
    LOGICAL_UNIT_DOES_NOT_RESPOND_TO_SELECTION                     = 0x0500,
    LOGICAL_UNIT_FAILED_SELFCONFIGURATION                          = 0x4C00,
    LOGICAL_UNIT_FAILED_SELFTEST                                   = 0x3E03,
    LOGICAL_UNIT_FAILURE                                           = 0x3E01,
    LOGICAL_UNIT_FAILURE_PREDICTION_THRESHOLD_EXCEEDED             = 0x5D02,
    LOGICAL_UNIT_HAS_NOT_SELFCONFIGURED_YET                        = 0x3E00,
    LOGICAL_UNIT_IS_IN_PROCESS_OF_BECOMING_READY                   = 0x0401,
    LOGICAL_UNIT_NOT_ACCESSIBLE_ASYMMETRIC_ACCESS_STATE_TRANSITION = 0x040A,
    LOGICAL_UNIT_NOT_ACCESSIBLE_TARGET_PORT_IN_STANDBY_STATE       = 0x040B,
    LOGICAL_UNIT_NOT_ACCESSIBLE_TARGET_PORT_IN_UNAVAILABLE_STATE   = 0x040C,
    LOGICAL_UNIT_NOT_READY_AUXILIARY_MEMORY_NOT_ACCESSIBLE         = 0x0410,
    LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE                    = 0x0400,
    LOGICAL_UNIT_NOT_READY_FORMAT_IN_PROGRESS                      = 0x0404,
    LOGICAL_UNIT_NOT_READY_INITIALIZING_COMMAND_REQUIRED           = 0x0402,
    LOGICAL_UNIT_NOT_READY_LONG_WRITE_IN_PROGRESS                  = 0x0408,
    LOGICAL_UNIT_NOT_READY_MANUAL_INTERVENTION_REQUIRED            = 0x0403,
    LOGICAL_UNIT_NOT_READY_NOTIFY_ENABLE_SPINUP_REQUIRED           = 0x0411,
    LOGICAL_UNIT_NOT_READY_OPERATION_IN_PROGRESS                   = 0x0407,
    LOGICAL_UNIT_NOT_READY_SELFTEST_IN_PROGRESS                    = 0x0409,
    LOGICAL_UNIT_NOT_SUPPORTED                                     = 0x2500,
    LOGICAL_UNIT_SOFTWARE_WRITE_PROTECTED                          = 0x2702,
    LOGICAL_UNIT_UNABLE_TO_UPDATE_SELFTEST_LOG                     = 0x3E04,
    LOW_POWER_CONDITION_ON                                         = 0x5E00,
    MECHANICAL_POSITIONING_ERROR                                   = 0x1501,
    MECHANICAL_POSITIONING_OR_CHANGER_ERROR                        = 0x3B16,
    MEDIA_FAILURE_PREDICTION_THRESHOLD_EXCEEDED                    = 0x5D01,
    MEDIA_LOAD_OR_EJECT_FAILED                                     = 0x5300,
    MEDIA_REGION_CODE_IS_MISMATCHED_TO_LOGICAL_UNIT_REGION         = 0x6F04,
    MEDIUM_AUXILIARY_MEMORY_ACCESSIBLE                             = 0x3F11,
    MEDIUM_DESTINATION_ELEMENT_FULL                                = 0x3B0D,
    MEDIUM_FORMAT_CORRUPTED                                        = 0x3100,
    MEDIUM_LOADABLE                                                = 0x3F10,
    MEDIUM_MAGAZINE_INSERTED                                       = 0x3B13,
    MEDIUM_MAGAZINE_LOCKED                                         = 0x3B14,
    MEDIUM_MAGAZINE_NOT_ACCESSIBLE                                 = 0x3B11,
    MEDIUM_MAGAZINE_REMOVED                                        = 0x3B12,
    MEDIUM_MAGAZINE_UNLOCKED                                       = 0x3B15,
    MEDIUM_NOT_FORMATTED                                           = 0x3010,
    MEDIUM_NOT_PRESENT                                             = 0x3A00,
    MEDIUM_NOT_PRESENT_LOADABLE                                    = 0x3A03,
    MEDIUM_NOT_PRESENT_MEDIUM_AUXILIARY_MEMORY_ACCESSIBLE          = 0x3A04,
    MEDIUM_NOT_PRESENT_TRAY_CLOSED                                 = 0x3A01,
    MEDIUM_NOT_PRESENT_TRAY_OPEN                                   = 0x3A02,
    MEDIUM_REMOVAL_PREVENTED                                       = 0x5302,
    MEDIUM_SOURCE_ELEMENT_EMPTY                                    = 0x3B0E,
    MESSAGE_ERROR                                                  = 0x4300,
    MICROCODE_HAS_BEEN_CHANGED                                     = 0x3F01,
    MISCOMPARE_DURING_VERIFY_OPERATION                             = 0x1D00,
    MODE_PARAMETERS_CHANGED                                        = 0x2A01,
    NAK_RECEIVED                                                   = 0x4B04,
    NO_ADDITIONAL_SENSE_INFORMATION                                = 0x0000,
    NO_CURRENT_AUDIO_STATUS_TO_RETURN                              = 0x0015,
    NO_MORE_TRACK_RESERVATIONS_ALLOWED                             = 0x7205,
    NO_REFERENCE_POSITION_FOUND                                    = 0x0600,
    NO_SEEK_COMPLETE                                               = 0x0200,
    NOT_READY_TO_READY_CHANGE_MEDIUM_MAY_HAVE_CHANGED              = 0x2800,
    OPERATION_IN_PROGRESS                                          = 0x0016,
    OPERATOR_MEDIUM_REMOVAL_REQUEST                                = 0x5A01,
    OPERATOR_REQUEST_OR_STATE_CHANGE_INPUT                         = 0x5A00,
    OPERATOR_SELECTED_WRITE_PERMIT                                 = 0x5A03,
    OPERATOR_SELECTED_WRITE_PROTECT                                = 0x5A02,
    OVERLAPPED_COMMANDS_ATTEMPTED                                  = 0x4E00,
    PACKET_DOES_NOT_FIT_IN_AVAILABLE_SPACE                         = 0x6301,
    PARAMETER_LIST_LENGTH_ERROR                                    = 0x1A00,
    PARAMETER_NOT_SUPPORTED                                        = 0x2601,
    PARAMETER_VALUE_INVALID                                        = 0x2602,
    PARAMETERS_CHANGED                                             = 0x2A00,
    PERMANENT_WRITE_PROTECT                                        = 0x2705,
    PERSISTENT_PREVENT_CONFLICT                                    = 0x2C06,
    PERSISTENT_WRITE_PROTECT                                       = 0x2704,
    POSITIONING_ERROR_DETECTED_BY_READ_OF_MEDIUM                   = 0x1502,
    POWER_CALIBRATION_AREA_ALMOST_FULL                             = 0x7301,
    POWER_CALIBRATION_AREA_ERROR                                   = 0x7303,
    POWER_CALIBRATION_AREA_IS_FULL                                 = 0x7302,
    POWER_ON_OCCURRED                                              = 0x2901,
    POWER_ON_RESET_OR_BUS_DEVICE_RESET_OCCURRED                    = 0x2900,
    PREVIOUS_BUSY_STATUS                                           = 0x2C07,
    PREVIOUS_RESERVATION_CONFLICT_STATUS                           = 0x2C09,
    PREVIOUS_TASK_SET_FULL_STATUS                                  = 0x2C08,
    PRIORITY_CHANGED                                               = 0x2A08,
    PROGRAM_MEMORY_AREA_IS_FULL                                    = 0x7305,
    PROGRAM_MEMORY_AREA_UPDATE_FAILURE                             = 0x7304,
    PROTOCOL_SERVICE_CRC_ERROR                                     = 0x4705,
    RANDOM_POSITIONING_ERROR                                       = 0x1500,
    READ_ERROR_FAILED_RETRANSMISSION_REQUEST                       = 0x1113,
    READ_ERROR_LOSS_OF_STREAMING                                   = 0x1111,
    READ_OF_SCRAMBLED_SECTOR_WITHOUT_AUTHENTICATION                = 0x6F03,
    READ_RETRIES_EXHAUSTED                                         = 0x1101,
    RECORD_NOT_FOUND                                               = 0x1401,
    RECORDED_ENTITY_NOT_FOUND                                      = 0x1400,
    RECOVERED_DATA_DATA_AUTOREALLOCATED                            = 0x1802,
    RECOVERED_DATA_RECOMMEND_REASSIGNMENT                          = 0x1805,
    RECOVERED_DATA_RECOMMEND_REWRITE                               = 0x1806,
    RECOVERED_DATA_USING_PREVIOUS_SECTOR_ID                        = 0x1705,
    RECOVERED_DATA_WITH_CIRC                                       = 0x1803,
    RECOVERED_DATA_WITH_ERROR_CORR_AND_RETRIES_APPLIED             = 0x1801,
    RECOVERED_DATA_WITH_ERROR_CORRECTION_APPLIED                   = 0x1800,
    RECOVERED_DATA_WITH_LEC                                        = 0x1804,
    RECOVERED_DATA_WITH_LINKING                                    = 0x1808,
    RECOVERED_DATA_WITH_NEGATIVE_HEAD_OFFSET                       = 0x1703,
    RECOVERED_DATA_WITH_NO_ERROR_CORRECTION_APPLIED                = 0x1700,
    RECOVERED_DATA_WITH_POSITIVE_HEAD_OFFSET                       = 0x1702,
    RECOVERED_DATA_WITH_RETRIES                                    = 0x1701,
    RECOVERED_DATA_WITH_RETRIES_AND_OR_CIRC_APPLIED                = 0x1704,
    RECOVERED_DATA_WITHOUT_ECC_DATA_REWRITTEN                      = 0x1709,
    RECOVERED_DATA_WITHOUT_ECC_RECOMMEND_REASSIGNMENT              = 0x1707,
    RECOVERED_DATA_WITHOUT_ECC_RECOMMEND_REWRITE                   = 0x1708,
    REDUNDANCY_GROUP_CREATED_OR_MODIFIED                           = 0x3F06,
    REDUNDANCY_GROUP_DELETED                                       = 0x3F07,
    REGISTRATIONS_PREEMPTED                                        = 0x2A05,
    REPORTED_LUNS_DATA_HAS_CHANGED                                 = 0x3F0E,
    RESERVATIONS_PREEMPTED                                         = 0x2A03,
    RESERVATIONS_RELEASED                                          = 0x2A04,
    RMA_PMA_IS_ALMOST_FULL                                         = 0x7306,
    ROUNDED_PARAMETER                                              = 0x3700,
    SAVING_PARAMETERS_NOT_SUPPORTED                                = 0x3900,
    SCSI_BUS_RESET_OCCURRED                                        = 0x2902,
    SCSI_PARITY_ERROR                                              = 0x4700,
    SCSI_PARITY_ERROR_DETECTED_DURING_ST_DATA_PHASE                = 0x4702,
    SELECT_OR_RESELECT_FAILURE                                     = 0x4500,
    SESSION_FIXATION_ERROR                                         = 0x7200,
    SESSION_FIXATION_ERROR_INCOMPLETE_TRACK_IN_SESSION             = 0x7203,
    SESSION_FIXATION_ERROR_WRITING_LEADIN                          = 0x7201,
    SESSION_FIXATION_ERROR_WRITING_LEADOUT                         = 0x7202,
    SET_TARGET_PORT_GROUPS_COMMAND_FAILED                          = 0x670A,
    SOME_COMMANDS_CLEARED_BY_ISCSI_PROTOCOL_EVENT                  = 0x477F,
    SPARE_AREA_EXHAUSTION_PREDICTION_THRESHOLD_EXCEEDED            = 0x5D03,
    SPARE_CREATED_OR_MODIFIED                                      = 0x3F08,
    SPARE_DELETED                                                  = 0x3F09,
    SPINDLE_SERVO_FAILURE                                          = 0x0903,
    STANDBY_CONDITION_ACTIVATED_BY_COMMAND                         = 0x5E04,
    STANDBY_CONDITION_ACTIVATED_BY_TIMER                           = 0x5E02,
    SYNCHRONOUS_DATA_TRANSFER_ERROR                                = 0x1B00,
    TARGET_OPERATING_CONDITIONS_HAVE_CHANGED                       = 0x3F00,
    THIRD_PARTY_DEVICE_FAILURE                                     = 0x0D01,
    THRESHOLD_CONDITION_MET                                        = 0x5B01,
    THRESHOLD_PARAMETERS_NOT_SUPPORTED                             = 0x2603,
    TIMEOUT_ON_LOGICAL_UNIT                                        = 0x3E02,
    TOO_MANY_SEGMENT_DESCRIPTORS                                   = 0x2608,
    TOO_MANY_TARGET_DESCRIPTORS                                    = 0x2606,
    TOO_MUCH_WRITE_DATA                                            = 0x4B02,
    TRACK_FOLLOWING_ERROR                                          = 0x0900,
    TRACKING_SERVO_FAILURE                                         = 0x0901,
    TRANSCEIVER_MODE_CHANGED_TO_LVD                                = 0x2906,
    TRANSCEIVER_MODE_CHANGED_TO_SINGLEENDED                        = 0x2905,
    UNABLE_TO_RECOVER_TOC                                          = 0x5700,
    UNEXPECTED_INEXACT_SEGMENT                                     = 0x260A,
    UNREACHABLE_COPY_TARGET                                        = 0x0804,
    UNRECOVERED_READ_ERROR                                         = 0x1100,
    UNSUCCESSFUL_SOFT_RESET                                        = 0x4600,
    UNSUPPORTED_ENCLOSURE_FUNCTION                                 = 0x3501,
    UNSUPPORTED_SEGMENT_DESCRIPTOR_TYPE_CODE                       = 0x2609,
    UNSUPPORTED_TARGET_DESCRIPTOR_TYPE_CODE                        = 0x2607,
    VOLTAGE_FAULT                                                  = 0x6500,
    VOLUME_SET_CREATED_OR_MODIFIED                                 = 0x3F0A,
    VOLUME_SET_DEASSIGNED                                          = 0x3F0C,
    VOLUME_SET_DELETED                                             = 0x3F0B,
    VOLUME_SET_REASSIGNED                                          = 0x3F0D,
    WARNING                                                        = 0x0B00,
    WARNING_ENCLOSURE_DEGRADED                                     = 0x0B02,
    WARNING_SPECIFIED_TEMPERATURE_EXCEEDED                         = 0x0B01,
    WRITE_ERROR                                                    = 0x0C00,
    WRITE_ERROR_LOSS_OF_STREAMING                                  = 0x0C09,
    WRITE_ERROR_NOT_ENOUGH_UNSOLICITED_DATA                        = 0x0C0D,
    WRITE_ERROR_PADDING_BLOCKS_ADDED                               = 0x0C0A,
    WRITE_ERROR_RECOVERY_FAILED                                    = 0x0C08,
    WRITE_ERROR_RECOVERY_NEEDED                                    = 0x0C07,
    WRITE_ERROR_UNEXPECTED_UNSOLICITED_DATA                        = 0x0C0C,
    WRITE_PROTECTED                                                = 0x2700,
    ZONED_FORMATTING_FAILED_DUE_TO_SPARE_LINKING                   = 0x3102
} AdditionalSenseCode;

/* Inline function that combines sense key and ASC/ASCQ code */
static inline guint32 make_status(SenseStatus status, SenseKey sensekey, AdditionalSenseCode sensecode)
{
    guint32 val = (status << 24) | (sensekey << 16) | (sensecode);

    return val;
}

#endif /* __CDEMU_SENSE_CONSTANTS_H__ */
