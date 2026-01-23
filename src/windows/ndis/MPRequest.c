/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             M P R E Q U E S T . C

GENERAL DESCRIPTION
    This is a debug module that converts an OID to a string for debug printing.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "MPMain.h"
#include "MPUsb.h"
#include "MPOID.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "MPRequest.tmh"

#endif  // EVENT_TRACING

PUCHAR MPOID_OidToNameStr(ULONG oid)
{

    PCHAR retStr = "";
    switch (oid)
    {

        case OID_GEN_SUPPORTED_LIST:
            retStr = "OID_GEN_SUPPORTED_LIST";
            break;

        case OID_GEN_HARDWARE_STATUS:
            retStr = "OID_GEN_HARDWARE_STATUS";
            break;

        case OID_GEN_MEDIA_SUPPORTED:
            retStr = "OID_GEN_MEDIA_SUPPORTED";
            break;

        case OID_GEN_MEDIA_IN_USE:
            retStr = "OID_GEN_MEDIA_IN_USE";
            break;

        case OID_GEN_MAXIMUM_LOOKAHEAD:
            retStr = "OID_GEN_MAXIMUM_LOOKAHEAD";
            break;

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            retStr = "OID_GEN_MAXIMUM_FRAME_SIZE";
            break;

        case OID_GEN_LINK_SPEED:
            retStr = "OID_GEN_LINK_SPEED";
            break;

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
            retStr = "OID_GEN_TRANSMIT_BUFFER_SPACE";
            break;

        case OID_GEN_RECEIVE_BUFFER_SPACE:
            retStr = "OID_GEN_RECEIVE_BUFFER_SPACE";
            break;

        case OID_GEN_TRANSMIT_BLOCK_SIZE:
            retStr = "OID_GEN_TRANSMIT_BLOCK_SIZE";
            break;

        case OID_GEN_RECEIVE_BLOCK_SIZE:
            retStr = "OID_GEN_RECEIVE_BLOCK_SIZE";
            break;

        case OID_GEN_VENDOR_ID:
            retStr = "OID_GEN_VENDOR_ID";
            break;

        case OID_GEN_VENDOR_DESCRIPTION:
            retStr = "OID_GEN_VENDOR_DESCRIPTION";
            break;

        case OID_GEN_CURRENT_PACKET_FILTER:
            retStr = "OID_GEN_CURRENT_PACKET_FILTER";
            break;

        case OID_GEN_CURRENT_LOOKAHEAD:
            retStr = "OID_GEN_CURRENT_LOOKAHEAD";
            break;

        case OID_GEN_DRIVER_VERSION:
            retStr = "OID_GEN_DRIVER_VERSION";
            break;

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
            retStr = "OID_GEN_MAXIMUM_TOTAL_SIZE";
            break;

        case OID_GEN_PROTOCOL_OPTIONS:
            retStr = "OID_GEN_PROTOCOL_OPTIONS";
            break;

        case OID_GEN_MAC_OPTIONS:
            retStr = "OID_GEN_MAC_OPTIONS";
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS:
            retStr = "OID_GEN_MEDIA_CONNECT_STATUS";
            break;

        case OID_GEN_MAXIMUM_SEND_PACKETS:
            retStr = "OID_GEN_MAXIMUM_SEND_PACKETS";
            break;

        case OID_GEN_VENDOR_DRIVER_VERSION:
            retStr = "OID_GEN_VENDOR_DRIVER_VERSION";
            break;

        case OID_GEN_SUPPORTED_GUIDS:
            retStr = "OID_GEN_SUPPORTED_GUIDS";
            break;

        case OID_GEN_NETWORK_LAYER_ADDRESSES:
            retStr = "OID_GEN_NETWORK_LAYER_ADDRESSES";
            break;

        case OID_GEN_TRANSPORT_HEADER_OFFSET:
            retStr = "OID_GEN_TRANSPORT_HEADER_OFFSET";
            break;

        case OID_GEN_MEDIA_CAPABILITIES:
            retStr = "OID_GEN_MEDIA_CAPABILITIES";
            break;

        case OID_GEN_PHYSICAL_MEDIUM:
            retStr = "OID_GEN_PHYSICAL_MEDIUM";
            break;

        case OID_GEN_XMIT_OK:
            retStr = "OID_GEN_XMIT_OK";
            break;

        case OID_GEN_RCV_OK:
            retStr = "OID_GEN_RCV_OK";
            break;

        case OID_GEN_XMIT_ERROR:
            retStr = "OID_GEN_XMIT_ERROR";
            break;

        case OID_GEN_RCV_ERROR:
            retStr = "OID_GEN_RCV_ERROR";
            break;

        case OID_GEN_RCV_NO_BUFFER:
            retStr = "OID_GEN_RCV_NO_BUFFER";
            break;

        case OID_GEN_DIRECTED_BYTES_XMIT:
            retStr = "OID_GEN_DIRECTED_BYTES_XMIT";
            break;

        case OID_GEN_DIRECTED_FRAMES_XMIT:
            retStr = "OID_GEN_DIRECTED_FRAMES_XMIT";
            break;

        case OID_GEN_MULTICAST_BYTES_XMIT:
            retStr = "OID_GEN_MULTICAST_BYTES_XMIT";
            break;

        case OID_GEN_MULTICAST_FRAMES_XMIT:
            retStr = "OID_GEN_MULTICAST_FRAMES_XMIT";
            break;

        case OID_GEN_BROADCAST_BYTES_XMIT:
            retStr = "OID_GEN_BROADCAST_BYTES_XMIT";
            break;

        case OID_GEN_BROADCAST_FRAMES_XMIT:
            retStr = "OID_GEN_BROADCAST_FRAMES_XMIT";
            break;

        case OID_GEN_DIRECTED_BYTES_RCV:
            retStr = "OID_GEN_DIRECTED_BYTES_RCV";
            break;

        case OID_GEN_DIRECTED_FRAMES_RCV:
            retStr = "OID_GEN_DIRECTED_FRAMES_RCV";
            break;

        case OID_GEN_MULTICAST_BYTES_RCV:
            retStr = "OID_GEN_MULTICAST_BYTES_RCV";
            break;

        case OID_GEN_MULTICAST_FRAMES_RCV:
            retStr = "OID_GEN_MULTICAST_FRAMES_RCV";
            break;

        case OID_GEN_BROADCAST_BYTES_RCV:
            retStr = "OID_GEN_BROADCAST_BYTES_RCV";
            break;

        case OID_GEN_BROADCAST_FRAMES_RCV:
            retStr = "OID_GEN_BROADCAST_FRAMES_RCV";
            break;

        case OID_GEN_RCV_CRC_ERROR:
            retStr = "OID_GEN_RCV_CRC_ERROR";
            break;

        case OID_GEN_TRANSMIT_QUEUE_LENGTH:
            retStr = "OID_GEN_TRANSMIT_QUEUE_LENGTH";
            break;

        case OID_GEN_GET_TIME_CAPS:
            retStr = "OID_GEN_GET_TIME_CAPS";
            break;

        case OID_GEN_GET_NETCARD_TIME:
            retStr = "OID_GEN_GET_NETCARD_TIME";
            break;

        case OID_GEN_NETCARD_LOAD:
            retStr = "OID_GEN_NETCARD_LOAD";
            break;

        case OID_GEN_DEVICE_PROFILE:
            retStr = "OID_GEN_DEVICE_PROFILE";
            break;

        case OID_GEN_INIT_TIME_MS:
            retStr = "OID_GEN_INIT_TIME_MS";
            break;

        case OID_GEN_RESET_COUNTS:
            retStr = "OID_GEN_RESET_COUNTS";
            break;

        case OID_GEN_MEDIA_SENSE_COUNTS:
            retStr = "OID_GEN_MEDIA_SENSE_COUNTS";
            break;

        case OID_PNP_CAPABILITIES:
            retStr = "OID_PNP_CAPABILITIES";
            break;

        case OID_PNP_SET_POWER:
            retStr = "OID_PNP_SET_POWER";
            break;

        case OID_PNP_QUERY_POWER:
            retStr = "OID_PNP_QUERY_POWER";
            break;

        case OID_PNP_ADD_WAKE_UP_PATTERN:
            retStr = "OID_PNP_ADD_WAKE_UP_PATTERN";
            break;

        case OID_PNP_REMOVE_WAKE_UP_PATTERN:
            retStr = "OID_PNP_REMOVE_WAKE_UP_PATTERN";
            break;

        case OID_PNP_ENABLE_WAKE_UP:
            retStr = "OID_PNP_ENABLE_WAKE_UP";
            break;

        case OID_802_3_PERMANENT_ADDRESS:
            retStr = "OID_802_3_PERMANENT_ADDRESS";
            break;

        case OID_802_3_CURRENT_ADDRESS:
            retStr = "OID_802_3_CURRENT_ADDRESS";
            break;

        case OID_802_3_MULTICAST_LIST:
            retStr = "OID_802_3_MULTICAST_LIST";
            break;

        case OID_802_3_MAXIMUM_LIST_SIZE:
            retStr = "OID_802_3_MAXIMUM_LIST_SIZE";
            break;

        case OID_802_3_MAC_OPTIONS:
            retStr = "OID_802_3_MAC_OPTIONS";
            break;

        case OID_802_3_RCV_ERROR_ALIGNMENT:
            retStr = "OID_802_3_RCV_ERROR_ALIGNMENT";
            break;

        case OID_802_3_XMIT_ONE_COLLISION:
            retStr = "OID_802_3_XMIT_ONE_COLLISION";
            break;

        case OID_802_3_XMIT_MORE_COLLISIONS:
            retStr = "OID_802_3_XMIT_MORE_COLLISIONS";
            break;

        case OID_802_3_XMIT_DEFERRED:
            retStr = "OID_802_3_XMIT_DEFERRED";
            break;

        case OID_802_3_XMIT_MAX_COLLISIONS:
            retStr = "OID_802_3_XMIT_MAX_COLLISIONS";
            break;

        case OID_802_3_RCV_OVERRUN:
            retStr = "OID_802_3_RCV_OVERRUN";
            break;

        case OID_802_3_XMIT_UNDERRUN:
            retStr = "OID_802_3_XMIT_UNDERRUN";
            break;

        case OID_802_3_XMIT_HEARTBEAT_FAILURE:
            retStr = "OID_802_3_XMIT_HEARTBEAT_FAILURE";
            break;

        case OID_802_3_XMIT_TIMES_CRS_LOST:
            retStr = "OID_802_3_XMIT_TIMES_CRS_LOST";
            break;

        case OID_802_3_XMIT_LATE_COLLISIONS:
            retStr = "OID_802_3_XMIT_LATE_COLLISIONS";
            break;

        case OID_GEN_MACHINE_NAME:
            retStr = "OID_GEN_MACHINE_NAME";
            break;

        case OID_GEN_VLAN_ID:
            retStr = "OID_GEN_VLAN_ID";
            break;

#ifdef NDIS60_MINIPORT

        case OID_GEN_LINK_PARAMETERS:
            retStr = "OID_GEN_LINK_PARAMETERS";
            break;

        case OID_GEN_STATISTICS:
            retStr = "OID_GEN_STATISTICS";
            break;

        case OID_GEN_LINK_STATE:
            retStr = "OID_GEN_LINK_STATE";
            break;

#endif // NDIS60_MINIPORT

#ifdef NDIS620_MINIPORT

        case OID_WWAN_DRIVER_CAPS:
            retStr = "OID_WWAN_DRIVER_CAPS";
            break;

        case OID_WWAN_DEVICE_CAPS:
            retStr = "OID_WWAN_DEVICE_CAPS";
            break;

        case OID_WWAN_READY_INFO:
            retStr = "OID_WWAN_READY_INFO";
            break;

        case OID_WWAN_SERVICE_ACTIVATION:
            retStr = "OID_WWAN_SERVICE_ACTIVATION";
            break;

        case OID_WWAN_RADIO_STATE:
            retStr = "OID_WWAN_RADIO_STATE";
            break;

        case OID_WWAN_PIN:
            retStr = "OID_WWAN_PIN";
            break;

        case OID_WWAN_PIN_LIST:
            retStr = "OID_WWAN_PIN_LIST";
            break;

        case OID_WWAN_HOME_PROVIDER:
            retStr = "OID_WWAN_HOME_PROVIDER";
            break;

        case OID_WWAN_PREFERRED_PROVIDERS:
            retStr = "OID_WWAN_PREFERRED_PROVIDERS";
            break;

        case OID_WWAN_VISIBLE_PROVIDERS:
            retStr = "OID_WWAN_VISIBLE_PROVIDERS";
            break;

        case OID_WWAN_REGISTER_STATE:
            retStr = "OID_WWAN_REGISTER_STATE";
            break;

        case OID_WWAN_SIGNAL_STATE:
            retStr = "OID_WWAN_SIGNAL_STATE";
            break;

        case OID_WWAN_PACKET_SERVICE:
            retStr = "OID_WWAN_PACKET_SERVICE";
            break;

        case OID_WWAN_PROVISIONED_CONTEXTS:
            retStr = "OID_WWAN_PROVISIONED_CONTEXTS";
            break;

        case OID_WWAN_CONNECT:
            retStr = "OID_WWAN_CONNECT";
            break;

        case OID_WWAN_SMS_CONFIGURATION:
            retStr = "OID_WWAN_SMS_CONFIGURATION";
            break;

        case OID_WWAN_SMS_READ:
            retStr = "OID_WWAN_SMS_READ";
            break;

        case OID_WWAN_SMS_SEND:
            retStr = "OID_WWAN_SMS_SEND";
            break;

        case OID_WWAN_SMS_DELETE:
            retStr = "OID_WWAN_SMS_DELETE";
            break;

        case OID_WWAN_SMS_STATUS:
            retStr = "OID_WWAN_SMS_STATUS";
            break;

#endif // NDIS620_MINIPORT

        case OID_GOBI_DEVICE_INFO:
            retStr = "OID_GOBI_DEVICE_INFO";
            break;

        default:
            retStr = "<** UNKNOWN OID **>";
            break;
    }
    return retStr;
}
