/* packet-cops.c
 * Routines for the COPS (Common Open Policy Service) protocol dissection
 *
 * RFC 2748 The COPS (Common Open Policy Service) Protocol
 * RFC 3084 COPS Usage for Policy Provisioning (COPS-PR)
 * RFC 3159 Structure of Policy Provisioning Information (SPPI)
 *
 * Copyright 2000, Heikki Vatiainen <hessu@cs.tut.fi>
 *
 * Added request/response tracking in July 2013 by Simon Zhong <szhong@juniper.net>
 *
 * Added PacketCable D-QoS specifications by Dick Gooris <gooris@lucent.com>
 *
 * Taken from PacketCable specifications :
 *    PacketCable Dynamic Quality-of-Service Specification
 *    Based on PKT-SP-DQOS-I09-040402 (April 2, 2004)
 *
 *    PacketCable Multimedia Specification
 *    Based on PKT-SP-MM-I04-080522 and PKT-SP-MM-I05-091029
 *
 *    www.packetcable.com
 *
 * Implemented in wireshark at April 7-8, 2004
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Some of the development of the COPS protocol decoder was sponsored by
 * Cable Television Laboratories, Inc. ("CableLabs") based upon proprietary
 * CableLabs' specifications. Your license and use of this protocol decoder
 * does not mean that you are licensed to use the CableLabs'
 * specifications.  If you have questions about this protocol, contact
 * jf.mule [AT] cablelabs.com or c.stuart [AT] cablelabs.com for additional
 * information.
 */


#include "config.h"

#include <epan/packet.h>
#include "packet-tcp.h"

#include <epan/oids.h>
#include <epan/expert.h>
#include <epan/tfs.h>
#include <unit_strings.h>

#include <wsutil/str_util.h>

#include "packet-ber.h"

/* XXX - The "plain" COPS port (3288) can be overridden in the prefs.
   The PacketCable port cannot - should this be the case? */
#define TCP_PORT_COPS 3288
#define TCP_PORT_PKTCABLE_COPS 2126
#define TCP_PORT_PKTCABLE_MM_COPS 3918

void proto_register_cops(void);
void proto_reg_handoff_cops(void);


/* Preference: desegmentation of COPS */
static bool cops_desegment = true;

#define COPS_OBJECT_HDR_SIZE 4

static const value_string cops_flags_vals[] = {
    { 0x00,          "None" },
    { 0x01,          "Solicited Message Flag Bit" },
    { 0, NULL },
};

/* The different COPS message types */
enum cops_op_code {
    COPS_NO_MSG,          /* Not a COPS Message type     */

    COPS_MSG_REQ,         /* Request (REQ)               */
    COPS_MSG_DEC,         /* Decision (DEC)              */
    COPS_MSG_RPT,         /* Report State (RPT)          */
    COPS_MSG_DRQ,         /* Delete Request State (DRQ)  */
    COPS_MSG_SSQ,         /* Synchronize State Req (SSQ) */
    COPS_MSG_OPN,         /* Client-Open (OPN)           */
    COPS_MSG_CAT,         /* Client-Accept (CAT)         */
    COPS_MSG_CC,          /* Client-Close (CC)           */
    COPS_MSG_KA,          /* Keep-Alive (KA)             */
    COPS_MSG_SSC,         /* Synchronize Complete (SSC)  */

    COPS_LAST_OP_CODE     /* For error checking          */
};

static const value_string cops_op_code_vals[] = {
    { COPS_MSG_REQ,          "Request (REQ)" },
    { COPS_MSG_DEC,          "Decision (DEC)" },
    { COPS_MSG_RPT,          "Report State (RPT)" },
    { COPS_MSG_DRQ,          "Delete Request State (DRQ)" },
    { COPS_MSG_SSQ,          "Synchronize State Req (SSQ)" },
    { COPS_MSG_OPN,          "Client-Open (OPN)" },
    { COPS_MSG_CAT,          "Client-Accept (CAT)" },
    { COPS_MSG_CC,           "Client-Close (CC)" },
    { COPS_MSG_KA,           "Keep-Alive (KA)" },
    { COPS_MSG_SSC,          "Synchronize Complete (SSC)" },
    { 0, NULL },
};


/* The different objects in COPS messages */
enum cops_c_num {
    COPS_NO_OBJECT,        /* Not a COPS Object type               */

    COPS_OBJ_HANDLE,       /* Handle Object (Handle)               */
    COPS_OBJ_CONTEXT,      /* Context Object (Context)             */
    COPS_OBJ_IN_INT,       /* In-Interface Object (IN-Int)         */
    COPS_OBJ_OUT_INT,      /* Out-Interface Object (OUT-Int)       */
    COPS_OBJ_REASON,       /* Reason Object (Reason)               */
    COPS_OBJ_DECISION,     /* Decision Object (Decision)           */
    COPS_OBJ_LPDPDECISION, /* LPDP Decision Object (LPDPDecision)  */
    COPS_OBJ_ERROR,        /* Error Object (Error)                 */
    COPS_OBJ_CLIENTSI,     /* Client Specific Information Object (ClientSI) */
    COPS_OBJ_KATIMER,      /* Keep-Alive Timer Object (KATimer)    */
    COPS_OBJ_PEPID,        /* PEP Identification Object (PEPID)    */
    COPS_OBJ_REPORT_TYPE,  /* Report-Type Object (Report-Type)     */
    COPS_OBJ_PDPREDIRADDR, /* PDP Redirect Address Object (PDPRedirAddr) */
    COPS_OBJ_LASTPDPADDR,  /* Last PDP Address (LastPDPaddr)       */
    COPS_OBJ_ACCTTIMER,    /* Accounting Timer Object (AcctTimer)  */
    COPS_OBJ_INTEGRITY,    /* Message Integrity Object (Integrity) */
    COPS_LAST_C_NUM        /* For error checking                   */
};

static const value_string cops_c_num_vals[] = {
    { COPS_OBJ_HANDLE,       "Handle Object (Handle)" },
    { COPS_OBJ_CONTEXT,      "Context Object (Context)" },
    { COPS_OBJ_IN_INT,       "In-Interface Object (IN-Int)" },
    { COPS_OBJ_OUT_INT,      "Out-Interface Object (OUT-Int)" },
    { COPS_OBJ_REASON,       "Reason Object (Reason)" },
    { COPS_OBJ_DECISION,     "Decision Object (Decision)" },
    { COPS_OBJ_LPDPDECISION, "LPDP Decision Object (LPDPDecision)" },
    { COPS_OBJ_ERROR,        "Error Object (Error)" },
    { COPS_OBJ_CLIENTSI,     "Client Specific Information Object (ClientSI)" },
    { COPS_OBJ_KATIMER,      "Keep-Alive Timer Object (KATimer)" },
    { COPS_OBJ_PEPID,        "PEP Identification Object (PEPID)" },
    { COPS_OBJ_REPORT_TYPE,  "Report-Type Object (Report-Type)" },
    { COPS_OBJ_PDPREDIRADDR, "PDP Redirect Address Object (PDPRedirAddr)" },
    { COPS_OBJ_LASTPDPADDR,  "Last PDP Address (LastPDPaddr)" },
    { COPS_OBJ_ACCTTIMER,    "Accounting Timer Object (AcctTimer)" },
    { COPS_OBJ_INTEGRITY,    "Message Integrity Object (Integrity)" },
    { 0, NULL },
};


/* The different objects in COPS-PR messages */
enum cops_s_num {
    COPS_NO_PR_OBJECT,     /* Not a COPS-PR Object type               */
    COPS_OBJ_PRID,         /* Provisioning Instance Identifier (PRID) */
    COPS_OBJ_PPRID,        /* Prefix Provisioning Instance Identifier (PPRID) */
    COPS_OBJ_EPD,          /* Encoded Provisioning Instance Data (EPD) */
    COPS_OBJ_GPERR,        /* Global Provisioning Error Object (GPERR) */
    COPS_OBJ_CPERR,        /* PRC Class Provisioning Error Object (CPERR) */
    COPS_OBJ_ERRPRID,      /* Error Provisioning Instance Identifier (ErrorPRID)*/

    COPS_LAST_S_NUM        /* For error checking                   */
};


static const value_string cops_s_num_vals[] = {
    { COPS_OBJ_PRID,         "Provisioning Instance Identifier (PRID)" },
    { COPS_OBJ_PPRID,        "Prefix Provisioning Instance Identifier (PPRID)" },
    { COPS_OBJ_EPD,          "Encoded Provisioning Instance Data (EPD)" },
    { COPS_OBJ_GPERR,        "Global Provisioning Error Object (GPERR)" },
    { COPS_OBJ_CPERR,        "PRC Class Provisioning Error Object (CPERR)" },
    { COPS_OBJ_ERRPRID,      "Error Provisioning Instance Identifier (ErrorPRID)" },
    { 0, NULL },

};

/* R-Type is carried within the Context Object */
static const value_string cops_r_type_vals[] = {
    { 0x01, "Incoming-Message/Admission Control request" },
    { 0x02, "Resource-Allocation request" },
    { 0x04, "Outgoing-Message request" },
    { 0x08, "Configuration request" },
    { 0, NULL },
};
/* S-Type is carried within the ClientSI Object for COPS-PR*/
static const value_string cops_s_type_vals[] = {
    { 0x01, "BER" },
    { 0, NULL },
};

/* Reason-Code is carried within the Reason object */
static const value_string cops_reason_vals[] = {
    { 1,  "Unspecified" },
    { 2,  "Management" },
    { 3,  "Preempted (Another request state takes precedence)" },
    { 4,  "Tear (Used to communicate a signaled state removal)" },
    { 5,  "Timeout (Local state has timed-out)" },
    { 6,  "Route Change (Change invalidates request state)" },
    { 7,  "Insufficient Resources (No local resource available)" },
    { 8,  "PDP's Directive (PDP decision caused the delete)" },
    { 9,  "Unsupported decision (PDP decision not supported)" },
    { 10, "Synchronize Handle Unknown" },
    { 11, "Transient Handle (stateless event)" },
    { 12, "Malformed Decision (could not recover)" },
    { 13, "Unknown COPS Object from PDP" },
    { 0, NULL },
};

/* Command-Code is carried within the Decision object if C-Type is 1 */
static const value_string cops_dec_cmd_code_vals[] = {
    { 0, "NULL Decision (No configuration data available)" },
    { 1, "Install (Admit request/Install configuration)" },
    { 2, "Remove (Remove request/Remove configuration)" },
    { 0, NULL },
};

/* Decision flags are also carried with the Decision object if C-Type is 1 */
static const value_string cops_dec_cmd_flag_vals[] = {
    { 0x00, "<None set>" },
    { 0x01, "Trigger Error (Trigger error message if set)" },
    { 0, NULL },
};

/* Error-Code from Error object */
static const value_string cops_error_vals[] = {
    {1,  "Bad handle" },
    {2,  "Invalid handle reference" },
    {3,  "Bad message format (Malformed Message)" },
    {4,  "Unable to process (server gives up on query)" },
    {5,  "Mandatory client-specific info missing" },
    {6,  "Unsupported client" },
    {7,  "Mandatory COPS object missing" },
    {8,  "Client Failure" },
    {9,  "Communication Failure" },
    {10, "Unspecified" },
    {11, "Shutting down" },
    {12, "Redirect to Preferred Server" },
    {13, "Unknown COPS Object" },
    {14, "Authentication Failure" },
    {15, "Authentication Required" },
    {0,  NULL },
};
/* Error-Code from GPERR object */
static const value_string cops_gperror_vals[] = {
    {1,  "AvailMemLow" },
    {2,  "AvailMemExhausted" },
    {3,  "unknownASN.1Tag" },
    {4,  "maxMsgSizeExceeded" },
    {5,  "unknownError" },
    {6,  "maxRequestStatesOpen" },
    {7,  "invalidASN.1Length" },
    {8,  "invalidObjectPad" },
    {9,  "unknownPIBData" },
    {10, "unknownCOPSPRObject" },
    {11, "malformedDecision" },
    {0,  NULL },
};

/* Error-Code from CPERR object */
static const value_string cops_cperror_vals[] = {
    {1,  "priSpaceExhausted" },
    {2,  "priInstanceInvalid" },
    {3,  "attrValueInvalid" },
    {4,  "attrValueSupLimited" },
    {5,  "attrEnumSupLimited" },
    {6,  "attrMaxLengthExceeded" },
    {7,  "attrReferenceUnknown" },
    {8,  "priNotifyOnly" },
    {9,  "unknownPrc" },
    {10, "tooFewAttrs" },
    {11, "invalidAttrType" },
    {12, "deletedInRef" },
    {13, "priSpecificError" },
    {0,  NULL },
};


/* Report-Type from Report-Type object */
static const value_string cops_report_type_vals[] = {
    {1, " Success   : Decision was successful at the PEP" },
    {2, " Failure   : Decision could not be completed by PEP" },
    {3, " Accounting: Accounting update for an installed state" },
    {0, NULL },
};


/* Client-type descriptions */
/* http://www.iana.org/assignments/cops-parameters */

/* PacketCable Types */

/* static dissector_handle_t sdp_handle; */

#define COPS_CLIENT_PC_DQOS     0x8008
#define COPS_CLIENT_PC_MM       0x800a

static const value_string cops_client_type_vals[] = {
    {0,                   "None"},
    {1,                   "RSVP"},
    {2,                   "DiffServ QoS"},
    {0x8001,              "IP Highway"},
    {0x8002,              "IP Highway"},
    {0x8003,              "IP Highway"},
    {0x8004,              "IP Highway"},
    {0x8005,              "Fujitsu"},
    {0x8006,              "HP OpenView PolicyXpert"},
    {0x8007,              "HP OpenView PolicyXpert COPS-PR PXPIB"},
    {COPS_CLIENT_PC_DQOS, "PacketCable Dynamic Quality-of-Service"},
    {0x8009,              "3GPP"},
    {COPS_CLIENT_PC_MM,   "PacketCable Multimedia"},
    {0x800b,              "Juniper"},
    {0x800c,              "Q.3303.1 (Rw interface) COPS alternative"},
    {0x800d,              "Q.3304.1 (Rc interface) COPS alternative"},
    {0, NULL},
};

/* The next tables are for PacketCable */

/* Transaction ID table */
static const value_string table_cops_dqos_transaction_id[] =
{
    { 0x1,  "Gate Alloc" },
    { 0x2,  "Gate Alloc Ack" },
    { 0x3,  "Gate Alloc Err" },
    { 0x4,  "Gate Set" },
    { 0x5,  "Gate Set Ack" },
    { 0x6,  "Gate Set Err" },
    { 0x7,  "Gate Info" },
    { 0x8,  "Gate Info Ack" },
    { 0x9,  "Gate Info Err" },
    { 0xa,  "Gate Delete" },
    { 0xb,  "Gate Delete Ack" },
    { 0xc,  "Gate Delete Err" },
    { 0xd,  "Gate Open" },
    { 0xe,  "Gate Close" },
    { 0, NULL },
};

/* Direction */
static const value_string table_cops_direction[] =
{
    { 0x0,  "Downstream gate" },
    { 0x1,  "Upstream gate" },
    { 0, NULL },
};

/* Session Class */
static const value_string table_cops_session_class[] =
{
    { 0x0,  "Unspecified" },
    { 0x1,  "Normal priority VoIP session" },
    { 0x2,  "High priority VoIP session" },
    { 0x3,  "Reserved" },
    { 0, NULL },
};

/* Reason Code */
static const value_string table_cops_reason_code[] =
{
    { 0x0,  "Gate Delete Operation" },
    { 0x1,  "Gate Close Operation" },
    { 0, NULL },
};

/* Reason Sub Code - Delete */
static const value_string table_cops_reason_subcode_delete[] =
{
    { 0x0,  "Normal Operation" },
    { 0x1,  "Local Gate-coordination not completed" },
    { 0x2,  "Remote Gate-coordination not completed" },
    { 0x3,  "Authorization revoked" },
    { 0x4,  "Unexpected Gate-Open" },
    { 0x5,  "Local Gate-Close failure" },
    { 0x7f,"Unspecified error" },
    { 0, NULL },
};

/* Reason Sub Code - Close */
static const value_string table_cops_reason_subcode_close[] =
{
    { 0x0,  "Client initiated release (normal operation)" },
    { 0x1,  "Reservation reassignment (e.g., for priority session)" },
    { 0x2,  "Lack of reservation maintenance (e.g., RSVP refreshes)" },
    { 0x3,  "Lack of Docsis Mac-layer responses (e.g., station maintenance)" },
    { 0x4,  "Timer T0 expiration; no Gate-Set received from CMS" },
    { 0x5,  "Timer T1 expiration; no Commit received from MTA" },
    { 0x6,  "Timer T7 expiration; Service Flow reservation timeout" },
    { 0x7,  "Timer T8 expiration; Service Flow inactivity in the upstream direction" },
    { 0x7f,"Unspecified error" },
    { 0, NULL },
};

/* PacketCable Error */
static const value_string table_cops_packetcable_error[] =
{
    { 0x1,  "No gates currently available" },
    { 0x2,  "Unknown Gate ID" },
    { 0x3,  "Illegal Session Class value" },
    { 0x4,  "Subscriber exceeded gate limit" },
    { 0x5,  "Gate already set" },
    { 0x6,  "Missing Required Object" },
    { 0x7,  "Invalid Object" },
    { 0x7f,"Unspecified error" },
    { 0, NULL },
};


/* PacketCable Multimedia */

static const value_string table_cops_mm_transaction_id[] = {
    {1,  "Reserved"},
    {2,  "Reserved"},
    {3,  "Reserved"},
    {4,  "Gate Set"},
    {5,  "Gate Set Ack"},
    {6,  "Gate Set Err"},
    {7,  "Gate Info"},
    {8,  "Gate Info Ack"},
    {9,  "Gate Info Err"},
    {10, "Gate Delete"},
    {11, "Gate Delete Ack"},
    {12, "Gate Delete Err"},
    {13, "Gate Open"},
    {14, "Gate Close"},
    {15, "Gate Report State"},
    {16, "Invalid Gate Cmd Err"},
    {17, "PDP Config"},
    {18, "PDP Config Ack"},
    {19, "PDP Config Error"},
    {20, "Synch Request"},
    {21, "Synch Report"},
    {22, "Synch Complete"},
    {23, "Message Receipt"},
    {0, NULL },
};

static const value_string pcmm_activation_state_vals[] = {
    {0, "Inactive"},
    {1, "Active"},
    {0, NULL },
};

static const value_string pcmm_action_vals[] = {
    {0, "Add classifier"},
    {1, "Replace classifier"},
    {2, "Delete classifier"},
    {3, "No change"},
    {0, NULL },
};

static const value_string pcmm_flow_spec_service_vals[] = {
    {2, "Guaranteed Rate"},
    {5, "Controlled Load"},
    {0, NULL },
};

static const value_string pcmm_report_type_vals[] = {
    {0, "Standard Report Data"},
    {1, "Complete Gate Data"},
    {0, NULL},
};

static const value_string pcmm_synch_type_vals[] = {
    {0, "Full Synchronization"},
    {1, "Incremental Synchronization"},
    {0, NULL},
};

static const value_string pcmm_packetcable_error_code[] = {
    {1,  "Insufficient Resources"},
    {2,  "Unknown GateID"},
    {6,  "Missing Required Object"},
    {7,  "Invalid Object"},
    {8,  "Volume-Based Usage Limit Exceeded"},
    {9,  "Time-Based Usage Limit Exceeded"},
    {10, "Session Class Limit Exceeded"},
    {11, "Undefined Service Class Name"},
    {12, "Incompatible Envelope"},
    {13, "Invalid SubscriberID"},
    {14, "Unauthorized AMID"},
    {15, "Number of Classifiers Not Supported"},
    {16, "Policy Exception"},
    {17, "Invalid Field Value in Object"},
    {18, "Transport Error"},
    {19, "Unknown Gate Command"},
    {20, "Unauthorized PSID"},
    {21, "No State for PDP"},
    {22, "Unsupported Synch Type"},
    {23, "Incremental Data Incomplete"},
    {127, "Other, Unspecified Error"},
    {0, NULL},
};

static const value_string pcmm_gate_state[] = {
    {1, "Idle/Closed"},
    {2, "Authorized"},
    {3, "Reserved"},
    {4, "Committed"},
    {5, "Committed-Recovery"},
    {0, NULL},
};

static const value_string pcmm_gate_state_reason[] = {
    {1,  "Close initiated by CMTS due to reservation reassignment"},
    {2,  "Close initiated by CMTS due to lack of DOCSIS MAC-layer responses"},
    {3,  "Close initiated by CMTS due to timer T1 expiration"},
    {4,  "Close initiated by CMTS due to timer T2 expiration"},
    {5,  "Inactivity timer expired due to Service Flow inactivity (timer T3 expiration)"},
    {6,  "Close initiated by CMTS due to lack of Reservation Maintenance"},
    {7,  "Gate state unchanged, but volume limit reached"},
    {8,  "Close initiated by CMTS due to timer T4 expiration"},
    {9,  "Gate state unchanged, but timer T2 expiration caused reservation reduction"},
    {10, "Gate state unchanged, but time limit reached"},
    {11, "Close initiated by Policy Server or CMTS, volume limit reached"},
    {12, "Close initiated by Policy Server or CMTS, time limit reached"},
    {13, "Close initiated by CMTS, other"},
    {65535, "Other"},
    {0, NULL},
};


/* End of PacketCable Tables */


/* Initialize the protocol and registered fields */
static int proto_cops;
static int hf_cops_ver_flags;
static int hf_cops_version;
static int hf_cops_flags;

static int hf_cops_response_in;
static int hf_cops_response_to;
static int hf_cops_response_time;

static int hf_cops_op_code;
static int hf_cops_client_type;
static int hf_cops_msg_len;

static int hf_cops_obj_len;
static int hf_cops_obj_c_num;
static int hf_cops_obj_c_type;

static int hf_cops_obj_s_num;
static int hf_cops_obj_s_type;

static int hf_cops_handle;

static int hf_cops_r_type_flags;
static int hf_cops_m_type_flags;

static int hf_cops_in_int_ipv4;
static int hf_cops_in_int_ipv6;
static int hf_cops_out_int_ipv4;
static int hf_cops_out_int_ipv6;
static int hf_cops_int_ifindex;

static int hf_cops_reason;
static int hf_cops_reason_sub;

static int hf_cops_dec_cmd_code;
static int hf_cops_dec_flags;

static int hf_cops_error;
static int hf_cops_error_sub;

static int hf_cops_gperror;
static int hf_cops_gperror_sub;

static int hf_cops_cperror;
static int hf_cops_cperror_sub;

static int hf_cops_katimer;

static int hf_cops_pepid;

static int hf_cops_report_type;

static int hf_cops_pdprediraddr_ipv4;
static int hf_cops_pdprediraddr_ipv6;
static int hf_cops_lastpdpaddr_ipv4;
static int hf_cops_lastpdpaddr_ipv6;
static int hf_cops_pdp_tcp_port;

static int hf_cops_accttimer;

static int hf_cops_key_id;
static int hf_cops_seq_num;

static int hf_cops_prid_oid;
static int hf_cops_pprid_oid;
static int hf_cops_errprid_oid;
static int hf_cops_epd_null;
static int hf_cops_epd_int;
static int hf_cops_epd_octets;
static int hf_cops_epd_oid;
static int hf_cops_epd_ipv4;
static int hf_cops_epd_u32;
static int hf_cops_epd_ticks;
static int hf_cops_epd_opaque;
static int hf_cops_epd_i64;
static int hf_cops_epd_u64;
static int hf_cops_epd_unknown;
static int hf_cops_reserved8;
static int hf_cops_reserved16;
static int hf_cops_reserved24;
static int hf_cops_keyed_message_digest;
static int hf_cops_integrity_contents;
static int hf_cops_opaque_data;

/* For PacketCable D-QoS */
static int hf_cops_subtree;
static int hf_cops_pc_activity_count;
static int hf_cops_pc_algorithm;
static int hf_cops_pc_close_subcode;
static int hf_cops_pc_cmts_ip;
static int hf_cops_pc_cmts_ip_port;
static int hf_cops_pc_prks_ip;
static int hf_cops_pc_prks_ip_port;
static int hf_cops_pc_srks_ip;
static int hf_cops_pc_srks_ip_port;
static int hf_cops_pc_delete_subcode;
static int hf_cops_pc_dest_ip;
static int hf_cops_pc_dest_port;
static int hf_cops_pc_direction;
static int hf_cops_pc_ds_field;
static int hf_cops_pc_gate_id;
static int hf_cops_pc_gate_spec_flags;
static int hf_cops_pc_gate_command_type;
static int hf_cops_pc_dqos_gate_command_type;
static int hf_cops_pc_mm_gate_command_type;
static int hf_cops_pc_key;
static int hf_cops_pc_max_packet_size;
static int hf_cops_pc_min_policed_unit;
static int hf_cops_pc_packetcable_err_code;
static int hf_cops_pc_packetcable_sub_code;
static int hf_cops_pc_peak_data_rate;
static int hf_cops_pc_protocol_id;
static int hf_cops_pc_reason_code;
static int hf_cops_pc_remote_flags;
static int hf_cops_pc_remote_gate_id;
static int hf_cops_pc_reserved;
static int hf_cops_pc_session_class;
static int hf_cops_pc_slack_term;
static int hf_cops_pc_spec_rate;
static int hf_cops_pc_src_ip;
static int hf_cops_pc_src_port;
static int hf_cops_pc_subscriber_id_ipv4;
static int hf_cops_pc_subscriber_id_ipv6;
static int hf_cops_pc_t1_value;
static int hf_cops_pc_t7_value;
static int hf_cops_pc_t8_value;
static int hf_cops_pc_token_bucket_rate;
static int hf_cops_pc_token_bucket_size;
static int hf_cops_pc_transaction_id;
static int hf_cops_pc_dqos_transaction_id;
static int hf_cops_pc_mm_transaction_id;
static int hf_cops_pc_bcid_ts;
static int hf_cops_pc_bcid_id;
static int hf_cops_pc_bcid_tz;
static int hf_cops_pc_bcid_ev;
static int hf_cops_pc_dfcdc_ip;
static int hf_cops_pc_dfccc_ip;
static int hf_cops_pc_dfcdc_ip_port;
static int hf_cops_pc_dfccc_ip_port;
static int hf_cops_pc_dfccc_id;

/* PacketCable Multimedia */
static int hf_cops_pcmm_amid_app_type;
static int hf_cops_pcmm_amid_am_tag;
static int hf_cops_pcmm_gate_spec_flags;
static int hf_cops_pcmm_gate_spec_flags_gate;
static int hf_cops_pcmm_gate_spec_flags_dscp_overwrite;
static int hf_cops_pcmm_gate_spec_dscp_tos_field;
static int hf_cops_pcmm_gate_spec_dscp_tos_mask;
static int hf_cops_pcmm_gate_spec_session_class_id;
static int hf_cops_pcmm_gate_spec_session_class_id_priority;
static int hf_cops_pcmm_gate_spec_session_class_id_preemption;
static int hf_cops_pcmm_gate_spec_session_class_id_configurable;
static int hf_cops_pcmm_gate_spec_timer_t1;
static int hf_cops_pcmm_gate_spec_timer_t2;
static int hf_cops_pcmm_gate_spec_timer_t3;
static int hf_cops_pcmm_gate_spec_timer_t4;
static int hf_cops_pcmm_classifier_protocol_id;
static int hf_cops_pcmm_classifier_dscp_tos_field;
static int hf_cops_pcmm_classifier_dscp_tos_mask;
static int hf_cops_pcmm_classifier_src_addr;
static int hf_cops_pcmm_classifier_src_mask;
static int hf_cops_pcmm_classifier_dst_addr;
static int hf_cops_pcmm_classifier_dst_mask;
static int hf_cops_pcmm_classifier_src_port;
static int hf_cops_pcmm_classifier_src_port_end;
static int hf_cops_pcmm_classifier_dst_port;
static int hf_cops_pcmm_classifier_dst_port_end;
static int hf_cops_pcmm_classifier_priority;
static int hf_cops_pcmm_classifier_classifier_id;
static int hf_cops_pcmm_classifier_activation_state;
static int hf_cops_pcmm_classifier_action;
static int hf_cops_pcmm_classifier_flags;
static int hf_cops_pcmm_classifier_tc_low;
static int hf_cops_pcmm_classifier_tc_high;
static int hf_cops_pcmm_classifier_tc_mask;
static int hf_cops_pcmm_classifier_flow_label;
static int hf_cops_pcmm_classifier_next_header_type;
static int hf_cops_pcmm_classifier_source_prefix_length;
static int hf_cops_pcmm_classifier_destination_prefix_length;
static int hf_cops_pcmm_classifier_src_addr_v6;
static int hf_cops_pcmm_classifier_dst_addr_v6;
static int hf_cops_pcmm_flow_spec_envelope;
static int hf_cops_pcmm_flow_spec_service_number;
static int hf_cops_pcmm_docsis_scn;
static int hf_cops_pcmm_envelope;
static int hf_cops_pcmm_traffic_priority;
static int hf_cops_pcmm_request_transmission_policy;
static int hf_cops_pcmm_request_transmission_policy_sf_all_cm;
static int hf_cops_pcmm_request_transmission_policy_sf_priority;
static int hf_cops_pcmm_request_transmission_policy_sf_request_for_request;
static int hf_cops_pcmm_request_transmission_policy_sf_data_for_data;
static int hf_cops_pcmm_request_transmission_policy_sf_piggyback;
static int hf_cops_pcmm_request_transmission_policy_sf_concatenate;
static int hf_cops_pcmm_request_transmission_policy_sf_fragment;
static int hf_cops_pcmm_request_transmission_policy_sf_suppress;
static int hf_cops_pcmm_request_transmission_policy_sf_drop_packets;
static int hf_cops_pcmm_max_sustained_traffic_rate;
static int hf_cops_pcmm_max_traffic_burst;
static int hf_cops_pcmm_min_reserved_traffic_rate;
static int hf_cops_pcmm_ass_min_rtr_packet_size;
static int hf_cops_pcmm_max_concat_burst;
static int hf_cops_pcmm_req_att_mask;
static int hf_cops_pcmm_forbid_att_mask;
static int hf_cops_pcmm_att_aggr_rule_mask;
static int hf_cops_pcmm_nominal_polling_interval;
static int hf_cops_pcmm_tolerated_poll_jitter;
static int hf_cops_pcmm_unsolicited_grant_size;
static int hf_cops_pcmm_grants_per_interval;
static int hf_cops_pcmm_nominal_grant_interval;
static int hf_cops_pcmm_tolerated_grant_jitter;
static int hf_cops_pcmm_down_resequencing;
static int hf_cops_pcmm_down_peak_traffic_rate;
static int hf_cops_pcmm_max_downstream_latency;
static int hf_cops_pcmm_volume_based_usage_limit;
static int hf_cops_pcmm_time_based_usage_limit;
static int hf_cops_pcmm_gate_time_info;
static int hf_cops_pcmm_gate_usage_info;
static int hf_cops_pcmm_packetcable_error_code;
static int hf_cops_pcmm_packetcable_error_subcode;
static int hf_cops_pcmm_packetcable_gate_state;
static int hf_cops_pcmm_packetcable_gate_state_reason;
static int hf_cops_pcmm_packetcable_version_info_major;
static int hf_cops_pcmm_packetcable_version_info_minor;
static int hf_cops_pcmm_psid;
static int hf_cops_pcmm_synch_options_report_type;
static int hf_cops_pcmm_synch_options_synch_type;
static int hf_cops_pcmm_msg_receipt_key;
static int hf_cops_pcmm_userid;
static int hf_cops_pcmm_sharedresourceid;


/* Initialize the subtree pointers */
static int ett_cops;
static int ett_cops_ver_flags;
static int ett_cops_obj;
static int ett_cops_pr_obj;
static int ett_cops_obj_data;
static int ett_cops_r_type_flags;
static int ett_cops_itf;
static int ett_cops_reason;
static int ett_cops_decision;
static int ett_cops_error;
static int ett_cops_clientsi;
static int ett_cops_asn1;
static int ett_cops_gperror;
static int ett_cops_cperror;
static int ett_cops_pdp;
static int ett_cops_pcmm_gate_spec_flags;
static int ett_cops_pcmm_gate_spec_session_class_id;

static expert_field ei_cops_pepid_not_null;
static expert_field ei_cops_trailing_garbage;
static expert_field ei_cops_bad_cops_object_length;
static expert_field ei_cops_bad_cops_pr_object_length;
static expert_field ei_cops_unknown_c_num;
/* static expert_field ei_cops_unknown_s_num; */

/* For PacketCable */
static int ett_cops_subtree;

static int ett_docsis_request_transmission_policy;

static dissector_handle_t cops_handle;

static int* const docsis_request_transmission_policies[] = {
  &hf_cops_pcmm_request_transmission_policy_sf_all_cm,
  &hf_cops_pcmm_request_transmission_policy_sf_priority,
  &hf_cops_pcmm_request_transmission_policy_sf_request_for_request,
  &hf_cops_pcmm_request_transmission_policy_sf_data_for_data,
  &hf_cops_pcmm_request_transmission_policy_sf_piggyback,
  &hf_cops_pcmm_request_transmission_policy_sf_concatenate,
  &hf_cops_pcmm_request_transmission_policy_sf_fragment,
  &hf_cops_pcmm_request_transmission_policy_sf_suppress,
  &hf_cops_pcmm_request_transmission_policy_sf_drop_packets,
  NULL
};

/* For request/response matching */
typedef struct _cops_conv_info_t {
    wmem_map_t *pdus_tree;
} cops_conv_info_t;

typedef struct _cops_call_t
{
    uint8_t op_code;
    bool solicited;
    uint32_t req_num;
    uint32_t rsp_num;
    nstime_t req_time;
} cops_call_t;

static int dissect_cops_object(tvbuff_t *tvb, packet_info *pinfo, uint8_t op_code, uint32_t offset, proto_tree *tree, uint16_t client_type, uint32_t* handle_value);
static void dissect_cops_object_data(tvbuff_t *tvb, packet_info *pinfo, uint32_t offset, proto_tree *tree,
                                     uint8_t op_code, uint16_t client_type, uint8_t c_num, uint8_t c_type, int len, uint32_t* handle_value);

static void dissect_cops_pr_objects(tvbuff_t *tvb, packet_info *pinfo, uint32_t offset, proto_tree *tree, int pr_len,
                                                                        oid_info_t** oid_info_p, uint32_t** pprid_subids_p, unsigned* pprid_subids_len_p);
static int dissect_cops_pr_object_data(tvbuff_t *tvb, packet_info *pinfo, uint32_t offset, proto_tree *tree,
                                                                           uint8_t s_num, uint8_t s_type, int len,
                                                                           oid_info_t** oid_info_p, uint32_t** pprid_subids, unsigned* pprid_subids_len);

/* Added for PacketCable */
static proto_tree *info_to_cops_subtree(tvbuff_t *, proto_tree *, int, int, const char *);

static void cops_transaction_id(tvbuff_t *, packet_info *, proto_tree *, uint8_t, unsigned, uint32_t);
static void cops_subscriber_id_v4(tvbuff_t *, proto_tree *, unsigned, uint32_t);
static void cops_subscriber_id_v6(tvbuff_t *, proto_tree *, unsigned, uint32_t);
static void cops_gate_id(tvbuff_t *, proto_tree *, unsigned, uint32_t);
static void cops_activity_count(tvbuff_t *, proto_tree *, unsigned, uint32_t);
static void cops_gate_specs(tvbuff_t *, proto_tree *, unsigned, uint32_t);
static void cops_remote_gate_info(tvbuff_t *, proto_tree *, unsigned, uint32_t);
static void cops_packetcable_reason(tvbuff_t *, proto_tree *, unsigned, uint32_t);
static void cops_packetcable_error(tvbuff_t *, proto_tree *, unsigned, uint32_t);
static void cops_event_generation_info(tvbuff_t *, proto_tree *, unsigned, uint32_t);
static void cops_surveillance_parameters(tvbuff_t *, proto_tree *, unsigned, uint32_t);

static void cops_amid(tvbuff_t *, proto_tree *, unsigned, uint32_t);

static void cops_analyze_packetcable_dqos_obj(tvbuff_t *, packet_info *, proto_tree *, uint8_t, uint32_t);
static void cops_analyze_packetcable_mm_obj(tvbuff_t *, packet_info *, proto_tree *, uint8_t, uint32_t);

static bool cops_packetcable = true;

/* End of addition for PacketCable */

/* COPS PR Tags */

#define COPS_IPA    0           /* IP Address */
#define COPS_U32    2           /* Unsigned 32*/
#define COPS_TIT    3           /* TimeTicks */
#define COPS_OPQ    4           /* Opaque */
#define COPS_I64    10          /* Integer64 */
#define COPS_U64    11          /* Uinteger64 */

/* COPS PR Types */

#define COPS_NULL                0
#define COPS_INTEGER             1    /* l  */
#define COPS_OCTETSTR            2    /* c  */
#define COPS_OBJECTID            3    /* ul */
#define COPS_IPADDR              4    /* uc */
#define COPS_UNSIGNED32          5    /* ul */
#define COPS_TIMETICKS           7    /* ul */
#define COPS_OPAQUE              8    /* c  */
#define COPS_INTEGER64           10   /* ll */
#define COPS_UNSIGNED64          11   /* ull  */

typedef struct _COPS_CNV COPS_CNV;

struct _COPS_CNV
{
  unsigned ber_class;
  unsigned tag;
  int   syntax;
  const char *name;
  int* hfidp;
};

static const true_false_string tfs_upstream_downstream = { "Upstream", "Downstream" };

static COPS_CNV CopsCnv [] =
{
    {BER_CLASS_UNI, BER_UNI_TAG_NULL,           COPS_NULL,      "NULL" , &hf_cops_epd_null},
    {BER_CLASS_UNI, BER_UNI_TAG_INTEGER,        COPS_INTEGER,   "INTEGER", &hf_cops_epd_int},
    {BER_CLASS_UNI, BER_UNI_TAG_OCTETSTRING,    COPS_OCTETSTR,  "OCTET STRING", &hf_cops_epd_octets},
    {BER_CLASS_UNI, BER_UNI_TAG_OID,            COPS_OBJECTID,  "OBJECTID", &hf_cops_epd_oid},
    {BER_CLASS_APP, COPS_IPA,                   COPS_IPADDR,    "IPADDR", &hf_cops_epd_ipv4},
    {BER_CLASS_APP, COPS_U32,                   COPS_UNSIGNED32,"UNSIGNED32", &hf_cops_epd_u32},
    {BER_CLASS_APP, COPS_TIT,                   COPS_TIMETICKS, "TIMETICKS", &hf_cops_epd_ticks},
    {BER_CLASS_APP, COPS_OPQ,                   COPS_OPAQUE,    "OPAQUE", &hf_cops_epd_opaque},
    {BER_CLASS_APP, COPS_I64,                   COPS_INTEGER64, "INTEGER64", &hf_cops_epd_i64},
    {BER_CLASS_APP, COPS_U64,                   COPS_UNSIGNED64, "UNSIGNED64", &hf_cops_epd_u64},
    {BER_CLASS_ANY, 0,                          -1,              NULL, NULL}
};

static int cops_tag_cls2syntax ( unsigned tag, unsigned cls ) {
    COPS_CNV *cnv;


    cnv = CopsCnv;
    while (cnv->syntax != -1)
    {
        if (cnv->tag == tag && cnv->ber_class == cls)
        {
            return *(cnv->hfidp);
        }
        cnv++;
    }
    return hf_cops_epd_unknown;
}

static unsigned
get_cops_pdu_len(packet_info *pinfo _U_, tvbuff_t *tvb, int offset, void *data _U_)
{
    /*
     * Get the length of the COPS message.
     */
    return tvb_get_ntohl(tvb, offset + 4);
}

static int
dissect_cops_pdu(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void* data _U_)
{
    uint8_t op_code;
    uint16_t client_type;
    int object_len;
    proto_item *ti, *tv;
    proto_tree *cops_tree, *ver_flags_tree;
    uint32_t msg_len;
    uint32_t offset = 0;
    uint8_t ver_flags;
    int garbage;
    uint32_t handle_value = 0;

    /* variables for Request/Response tracking */
    unsigned i;
    bool is_solicited, is_request, is_response;
    conversation_t *conversation;
    cops_conv_info_t *cops_conv_info;
    cops_call_t *cops_call;
    wmem_array_t* pdus_array;
    nstime_t delta;

    col_set_str(pinfo->cinfo, COL_PROTOCOL, "COPS");
    col_clear(pinfo->cinfo, COL_INFO);

    op_code = tvb_get_uint8(tvb, 1);
    col_add_fstr(pinfo->cinfo, COL_INFO, "COPS %s",
                 val_to_str_const(op_code, cops_op_code_vals, "Unknown Op Code"));

    /* Currently used by PacketCable */
    client_type = tvb_get_ntohs(tvb, 2);

    ti = proto_tree_add_item(tree, proto_cops, tvb, offset, -1, ENC_NA);
    cops_tree = proto_item_add_subtree(ti, ett_cops);

    /* Version and flags share the same byte, put them in a subtree */
    ver_flags = tvb_get_uint8(tvb, offset);
    is_solicited = (lo_nibble(ver_flags) == 0x01);
    tv = proto_tree_add_uint_format(cops_tree, hf_cops_ver_flags, tvb, offset, 1,
                                    ver_flags, "Version: %u, Flags: %s",
                                    hi_nibble(ver_flags),
                                    val_to_str_const(lo_nibble(ver_flags), cops_flags_vals, "Unknown"));
    ver_flags_tree = proto_item_add_subtree(tv, ett_cops_ver_flags);
    proto_tree_add_uint(ver_flags_tree, hf_cops_version, tvb, offset, 1, ver_flags);
    proto_tree_add_uint(ver_flags_tree, hf_cops_flags, tvb, offset, 1, ver_flags);
    offset++;

    proto_tree_add_item(cops_tree, hf_cops_op_code, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset ++;
    proto_tree_add_item(cops_tree, hf_cops_client_type, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    msg_len = tvb_get_ntohl(tvb, offset);
    proto_tree_add_uint(cops_tree, hf_cops_msg_len, tvb, offset, 4, msg_len);
    offset += 4;

    while (tvb_reported_length_remaining(tvb, offset) >= COPS_OBJECT_HDR_SIZE) {
        object_len = dissect_cops_object(tvb, pinfo, op_code, offset, cops_tree, client_type, &handle_value);
        if (object_len < 0)
            return offset;
        offset += object_len;
    }

    garbage = tvb_reported_length_remaining(tvb, offset);
    if (garbage > 0) {
        proto_tree_add_expert_format(tree, pinfo, &ei_cops_trailing_garbage, tvb, offset, garbage, "Trailing garbage: %d byte%s", garbage, plurality(garbage, "", "s"));
    }

    /* Start request/response matching */

    /* handle is 0(or not present), and op_code doesn't allow null handle, return */
    /* TODO, add expert info for this abnormal */
    if (handle_value == 0 &&
        ( op_code != COPS_MSG_SSQ &&
          op_code != COPS_MSG_OPN &&
          op_code != COPS_MSG_CAT &&
          op_code != COPS_MSG_CC &&
          op_code != COPS_MSG_KA &&
          op_code != COPS_MSG_SSC) ) {
        return offset;
    }


    is_request  =
         op_code == COPS_MSG_REQ ||                   /* expects DEC */
        (op_code == COPS_MSG_DEC && !is_solicited) || /* expects RPT|DRQ */
/*                  COPS_MSG_RPT                         doesn't expect response */
/*                  COPS_MSG_DRQ                         doesn't expect response */
         op_code == COPS_MSG_SSQ ||                   /* expects RPT|DRQ|SSC */
         op_code == COPS_MSG_OPN ||                   /* expects CAT|CC */
/*                  COPS_MSG_CAT                         doesn't expect response */
/*                  COPS_MSG_CC                          doesn't expect response */
        (op_code == COPS_MSG_KA && !is_solicited);    /* expects KA from PDP, always initialized by PEP */
/*                  COPS_MSG_SSC                         doesn't expect response */

    is_response =
/*                  COPS_MSG_REQ                         request only */
        (op_code == COPS_MSG_DEC && is_solicited) ||  /* response only if reply REQ */
        (op_code == COPS_MSG_RPT && is_solicited) ||  /* response only if reply DEC/SSQ */
        (op_code == COPS_MSG_DRQ && is_solicited) ||  /* response only if reply DEC/SSQ */
/*                  COPS_MSG_SSQ                         request only */
/*                  COPS_MSG_OPN                         request only */
         op_code == COPS_MSG_CAT ||                   /* response for OPN */
        (op_code == COPS_MSG_CC  && is_solicited) ||  /* response for OPN */
        (op_code == COPS_MSG_KA  && is_solicited) ||  /* response for KA from PEP */
         op_code == COPS_MSG_SSC;                     /* response for SSQ */

    conversation = find_or_create_conversation(pinfo);
    cops_conv_info = (cops_conv_info_t *)conversation_get_proto_data(conversation, proto_cops);
    if (!cops_conv_info) {
        cops_conv_info = wmem_new(wmem_file_scope(), cops_conv_info_t);

        cops_conv_info->pdus_tree = wmem_map_new(wmem_file_scope(), g_direct_hash, g_direct_equal);
        conversation_add_proto_data(conversation, proto_cops, cops_conv_info);
    }

    if ( is_request ||
        (op_code == COPS_MSG_DEC && is_solicited) ) { /* DEC as response for REQ is considered as request, because it expects RPT|DRQ */

        pdus_array = (wmem_array_t *)wmem_map_lookup(cops_conv_info->pdus_tree, GUINT_TO_POINTER(handle_value));
        if (pdus_array == NULL) { /* This is the first request we've seen with this handle_value */
            pdus_array = wmem_array_new(wmem_file_scope(), sizeof(cops_call_t *));
            wmem_map_insert(cops_conv_info->pdus_tree, GUINT_TO_POINTER(handle_value), pdus_array);
        }

        if (!pinfo->fd->visited) {
            /*
             * XXX - yes, we're setting all the fields in this
             * structure, but there's padding between op_code
             * and solicited, and that can't be set.
             *
             * For some reason, on some platforms, valgrind is
             * complaining about a test of the solicited field
             * accessing uninitialized data, perhaps because
             * the 8 bytes containing op_code and solicited is
             * being loaded as a unit.  If the compiler is, for
             * example, turning a test of
             *
             *   cops_call->op_code == COPS_MSG_KA && !(cops_call->solicited)
             *
             * into a load of those 8 bytes and a comparison against a value
             * with op_code being COPS_MSG_KA, solicited being false (0),
             * *and* the padding being zero, it's buggy, but overly-"clever"
             * buggy compilers do exist, so....)
             *
             * So we use wmem_new0() to forcibly zero out the entire
             * structure before filling it in.
             */
            cops_call = wmem_new0(wmem_file_scope(), cops_call_t);
            cops_call->op_code = op_code;
            cops_call->solicited = is_solicited;
            cops_call->req_num = pinfo->num;
            cops_call->rsp_num = 0;
            cops_call->req_time = pinfo->abs_ts;
            wmem_array_append_one(pdus_array, cops_call);
        }
        else {
            for (i=0; i < wmem_array_get_count(pdus_array); i++) {
                cops_call = *(cops_call_t**)(wmem_array_index(pdus_array, i));
                if ( cops_call->req_num == pinfo->num
                  && cops_call->rsp_num != 0)  {
                    ti = proto_tree_add_uint_format(cops_tree, hf_cops_response_in, tvb, 0, 0, cops_call->rsp_num,
                                                      "Response to this request is in frame %u", cops_call->rsp_num);
                    proto_item_set_generated(ti);
                }
            }
        }
    }

    if (is_response) {
        pdus_array = (wmem_array_t *)wmem_map_lookup(cops_conv_info->pdus_tree, GUINT_TO_POINTER(handle_value));

        if (pdus_array == NULL) /* There's no request with this handle value */
            return offset;

        if (!pinfo->fd->visited) {
            for (i=0; i < wmem_array_get_count(pdus_array); i++) {
                cops_call = *(cops_call_t**)(wmem_array_index(pdus_array, i));

                if (nstime_cmp(&pinfo->abs_ts, &cops_call->req_time) <= 0 || cops_call->rsp_num != 0)
                    continue;

                if (
                    ( (cops_call->op_code == COPS_MSG_REQ) &&
                        (op_code == COPS_MSG_DEC && is_solicited) ) ||
                    ( (cops_call->op_code == COPS_MSG_DEC) &&
                        ( (op_code == COPS_MSG_RPT && is_solicited) ||
                          (op_code == COPS_MSG_DRQ && is_solicited) ) ) ||
                    ( (cops_call->op_code == COPS_MSG_SSQ) &&
                        ( (op_code == COPS_MSG_RPT && is_solicited) ||
                          (op_code == COPS_MSG_DRQ && is_solicited) ||
                          (op_code == COPS_MSG_SSC) ) ) ||
                    ( (cops_call->op_code == COPS_MSG_OPN) &&
                        (op_code == COPS_MSG_CAT ||
                         op_code == COPS_MSG_CC) ) ||
                    ( (cops_call->op_code == COPS_MSG_KA && !(cops_call->solicited)) &&
                        (op_code == COPS_MSG_KA && is_solicited) ) ) {
                    cops_call->rsp_num = pinfo->num;
                    break;
                }
            }
        }
        else {
            for (i=0; i < wmem_array_get_count(pdus_array); i++) {
                cops_call = *(cops_call_t**)(wmem_array_index(pdus_array, i));
                if ( cops_call->rsp_num == pinfo->num ) {
                    ti = proto_tree_add_uint_format(cops_tree, hf_cops_response_to, tvb, 0, 0, cops_call->req_num,
                                                      "Response to a request in frame %u", cops_call->req_num);
                    proto_item_set_generated(ti);

                    nstime_delta(&delta, &pinfo->abs_ts, &cops_call->req_time);
                    ti = proto_tree_add_time(cops_tree, hf_cops_response_time, tvb, 0, 0, &delta);
                    proto_item_set_generated(ti);

                    break;
                }
            }
        }
    }

    return tvb_reported_length(tvb);
}

/* Code to actually dissect the packets */
static int
dissect_cops(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void* data)
{
    tcp_dissect_pdus(tvb, pinfo, tree, cops_desegment, 8,
                     get_cops_pdu_len, dissect_cops_pdu, data);
    return tvb_reported_length(tvb);
}

static const char *cops_c_type_to_str(uint8_t c_num, uint8_t c_type)
{
    switch (c_num) {
    case COPS_OBJ_HANDLE:
        if (c_type == 1)
            return "Client Handle";
        break;
    case COPS_OBJ_IN_INT:
    case COPS_OBJ_OUT_INT:
        if (c_type == 1)
            return "IPv4 Address + Interface";
        else if (c_type == 2)
            return "IPv6 Address + Interface";
        break;
    case COPS_OBJ_DECISION:
    case COPS_OBJ_LPDPDECISION:
        if (c_type == 1)
            return "Decision Flags (Mandatory)";
        else if (c_type == 2)
            return "Stateless Data";
        else if (c_type == 3)
            return "Replacement Data";
        else if (c_type == 4)
            return "Client Specific Decision Data";
        else if (c_type == 5)
            return "Named Decision Data";
        break;
    case COPS_OBJ_CLIENTSI:
        if (c_type == 1)
            return "Signaled ClientSI";
        else if (c_type == 2)
            return "Named ClientSI";
        break;
    case COPS_OBJ_KATIMER:
        if (c_type == 1)
            return "Keep-alive timer value";
        break;
    case COPS_OBJ_PDPREDIRADDR:
    case COPS_OBJ_LASTPDPADDR:
        if (c_type == 1)
            return "IPv4 Address + TCP Port";
        else if (c_type == 2)
            return "IPv6 Address + TCP Port";
        break;
    case COPS_OBJ_ACCTTIMER:
        if (c_type == 1)
            return "Accounting timer value";
        break;
    case COPS_OBJ_INTEGRITY:
        if (c_type == 1)
            return "HMAC digest";
        break;
    }

    return "";
}

static int dissect_cops_object(tvbuff_t *tvb, packet_info *pinfo, uint8_t op_code, uint32_t offset, proto_tree *tree, uint16_t client_type, uint32_t* handle_value)
{
    int object_len, contents_len;
    uint8_t c_num, c_type;
    proto_item *ti;
    proto_tree *obj_tree;
    const char *type_str;

    object_len = tvb_get_ntohs(tvb, offset);
    if (object_len < COPS_OBJECT_HDR_SIZE) {
        /* Bogus! */
        ti = proto_tree_add_uint(tree, hf_cops_obj_len, tvb, offset, 2, object_len);
        expert_add_info_format(pinfo, ti, &ei_cops_bad_cops_object_length,
                                    "Bad COPS object length: %u, should be at least %u",
                                    object_len, COPS_OBJECT_HDR_SIZE);
        return -1;
    }
    c_num = tvb_get_uint8(tvb, offset + 2);
    c_type = tvb_get_uint8(tvb, offset + 3);

    ti = proto_tree_add_uint_format(tree, hf_cops_obj_c_num, tvb, offset, object_len, c_num,
                                    "%s: %s", val_to_str_const(c_num, cops_c_num_vals, "Unknown"),
                                    cops_c_type_to_str(c_num, c_type));
    obj_tree = proto_item_add_subtree(ti, ett_cops_obj);

    proto_tree_add_uint(obj_tree, hf_cops_obj_len, tvb, offset, 2, object_len);
    offset += 2;

    proto_tree_add_uint(obj_tree, hf_cops_obj_c_num, tvb, offset, 1, c_num);
    offset++;

    type_str = cops_c_type_to_str(c_num, c_type);
    proto_tree_add_uint_format_value(obj_tree, hf_cops_obj_c_type, tvb, offset, 1, c_type,
                                     "%s%s%u%s",
                                     type_str,
                                     strlen(type_str) ? " (" : "",
                                     c_type,
                                     strlen(type_str) ? ")" : "");
    offset++;

    contents_len = object_len - COPS_OBJECT_HDR_SIZE;
    dissect_cops_object_data(tvb, pinfo, offset, obj_tree, op_code, client_type, c_num, c_type, contents_len, handle_value);

    /* Pad to 32bit boundary */
    if (object_len % sizeof (uint32_t))
        object_len += ((int)sizeof (uint32_t) - object_len % (int)sizeof (uint32_t));

    return object_len;
}

static void dissect_cops_pr_objects(tvbuff_t *tvb, packet_info *pinfo, uint32_t offset, proto_tree *tree, int pr_len,
                                    oid_info_t** oid_info_p, uint32_t** pprid_subids_p, unsigned* pprid_subids_len_p)
{
    int object_len, contents_len;
    uint8_t s_num, s_type;
    const char *type_str;
    int ret;
    proto_tree *cops_pr_tree, *obj_tree;
    proto_item *ti;

    cops_pr_tree = proto_item_add_subtree(tree, ett_cops_pr_obj);

    while (pr_len >= COPS_OBJECT_HDR_SIZE) {
        object_len = tvb_get_ntohs(tvb, offset);
        if (object_len < COPS_OBJECT_HDR_SIZE) {
            /* Bogus! */
            ti = proto_tree_add_uint(cops_pr_tree, hf_cops_obj_len, tvb, offset, 2, object_len);
            expert_add_info_format(pinfo, ti, &ei_cops_bad_cops_pr_object_length,
                                        "Bad COPS-PR object length: %u, should be at least %u",
                                        object_len, COPS_OBJECT_HDR_SIZE);
            return;
        }
        s_num = tvb_get_uint8(tvb, offset + 2);

        ti = proto_tree_add_uint_format(cops_pr_tree, hf_cops_obj_s_num, tvb, offset, object_len, s_num,
                                        "%s", val_to_str_const(s_num, cops_s_num_vals, "Unknown"));
        obj_tree = proto_item_add_subtree(ti, ett_cops_pr_obj);

        proto_tree_add_uint(obj_tree, hf_cops_obj_len, tvb, offset, 2, object_len);
        offset += 2;
        pr_len -= 2;

        proto_tree_add_uint(obj_tree, hf_cops_obj_s_num, tvb, offset, 1, s_num);
        offset++;
        pr_len--;

        s_type = tvb_get_uint8(tvb, offset);
        type_str = val_to_str_const(s_type, cops_s_type_vals, "Unknown");
        proto_tree_add_uint_format_value(obj_tree, hf_cops_obj_s_type, tvb, offset, 1, s_type,
                                         "%s%s%u%s",
                                         type_str,
                                         strlen(type_str) ? " (" : "",
                                         s_type,
                                         strlen(type_str) ? ")" : "");
        offset++;
        pr_len--;

        contents_len = object_len - COPS_OBJECT_HDR_SIZE;
        ret = dissect_cops_pr_object_data(tvb, pinfo, offset, obj_tree, s_num, s_type, contents_len,
                                          oid_info_p, pprid_subids_p, pprid_subids_len_p);
        if (ret < 0)
            break;

        /* Pad to 32bit boundary */
        if (object_len % sizeof (uint32_t))
            object_len += ((int)sizeof (uint32_t) - object_len % (int)sizeof (uint32_t));

        pr_len -= object_len - COPS_OBJECT_HDR_SIZE;
        offset += object_len - COPS_OBJECT_HDR_SIZE;
    }
}

static void dissect_cops_object_data(tvbuff_t *tvb, packet_info *pinfo, uint32_t offset, proto_tree *tree,
                                     uint8_t op_code, uint16_t client_type, uint8_t c_num, uint8_t c_type, int len, uint32_t* handle_value)
{
    proto_item *ti;
    proto_tree *r_type_tree, *itf_tree, *reason_tree, *dec_tree, *error_tree, *clientsi_tree, *pdp_tree;
    uint16_t r_type, m_type, reason, reason_sub, cmd_code, cmd_flags, error, error_sub,
            tcp_port, katimer, accttimer;
    uint32_t ifindex;
    ws_in6_addr ipv6addr;
    oid_info_t* oid_info = NULL;
    uint32_t* pprid_subids = NULL;
    unsigned pprid_subids_len = 0;

    switch (c_num) {
    case COPS_OBJ_HANDLE:       /* handle is a variable-length field, however 32bit seems enough for most of the applications */
        if (len >= 4) {
            offset += (len-4);  /* for handle longer than 32bit, only take lowest 32 bits as handle */
            *handle_value = tvb_get_ntohl(tvb, offset);
            proto_tree_add_item(tree, hf_cops_handle, tvb, offset, 4, ENC_BIG_ENDIAN);
        }
        break;
    case COPS_OBJ_CONTEXT:
        r_type = tvb_get_ntohs(tvb, offset);
        m_type = tvb_get_ntohs(tvb, offset + 2);
        r_type_tree = proto_tree_add_subtree_format(tree, tvb, offset, 4, ett_cops_r_type_flags, NULL,
                                 "Contents: R-Type: %s, M-Type: %u",
                                 val_to_str_const(r_type, cops_r_type_vals, "Unknown"), m_type);

        proto_tree_add_uint(r_type_tree, hf_cops_r_type_flags, tvb, offset, 2, r_type);
        offset += 2;
        proto_tree_add_uint(r_type_tree, hf_cops_m_type_flags, tvb, offset, 2, m_type);

        break;
    case COPS_OBJ_IN_INT:
    case COPS_OBJ_OUT_INT:
        if (c_type == 1) {          /* IPv4 */
            ifindex = tvb_get_ntohl(tvb, offset + 4);
            itf_tree = proto_tree_add_subtree_format(tree, tvb, offset, 8, ett_cops_itf, NULL,
                                     "Contents: IPv4 address %s, ifIndex: %u",
                                     tvb_ip_to_str(pinfo->pool, tvb, offset), ifindex);
            proto_tree_add_item(itf_tree,
                                (c_num == COPS_OBJ_IN_INT) ? hf_cops_in_int_ipv4 : hf_cops_out_int_ipv4,
                                tvb, offset, 4, ENC_BIG_ENDIAN);
            offset += 4;
        } else if (c_type == 2) {   /* IPv6 */
            ifindex = tvb_get_ntohl(tvb, offset + (int)sizeof ipv6addr);
            itf_tree = proto_tree_add_subtree_format(tree, tvb, offset, 20, ett_cops_itf, NULL,
                                     "Contents: IPv6 address %s, ifIndex: %u",
                                     tvb_ip6_to_str(pinfo->pool, tvb, offset), ifindex);
            proto_tree_add_item(itf_tree,
                                (c_num == COPS_OBJ_IN_INT) ? hf_cops_in_int_ipv6 : hf_cops_out_int_ipv6,
                                tvb, offset, 16, ENC_NA);
            offset += 16;
        } else {
            break;
        }
        proto_tree_add_uint(itf_tree, hf_cops_int_ifindex, tvb, offset, 4, ifindex);

        break;
    case COPS_OBJ_REASON:
        reason = tvb_get_ntohs(tvb, offset);
        reason_sub = tvb_get_ntohs(tvb, offset + 2);
        reason_tree = proto_tree_add_subtree_format(tree, tvb, offset, 4, ett_cops_reason, NULL,
                                 "Contents: Reason-Code: %s, Reason Sub-code: 0x%04x",
                                 val_to_str_const(reason, cops_reason_vals, "<Unknown value>"), reason_sub);
        proto_tree_add_uint(reason_tree, hf_cops_reason, tvb, offset, 2, reason);
        offset += 2;
        if (reason == 13) { /* RFC 2748 2.2.5 */
            proto_tree_add_uint_format_value(reason_tree, hf_cops_reason_sub, tvb, offset, 2,
                                reason_sub, "Unknown object's C-Num %u, C-Type %u",
                                tvb_get_uint8(tvb, offset), tvb_get_uint8(tvb, offset + 1));
        } else
            proto_tree_add_uint(reason_tree, hf_cops_reason_sub, tvb, offset, 2, reason_sub);

        break;
    case COPS_OBJ_DECISION:
    case COPS_OBJ_LPDPDECISION:
        if (c_type == 1) {
            cmd_code = tvb_get_ntohs(tvb, offset);
            cmd_flags = tvb_get_ntohs(tvb, offset + 2);
            dec_tree = proto_tree_add_subtree_format(tree, tvb, offset, 4, ett_cops_decision, NULL, "Contents: Command-Code: %s, Flags: %s",
                                     val_to_str_const(cmd_code, cops_dec_cmd_code_vals, "<Unknown value>"),
                                     val_to_str_const(cmd_flags, cops_dec_cmd_flag_vals, "<Unknown flag>"));
            proto_tree_add_uint(dec_tree, hf_cops_dec_cmd_code, tvb, offset, 2, cmd_code);
            offset += 2;
            proto_tree_add_uint(dec_tree, hf_cops_dec_flags, tvb, offset, 2, cmd_flags);
        } else if (c_type == 5) { /*COPS-PR Data*/
            dec_tree = proto_tree_add_subtree_format(tree, tvb, offset, len, ett_cops_decision, NULL, "Contents: %d bytes", len);
            dissect_cops_pr_objects(tvb, pinfo, offset, dec_tree, len, &oid_info, &pprid_subids, &pprid_subids_len);
        }

        /* PacketCable : Analyze the remaining data if available */
        if (client_type == COPS_CLIENT_PC_DQOS && c_type == 4) {
            cops_analyze_packetcable_dqos_obj(tvb, pinfo, tree, op_code, offset);
        } else if (client_type == COPS_CLIENT_PC_MM && c_type == 4) {
            cops_analyze_packetcable_mm_obj(tvb, pinfo, tree, op_code, offset);
        }

        break;
    case COPS_OBJ_ERROR:
        if (c_type != 1)
            break;

        error = tvb_get_ntohs(tvb, offset);
        error_sub = tvb_get_ntohs(tvb, offset + 2);
        error_tree = proto_tree_add_subtree_format(tree, tvb, offset, 4, ett_cops_error, NULL,
                                 "Contents: Error-Code: %s, Error Sub-code: 0x%04x",
                                 val_to_str_const(error, cops_error_vals, "<Unknown value>"), error_sub);
        proto_tree_add_uint(error_tree, hf_cops_error, tvb, offset, 2, error);
        offset += 2;
        if (error == 13) { /* RFC 2748 2.2.8 */
            proto_tree_add_uint_format_value(error_tree, hf_cops_error_sub, tvb, offset, 2,
                                error_sub, "Unknown object's C-Num %u, C-Type %u",
                                tvb_get_uint8(tvb, offset), tvb_get_uint8(tvb, offset + 1));
        } else
            proto_tree_add_uint(error_tree, hf_cops_error_sub, tvb, offset, 2, error_sub);

        break;
    case COPS_OBJ_CLIENTSI:

        /* For PacketCable */
        if (client_type == COPS_CLIENT_PC_DQOS && c_type == 1) {
            cops_analyze_packetcable_dqos_obj(tvb, pinfo, tree, op_code, offset);
            break;
        } else if (client_type == COPS_CLIENT_PC_MM && c_type == 1) {
            cops_analyze_packetcable_mm_obj(tvb, pinfo, tree, op_code, offset);
            break;
        }

        if (c_type != 2) /*Not COPS-PR data*/
            break;

        clientsi_tree = proto_tree_add_subtree_format(tree, tvb, offset, 4, ett_cops_clientsi, NULL, "Contents: %d bytes", len);

        dissect_cops_pr_objects(tvb, pinfo, offset, clientsi_tree, len, &oid_info, &pprid_subids, &pprid_subids_len);

        break;
    case COPS_OBJ_KATIMER:
        if (c_type != 1)
            break;

        katimer = tvb_get_ntohs(tvb, offset + 2);
        if (katimer == 0) {
            proto_tree_add_uint_format_value(tree, hf_cops_katimer, tvb, offset + 2, 2, katimer, "0 (infinity)");
        } else {
            proto_tree_add_item(tree, hf_cops_katimer, tvb, offset + 2, 2, ENC_BIG_ENDIAN);
        }
        break;
    case COPS_OBJ_PEPID:
        if (c_type != 1)
            break;

        if (tvb_strnlen(tvb, offset, len) == -1) {
            ti = proto_tree_add_item(tree, hf_cops_pepid, tvb, offset, len, ENC_ASCII);
            expert_add_info(pinfo, ti, &ei_cops_pepid_not_null);
        }
        else
            proto_tree_add_item(tree, hf_cops_pepid, tvb, offset,
                                tvb_strnlen(tvb, offset, len) + 1, ENC_ASCII);

        break;
    case COPS_OBJ_REPORT_TYPE:
        if (c_type != 1)
            break;

        proto_tree_add_item(tree, hf_cops_report_type, tvb, offset, 2, ENC_BIG_ENDIAN);

        break;
    case COPS_OBJ_PDPREDIRADDR:
    case COPS_OBJ_LASTPDPADDR:
        if (c_type == 1) {          /* IPv4 */
            tcp_port = tvb_get_ntohs(tvb, offset + 4 + 2);
            pdp_tree = proto_tree_add_subtree_format(tree, tvb, offset, 8, ett_cops_pdp, NULL,
                                     "Contents: IPv4 address %s, TCP Port Number: %u",
                                     tvb_ip_to_str(pinfo->pool, tvb, offset), tcp_port);
            proto_tree_add_item(pdp_tree,
                                (c_num == COPS_OBJ_PDPREDIRADDR) ? hf_cops_pdprediraddr_ipv4 : hf_cops_lastpdpaddr_ipv4,
                                tvb, offset, 4, ENC_BIG_ENDIAN);
            offset += 4;
        } else if (c_type == 2) {   /* IPv6 */
            tcp_port = tvb_get_ntohs(tvb, offset + (int)sizeof ipv6addr + 2);
            pdp_tree = proto_tree_add_subtree_format(tree, tvb, offset, 20, ett_cops_pdp, NULL,
                                     "Contents: IPv6 address %s, TCP Port Number: %u",
                                     tvb_ip6_to_str(pinfo->pool, tvb, offset), tcp_port);
            proto_tree_add_item(pdp_tree,
                                (c_num == COPS_OBJ_PDPREDIRADDR) ? hf_cops_pdprediraddr_ipv6 : hf_cops_lastpdpaddr_ipv6,
                                tvb, offset, 16, ENC_NA);
            offset += 16;
        } else {
            break;
        }
        offset += 2;
        proto_tree_add_uint(pdp_tree, hf_cops_pdp_tcp_port, tvb, offset, 2, tcp_port);

        break;
    case COPS_OBJ_ACCTTIMER:
        if (c_type != 1)
            break;

        accttimer = tvb_get_ntohs(tvb, offset + 2);
        if (accttimer == 0) {
            proto_tree_add_uint_format_value(tree, hf_cops_accttimer, tvb, offset + 2, 2, accttimer,
                "0 (there SHOULD be no unsolicited accounting updates)");
        } else {
            proto_tree_add_item(tree, hf_cops_accttimer, tvb, offset + 2, 2, ENC_BIG_ENDIAN);
        }
        break;
    case COPS_OBJ_INTEGRITY:
        if (c_type != 1)
            break;      /* Not HMAC digest */

        proto_tree_add_item(tree, hf_cops_key_id, tvb, offset, 4, ENC_BIG_ENDIAN);
        proto_tree_add_item(tree, hf_cops_seq_num, tvb, offset + 4, 4, ENC_BIG_ENDIAN);
        proto_tree_add_item(tree, hf_cops_keyed_message_digest, tvb, offset + 8 , len - 8, ENC_NA);

        break;
    default:
        proto_tree_add_expert_format(tree, pinfo, &ei_cops_unknown_c_num, tvb, offset, len, "Unknown C-Num %d, Contents: %d bytes", c_num, len);
        break;
    }
}

static unsigned redecode_oid(wmem_allocator_t* scope, uint32_t* pprid_subids, unsigned pprid_subids_len, uint8_t* encoded_subids, unsigned encoded_len, uint32_t** subids_p) {
    unsigned i;
    unsigned n = 0;
    uint32_t subid = 0;
    uint32_t* subids;
    uint32_t* subid_overflow;

    for (i=0; i<encoded_len; i++) { if (! (encoded_subids[i] & 0x80 )) n++; }

    *subids_p = subids = (uint32_t *)wmem_alloc(scope, sizeof(uint32_t)*(n+pprid_subids_len));
    subid_overflow = subids+n+pprid_subids_len;
    for (i=0;i<pprid_subids_len;i++) subids[i] = pprid_subids[i];

    subids += pprid_subids_len;


    for (i=0; i<encoded_len; i++){
        uint8_t byte = encoded_subids[i];

        subid <<= 7;
        subid |= byte & 0x7F;

        if (byte & 0x80) {
            continue;
        }

        DISSECTOR_ASSERT(subids < subid_overflow);
        *subids++ = subid;
        subid = 0;
    }

    return pprid_subids_len+n;
}


static int dissect_cops_pr_object_data(tvbuff_t *tvb, packet_info *pinfo, uint32_t offset, proto_tree *tree,
                                       uint8_t s_num, uint8_t s_type, int len,
                                       oid_info_t** oid_info_p, uint32_t** pprid_subids, unsigned* pprid_subids_len) {
    proto_tree *asn_tree, *gperror_tree, *cperror_tree;
    uint16_t gperror=0, gperror_sub=0, cperror=0, cperror_sub=0;
    asn1_ctx_t actx;

    memset(&actx,0,sizeof(actx));
    actx.pinfo = pinfo;

    switch (s_num){
    case COPS_OBJ_PPRID: {
        tvbuff_t* oid_tvb = NULL;

        if (s_type != 1) /* Not Prefix Provisioning Instance Identifier (PPRID) */
            break;
        /* Never tested this branch */
        asn_tree = proto_tree_add_subtree(tree, tvb, offset, len, ett_cops_asn1, NULL, "Contents:");

        dissect_ber_object_identifier(false, &actx, asn_tree, tvb, offset, hf_cops_pprid_oid, &oid_tvb);

        if (oid_tvb) {
            int encoid_len;
            uint8_t* encoid;

            encoid_len = tvb_reported_length_remaining(oid_tvb,0);
            if (encoid_len > 0) {
                encoid = (uint8_t*)tvb_memdup(pinfo->pool,oid_tvb,0,encoid_len);
                (*pprid_subids_len) = oid_encoded2subid(pinfo->pool, encoid, encoid_len, pprid_subids);
            }
        }
        break;
    }
    case COPS_OBJ_PRID: {
        uint32_t* subids;
        unsigned subids_len;
        unsigned matched;
        unsigned left;
        int8_t ber_class;
        bool ber_pc;
        int32_t ber_tag;
        unsigned encoid_len;
        uint8_t* encoid;
        oid_info_t* oid_info;

        if (s_type != 1) break; /* Not Provisioning Instance Identifier (PRID) */

        asn_tree = proto_tree_add_subtree(tree, tvb, offset, len, ett_cops_asn1, NULL, "Contents:");

        offset = get_ber_identifier(tvb, offset, &ber_class, &ber_pc, &ber_tag);
        offset = get_ber_length(tvb, offset, &encoid_len, NULL);

        /* TODO: check pc, class and tag */

        encoid = (uint8_t*)tvb_memdup(pinfo->pool,tvb,offset,encoid_len);

        if (*pprid_subids) {
            /* Never tested this branch */
            subids_len = redecode_oid(pinfo->pool, *pprid_subids, *pprid_subids_len, encoid, encoid_len, &subids);
            encoid_len = oid_subid2encoded(pinfo->pool, subids_len, subids, &encoid);
        } else {
            subids_len = oid_encoded2subid(pinfo->pool, encoid, encoid_len, &subids);
        }

        proto_tree_add_oid(asn_tree,hf_cops_prid_oid,tvb,offset,encoid_len,encoid);

        oid_info = oid_get(subids_len, subids, &matched, &left);

        /*
          TODO: from RFC 3159 find-out how the values are mapped;
          when instead of an oid for an xxEntry
          we have one describing a scalar or something else;
          what's below works in most cases but is not complete.
        */
        if (left <= 1 && oid_info->kind == OID_KIND_ROW) {
            *oid_info_p = oid_info;
        } else {
            *oid_info_p = NULL;
        }

        break;
    }
    case COPS_OBJ_EPD: {
        oid_info_t* oid_info;
        unsigned end_offset = offset + len;

        if (s_type != 1) break;/* Not Encoded Provisioning Instance Data (EPD) */

        asn_tree = proto_tree_add_subtree(tree, tvb, offset, len, ett_cops_asn1, NULL, "Contents:");

        /*
         * XXX: LAZINESS WARNING:
         * We are assuming that for the first element in the sequence
         * that describes an entry subid==1, and, that the subsequent elements
         * use ++subid; This is true for all IETF's PIBs (and good sense
         * indicates it should be this way) but AFAIK there's nothing in
         * SMIv2 that imposes this restriction.  -- a lazy lego
         */

        if(*oid_info_p) {
            if ((*oid_info_p)->kind == OID_KIND_ROW) {
                oid_info = (oid_info_t *)wmem_tree_lookup32((*oid_info_p)->children,1);
            } else {
                oid_info = NULL;
            }
        } else {
            oid_info = NULL;
        }


        while(offset < end_offset) {
            int8_t ber_class;
            bool ber_pc;
            int32_t ber_tag;
            uint32_t ber_length;
            bool ber_ind;
            int hfid;

            offset = get_ber_identifier(tvb, offset, &ber_class, &ber_pc, &ber_tag);
            offset = get_ber_length(tvb, offset, &ber_length, &ber_ind);

            if (oid_info) {
                /*
                 * XXX: LAZINESS WARNING:
                 * We are assuming that the value of the sequenced item is of
                 * the right class, the right type and the right length.
                 * We should check that to avoid throwing a Malformed packet and
                 * keep dissecting.
                 * We should verify the class and the tag match what we expect as well,
                 * but COPS and SNMP use different tags (&#@$!) so the typedata in oid_info_t
                 * does not work here.
                 * -- a lazy lego
                 */
                hfid = oid_info->value_hfid;
                oid_info = (oid_info_t *)wmem_tree_lookup32((*oid_info_p)->children,oid_info->subid+1);
            } else
                hfid = cops_tag_cls2syntax( ber_tag, ber_class );
            switch (proto_registrar_get_ftype(hfid)) {

            case FT_INT8:
            case FT_INT16:
            case FT_INT24:
            case FT_INT32:
            case FT_INT64:
            case FT_UINT8:
            case FT_UINT16:
            case FT_UINT24:
            case FT_UINT32:
            case FT_UINT64:
            case FT_BOOLEAN:
            case FT_FLOAT:
            case FT_DOUBLE:
            case FT_IPv4:
                proto_tree_add_item(asn_tree,hfid,tvb,offset,ber_length,ENC_BIG_ENDIAN);
                break;

            case FT_STRING:
                proto_tree_add_item(asn_tree,hfid,tvb,offset,ber_length,ENC_ASCII|ENC_NA);
                break;

            default:
                proto_tree_add_item(asn_tree,hfid,tvb,offset,ber_length,ENC_NA);
                break;
            }

            offset += ber_length;
        }

        (*oid_info_p) = NULL;
        break;
    }
    case COPS_OBJ_ERRPRID: {
        if (s_type != 1) break; /*Not  Error Provisioning Instance Identifier (ErrorPRID)*/

        asn_tree = proto_tree_add_subtree(tree, tvb, offset, len, ett_cops_asn1, NULL, "Contents:");

        dissect_ber_object_identifier(false, &actx, asn_tree, tvb, offset, hf_cops_errprid_oid, NULL);

        break;
    }
    case COPS_OBJ_GPERR:
        if (s_type != 1) /* Not Global Provisioning Error Object (GPERR) */
            break;

        gperror = tvb_get_ntohs(tvb, offset);
        gperror_sub = tvb_get_ntohs(tvb, offset + 2);
        gperror_tree = proto_tree_add_subtree_format(tree, tvb, offset, 4, ett_cops_gperror, NULL,
                                 "Contents: Error-Code: %s, Error Sub-code: 0x%04x",
                                 val_to_str_const(gperror, cops_gperror_vals, "<Unknown value>"), gperror_sub);
        proto_tree_add_uint(gperror_tree, hf_cops_gperror, tvb, offset, 2, gperror);
        offset += 2;
        if (gperror == 13) { /* RFC 3084 4.4 */
            proto_tree_add_uint_format_value(gperror_tree, hf_cops_gperror_sub, tvb, offset, 2,
                                gperror_sub, "Unknown object's C-Num %u, C-Type %u",
                                tvb_get_uint8(tvb, offset), tvb_get_uint8(tvb, offset + 1));
        } else
            proto_tree_add_uint(gperror_tree, hf_cops_gperror_sub, tvb, offset, 2, gperror_sub);

        break;
    case COPS_OBJ_CPERR:
        if (s_type != 1) /*Not PRC Class Provisioning Error Object (CPERR) */
            break;

        cperror = tvb_get_ntohs(tvb, offset);
        cperror_sub = tvb_get_ntohs(tvb, offset + 2);
        cperror_tree = proto_tree_add_subtree_format(tree, tvb, offset, 4, ett_cops_gperror, NULL,
                                 "Contents: Error-Code: %s, Error Sub-code: 0x%04x",
                                 val_to_str_const(gperror, cops_gperror_vals, "<Unknown value>"), gperror_sub);
        proto_tree_add_uint(cperror_tree, hf_cops_cperror, tvb, offset, 2, cperror);
        offset += 2;
        if (cperror == 13) { /* RFC 3084 4.5 */
            proto_tree_add_uint_format_value(cperror_tree, hf_cops_cperror_sub, tvb, offset, 2, cperror_sub,
                                "Unknown object's S-Num %u, C-Type %u",
                                tvb_get_uint8(tvb, offset), tvb_get_uint8(tvb, offset + 1));
        } else
            proto_tree_add_uint(cperror_tree, hf_cops_cperror_sub, tvb, offset, 2, cperror_sub);

        break;
    default:
        proto_tree_add_bytes_format_value(tree, hf_cops_integrity_contents, tvb, offset, len, NULL, "%d bytes", len);
        break;
    }

    return 0;
}


/* Additions for PacketCable ( Added by Dick Gooris, Lucent Technologies ) */

/* Print the subtree information for cops */
static proto_tree *
info_to_cops_subtree(tvbuff_t *tvb, proto_tree *st, int n, int offset, const char *str) {
    proto_item *tv;

    tv  = proto_tree_add_none_format( st, hf_cops_subtree, tvb, offset, n, "%s", str);
    return proto_item_add_subtree( tv, ett_cops_subtree );
}

/* Cops - Section : D-QoS Transaction ID */
static void
cops_transaction_id(tvbuff_t *tvb, packet_info *pinfo, proto_tree *st, uint8_t op_code, unsigned n, uint32_t offset) {

    proto_tree *stt;
    proto_item *ti;
    uint32_t code;

    /* Create a subtree */
    stt = info_to_cops_subtree(tvb,st,n,offset,"D-QoS Transaction ID");
    offset += 4;

    /* Transaction Identifier */
    proto_tree_add_item(stt, hf_cops_pc_dqos_transaction_id, tvb, offset, 2, ENC_BIG_ENDIAN);
    // Keep for backwards compatibility
    ti = proto_tree_add_item(stt, hf_cops_pc_transaction_id, tvb, offset, 2, ENC_BIG_ENDIAN);
    PROTO_ITEM_SET_HIDDEN(ti);
    offset +=2;

    /* Gate Command Type */
    proto_tree_add_item_ret_uint(stt, hf_cops_pc_dqos_gate_command_type,tvb, offset, 2, ENC_BIG_ENDIAN, &code);
    // Keep for backwards compatibility
    ti = proto_tree_add_item(stt, hf_cops_pc_gate_command_type, tvb, offset, 2, ENC_BIG_ENDIAN);
    PROTO_ITEM_SET_HIDDEN(ti);

    /* Write the right data into the 'info field' on the Gui */
    col_add_str(pinfo->cinfo, COL_INFO, wmem_strdup_printf(pinfo->pool, "COPS %-20s - %s",
        val_to_str_const(op_code, cops_op_code_vals, "Unknown"), val_to_str_const(code, table_cops_dqos_transaction_id, "Unknown")));
}

/* Cops - Section : Subscriber IDv4 */
static void
cops_subscriber_id_v4(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

    proto_item *tv;

    /* Create a subtree */
    tv = info_to_cops_subtree(tvb,st,n,offset,"Subscriber ID (IPv4)");
    offset += 4;

    /* Subscriber Identifier */
    proto_tree_add_item(tv, hf_cops_pc_subscriber_id_ipv4, tvb, offset, 4, ENC_BIG_ENDIAN);
}

/* Cops - Section : Subscriber IDv6 */
static void
cops_subscriber_id_v6(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

    proto_item *tv;

    /* Create a subtree */
    tv = info_to_cops_subtree(tvb,st,n,offset,"Subscriber ID (IPv6)");
    offset += 4;

    /* Subscriber Identifier */
    proto_tree_add_item(tv, hf_cops_pc_subscriber_id_ipv6, tvb, offset, 16, ENC_NA);
}

/* Cops - Section : Gate ID */
static void
cops_gate_id(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

    proto_tree *stt;

    /* Create a subtree */
    stt = info_to_cops_subtree(tvb,st,n,offset,"Gate ID");
    offset += 4;

    /* Gate Identifier */
    proto_tree_add_item(stt, hf_cops_pc_gate_id, tvb, offset, 4, ENC_BIG_ENDIAN);
}

/* Cops - Section : Activity Count */
static void
cops_activity_count(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

    proto_tree *stt;

    /* Create a subtree */
    stt = info_to_cops_subtree(tvb,st,n,offset,"Activity Count");
    offset += 4;

    /* Activity Count */
    proto_tree_add_item(stt, hf_cops_pc_activity_count, tvb, offset, 4, ENC_BIG_ENDIAN);
}


static int
cops_envelope(tvbuff_t* tvb, proto_tree* st, uint32_t offset, const char* name)
{
    proto_tree* object_tree;

    /* Reserved Envelope */
    object_tree = proto_tree_add_subtree(st, tvb, offset, 28, ett_cops_subtree, NULL, name);

    /* Token Bucket Rate */
    proto_tree_add_item(object_tree, hf_cops_pc_token_bucket_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Token Bucket Size */
    proto_tree_add_item(object_tree, hf_cops_pc_token_bucket_size, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Peak Data Rate */
    proto_tree_add_item(object_tree, hf_cops_pc_peak_data_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Minimum Policed Unit */
    proto_tree_add_item(object_tree, hf_cops_pc_min_policed_unit, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Packet Size */
    proto_tree_add_item(object_tree, hf_cops_pc_max_packet_size, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Rate */
    proto_tree_add_item(object_tree, hf_cops_pc_spec_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Slack Term */
    proto_tree_add_item(object_tree, hf_cops_pc_slack_term, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    return offset;
}

/* Cops - Section : Gate Specifications */
static void
cops_gate_specs(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Gate Specifications");
     offset += 4;

     /* Direction */
     proto_tree_add_item(stt, hf_cops_pc_direction, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     /* Protocol ID */
     proto_tree_add_item(stt, hf_cops_pc_protocol_id, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     /* Flags */
     proto_tree_add_item(stt, hf_cops_pc_gate_spec_flags, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     /* Session Class */
     proto_tree_add_item(stt, hf_cops_pc_session_class, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     /* Source IP Address */
     proto_tree_add_item(stt, hf_cops_pc_src_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* Destination IP Address */
     proto_tree_add_item(stt, hf_cops_pc_dest_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* Source IP Port */
     proto_tree_add_item(stt, hf_cops_pc_src_port, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Destination IP Port */
     proto_tree_add_item(stt, hf_cops_pc_dest_port, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* DiffServ Code Point */
     proto_tree_add_item(stt, hf_cops_pc_ds_field, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     /* 3 octets Not specified */
     offset += 3;

     /* Timer T1 Value */
     proto_tree_add_item(stt, hf_cops_pc_t1_value, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Reserved */
     proto_tree_add_item(stt, hf_cops_pc_reserved, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Timer T7 Value */
     proto_tree_add_item(stt, hf_cops_pc_t7_value, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Timer T8 Value */
     proto_tree_add_item(stt, hf_cops_pc_t8_value, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     cops_envelope(tvb, stt, offset, "Gate Envelope");
}

/* Cops - Section : Electronic Surveillance Parameters  */
static void
cops_surveillance_parameters(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Electronic Surveillance Parameters");
     offset += 4;

     /* DF IP Address for CDC */
     proto_tree_add_item(stt, hf_cops_pc_dfcdc_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* DF IP Port for CDC */
     proto_tree_add_item(stt, hf_cops_pc_dfcdc_ip_port, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Flags */
     proto_tree_add_item(stt, hf_cops_pc_gate_spec_flags, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* DF IP Address for CCC */
     proto_tree_add_item(stt, hf_cops_pc_dfccc_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* DF IP Port for CCC */
     proto_tree_add_item(stt, hf_cops_pc_dfccc_ip_port, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Reserved */
     proto_tree_add_item(stt, hf_cops_pc_reserved, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* CCCID */
     proto_tree_add_item(stt, hf_cops_pc_dfccc_id, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* BCID Timestamp */
     proto_tree_add_item(stt, hf_cops_pc_bcid_ts, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* BCID Element ID */
     proto_tree_add_item(stt, hf_cops_pc_bcid_id, tvb, offset, 8, ENC_ASCII);
     offset += 8;

     /* BCID Time Zone */
     proto_tree_add_item(stt, hf_cops_pc_bcid_tz, tvb, offset, 8, ENC_ASCII);
     offset += 8;

     /* BCID Event Counter */
     proto_tree_add_item(stt, hf_cops_pc_bcid_ev, tvb, offset, 4, ENC_BIG_ENDIAN);
}

/* Cops - Section : Event Gereration-Info */
static void
cops_event_generation_info(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Event Generation Info");
     offset += 4;

     /* Primary Record Keeping Server IP Address */
     proto_tree_add_item(stt, hf_cops_pc_prks_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* Primary Record Keeping Server IP Port */
     proto_tree_add_item(stt, hf_cops_pc_prks_ip_port, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Flags */
     proto_tree_add_item(stt, hf_cops_pc_gate_spec_flags, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     /* Reserved */
     proto_tree_add_item(stt, hf_cops_pc_reserved, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     /* Secondary Record Keeping Server IP Address */
     proto_tree_add_item(stt, hf_cops_pc_srks_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* Secondary Record Keeping Server IP Port */
     proto_tree_add_item(stt, hf_cops_pc_srks_ip_port, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Flags */
     proto_tree_add_item(stt, hf_cops_pc_gate_spec_flags, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     /* Reserved */
     proto_tree_add_item(stt, hf_cops_pc_reserved, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     /* BCID Timestamp */
     proto_tree_add_item(stt, hf_cops_pc_bcid_ts, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* BCID Element ID */
     proto_tree_add_item(stt, hf_cops_pc_bcid_id, tvb, offset, 8, ENC_ASCII);
     offset += 8;

     /* BCID Time Zone */
     proto_tree_add_item(stt, hf_cops_pc_bcid_tz, tvb, offset, 8, ENC_ASCII);
     offset += 8;

     /* BCID Event Counter */
     proto_tree_add_item(stt, hf_cops_pc_bcid_ev, tvb, offset, 4, ENC_BIG_ENDIAN);
}

/* Cops - Section : Remote Gate */
static void
cops_remote_gate_info(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Remote Gate Info");
     offset += 4;

     /* CMTS IP Address */
     proto_tree_add_item(stt, hf_cops_pc_cmts_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* CMTS IP Port */
     proto_tree_add_item(stt, hf_cops_pc_cmts_ip_port, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Flags */
     proto_tree_add_item(stt, hf_cops_pc_remote_flags, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Remote Gate ID */
     proto_tree_add_item(stt, hf_cops_pc_remote_gate_id, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* Algorithm */
     proto_tree_add_item(stt, hf_cops_pc_algorithm, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Reserved */
     proto_tree_add_item(stt, hf_cops_pc_reserved, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* Security Key */
     proto_tree_add_item(stt, hf_cops_pc_key, tvb, offset, 16, ENC_NA);
}

/* Cops - Section : PacketCable reason */
static void
cops_packetcable_reason(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;
     uint16_t code16;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"PacketCable Reason");
     offset += 4;

     /* Reason Code */
     code16 = tvb_get_ntohs(tvb,offset);
     proto_tree_add_uint_format(stt, hf_cops_pc_reason_code,tvb, offset, 2,
       code16, "%-28s : %s (%u)","Reason Code",
       val_to_str(code16, table_cops_reason_code, "Unknown (0x%04x)"),code16);
     offset += 2;

     if ( code16 == 0 ) {
        /* Reason Sub Code with Delete */
         proto_tree_add_item(stt, hf_cops_pc_delete_subcode, tvb, offset, 2, ENC_BIG_ENDIAN);
     } else {
        /* Reason Sub Code with Close */
         proto_tree_add_item(stt, hf_cops_pc_close_subcode, tvb, offset, 2, ENC_BIG_ENDIAN);
     }
}

/* Cops - Section : PacketCable error */
static void
cops_packetcable_error(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"PacketCable Error");
     offset += 4;

     /* Error Code */
     proto_tree_add_item(stt, hf_cops_pc_packetcable_err_code, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Error Sub Code */
     proto_tree_add_item(stt, hf_cops_pc_packetcable_sub_code, tvb, offset, 2, ENC_BIG_ENDIAN);
}

/* Cops - Section : Multimedia Transaction ID */
static void
cops_mm_transaction_id(tvbuff_t *tvb, packet_info *pinfo, proto_tree *st, uint8_t op_code, unsigned n, uint32_t offset) {

     proto_tree *stt;
     uint32_t code;
     proto_item* ti;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"MM Transaction ID");
     offset += 4;

     /* Transaction Identifier */
     proto_tree_add_item(stt, hf_cops_pc_mm_transaction_id, tvb, offset, 2, ENC_BIG_ENDIAN);
     // Keep for backwards compatibility
     ti = proto_tree_add_item(stt, hf_cops_pc_transaction_id, tvb, offset, 2, ENC_BIG_ENDIAN);
     PROTO_ITEM_SET_HIDDEN(ti);
     offset +=2;

     /* Gate Command Type */
     proto_tree_add_item_ret_uint(stt, hf_cops_pc_mm_gate_command_type, tvb, offset, 2, ENC_BIG_ENDIAN, &code);
     // Keep for backwards compatibility
     ti = proto_tree_add_item(stt, hf_cops_pc_gate_command_type, tvb, offset, 2, ENC_BIG_ENDIAN);
     PROTO_ITEM_SET_HIDDEN(ti);

     /* Write the right data into the 'info field' on the Gui */
     col_add_str(pinfo->cinfo, COL_INFO, wmem_strdup_printf(pinfo->pool, "COPS %-20s - %s",
         val_to_str_const(op_code, cops_op_code_vals, "Unknown"), val_to_str_const(code, table_cops_mm_transaction_id, "Unknown")));
}

/* Cops - Section : AMID */
static void
cops_amid(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"AMID");
     offset += 4;

     /* Application Type */
     proto_tree_add_item(stt, hf_cops_pcmm_amid_app_type, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Application Manager Tag */
     proto_tree_add_item(stt, hf_cops_pcmm_amid_am_tag, tvb, offset, 2, ENC_BIG_ENDIAN);
}


/* Cops - Section : Multimedia Gate Specifications */
static int
cops_mm_gate_spec(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {
     proto_tree *stt;

     static int* const class_ids[] = {
       &hf_cops_pcmm_gate_spec_session_class_id_priority,
       &hf_cops_pcmm_gate_spec_session_class_id_preemption,
       &hf_cops_pcmm_gate_spec_session_class_id_configurable,
       NULL
     };
     static int* const flags[] = {
       &hf_cops_pcmm_gate_spec_flags_gate,
       & hf_cops_pcmm_gate_spec_flags_dscp_overwrite,
       NULL
     };

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Gate Spec");
     offset += 4;

     /* Flags */
     proto_tree_add_bitmask(stt, tvb, offset, hf_cops_pcmm_gate_spec_flags, ett_cops_pcmm_gate_spec_flags, flags, ENC_BIG_ENDIAN);
     offset += 1;

     /* DiffServ Code Point */
     proto_tree_add_item(stt, hf_cops_pcmm_gate_spec_dscp_tos_field, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     /* DiffServ Code Point Mask */
     proto_tree_add_item(stt, hf_cops_pcmm_gate_spec_dscp_tos_mask, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     /* Session Class */
     proto_tree_add_bitmask(stt, tvb, offset, hf_cops_pcmm_gate_spec_session_class_id, ett_cops_pcmm_gate_spec_session_class_id, class_ids, ENC_BIG_ENDIAN);
     offset += 1;

     /* Timer T1 Value */
     proto_tree_add_item(stt, hf_cops_pcmm_gate_spec_timer_t1, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Timer T2 Value */
     proto_tree_add_item(stt, hf_cops_pcmm_gate_spec_timer_t2, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Timer T3 Value */
     proto_tree_add_item(stt, hf_cops_pcmm_gate_spec_timer_t3, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Timer T4 Value */
     proto_tree_add_item(stt, hf_cops_pcmm_gate_spec_timer_t4, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     return offset;
}

/* Cops - Section : Classifier */
static int
cops_classifier(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset, bool extended) {

    proto_tree *stt;

    /* Create a subtree */
    stt = info_to_cops_subtree(tvb,st,n,offset, extended ? "Extended Classifier" : "Classifier");
    offset += 4;

    /* Protocol ID */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_protocol_id, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* DiffServ Code Point */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_dscp_tos_field, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    /* DiffServ Code Point Mask */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_dscp_tos_mask, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    /* Source IP Address */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_src_addr, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    if (extended) {
        /* Source Mask */
        proto_tree_add_item(stt, hf_cops_pcmm_classifier_src_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
        offset += 4;
    }

    /* Destination IP Address */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_dst_addr, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    if (extended) {
        /* Destination Mask */
        proto_tree_add_item(stt, hf_cops_pcmm_classifier_dst_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
        offset += 4;
    }

    /* Source IP Port */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_src_port, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    if (extended) {
        /* Source Port End */
        proto_tree_add_item(stt, hf_cops_pcmm_classifier_src_port_end, tvb, offset, 2, ENC_BIG_ENDIAN);
        offset += 2;
    }

    /* Destination IP Port */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_dst_port, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    if (extended) {
        /* Destination Port End */
        proto_tree_add_item(stt, hf_cops_pcmm_classifier_dst_port_end, tvb, offset, 2, ENC_BIG_ENDIAN);
        offset += 2;

        /* ClassifierID */
        proto_tree_add_item(stt, hf_cops_pcmm_classifier_classifier_id, tvb, offset, 2, ENC_BIG_ENDIAN);
        offset += 2;
    }

    /* Priority */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_priority, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    if (extended) {
        /* Activation State */
        proto_tree_add_item(stt, hf_cops_pcmm_classifier_activation_state, tvb, offset, 1, ENC_BIG_ENDIAN);
        offset += 1;

        /* Action */
        proto_tree_add_item(stt, hf_cops_pcmm_classifier_action, tvb, offset, 1, ENC_BIG_ENDIAN);
        offset += 1;
    }

    /* 3 octets Not specified */
    offset += 3;

    return offset;
}

/* Cops - Section : IPv6 Classifier */
static int
cops_ipv6_classifier(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

    proto_tree *stt;

    /* Create a subtree */
    stt = info_to_cops_subtree(tvb,st,n,offset, "IPv6 Classifier");
    offset += 4;

    /* Reserved/Flags */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_flags, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    /* tc-low */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_tc_low, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    /* tc-high */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_tc_high, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    /* tc-mask */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_tc_mask, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    /* Flow Label */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_flow_label, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Next Header Type */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_flow_label, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Source Prefix Length */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_source_prefix_length, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    /* Destination Prefix Length */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_destination_prefix_length, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    /* Source IP Address */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_src_addr_v6, tvb, offset, 16, ENC_NA);
    offset += 16;

    /* Destination IP Address */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_dst_addr_v6, tvb, offset, 16, ENC_NA);
    offset += 16;

    /* Source IP Port */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_src_port, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Source Port End */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_src_port_end, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Destination IP Port */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_dst_port, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Destination Port End */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_dst_port_end, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* ClassifierID */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_classifier_id, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Priority */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_priority, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    /* Activation State */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_activation_state, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    /* Action */
    proto_tree_add_item(stt, hf_cops_pcmm_classifier_action, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    /* 3 octets Not specified */
    offset += 3;

    return offset;
}

/* Cops - Section : Gate Specifications */
static int
cops_flow_spec(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {
     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Flow Spec");
     offset += 4;

     /* Envelope */
     proto_tree_add_item(stt, hf_cops_pcmm_flow_spec_envelope, tvb, offset, 1, ENC_NA);
     offset += 1;

     /* Service Number */
     proto_tree_add_item(stt, hf_cops_pcmm_flow_spec_service_number, tvb, offset, 1, ENC_NA);
     offset += 1;

     /* Reserved */
     proto_tree_add_item(stt, hf_cops_pc_reserved, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Authorized Envelope */
     offset = cops_envelope(tvb, stt, offset, "Authorized Envelope");

     if (n < 64) return offset;

     /* Reserved Envelope */
     offset = cops_envelope(tvb, stt, offset, "Reserved Envelope");

     if (n < 92) return offset;

     /* Committed Envelope */
     offset = cops_envelope(tvb, stt, offset, "Committed Envelope");

     return offset;
}

/* Cops - Section : DOCSIS Service Class Name */
static int
cops_docsis_service_class_name(tvbuff_t *tvb, packet_info *pinfo, proto_tree *st, unsigned object_len, uint32_t offset) {

    proto_tree *stt;

    /* Create a subtree */
    stt = info_to_cops_subtree(tvb,st,object_len,offset,"DOCSIS Service Class Name");
    offset += 4;

    /* Envelope */
    proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
    offset += 3;

    if (object_len >= 12) {
        proto_tree_add_item(stt, hf_cops_pcmm_docsis_scn, tvb, offset, object_len - 8, ENC_ASCII);
        offset += object_len - 8;
    } else {
        proto_tree_add_expert_format(stt, pinfo, &ei_cops_bad_cops_object_length,
                                    tvb, offset - 8, 2, "Invalid object length: %u", object_len);
    }

    return offset;
}

/* New functions were made with the i04 suffix to maintain backward compatibility with I03
*
*  BEGIN PCMM I04
*
*/
static int
cops_best_effort_service_envelope(tvbuff_t* tvb, proto_tree* st, uint32_t offset, const char* name, bool i05)
{
    proto_tree* object_tree;

    object_tree = proto_tree_add_subtree(st, tvb, offset, i05 ? 36 : 32, ett_cops_subtree, NULL, name);

    /* Traffic Priority */
    proto_tree_add_item(object_tree, hf_cops_pcmm_traffic_priority, tvb, offset, 1, ENC_NA);
    offset += 1;

    proto_tree_add_item(object_tree, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
    offset += 3;

    /* Request Transmission Policy */
    proto_tree_add_bitmask(object_tree, tvb, offset, hf_cops_pcmm_request_transmission_policy,
        ett_docsis_request_transmission_policy,
        docsis_request_transmission_policies,
        ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Sustained Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_sustained_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Traffic Burst */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_traffic_burst, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Minimum Reserved Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_min_reserved_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Assumed Minimum Reserved Traffic Rate Packet Size */
    proto_tree_add_item(object_tree, hf_cops_pcmm_ass_min_rtr_packet_size, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Maximum Concatenated Burst */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_concat_burst, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Required Attribute Mask */
    proto_tree_add_item(object_tree, hf_cops_pcmm_req_att_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Forbidden Attribute Mask */
    proto_tree_add_item(object_tree, hf_cops_pcmm_forbid_att_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    if (i05) {
        /* Attribute Aggregation Rule Mask */
        proto_tree_add_item(object_tree, hf_cops_pcmm_att_aggr_rule_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
        offset += 4;
    }

    return offset;
}

/* Cops - Section : Best Effort Service */
static int
cops_best_effort_service_i04_i05(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset, bool i05) {
     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Best Effort Service");
     offset += 4;

     /* Envelope */
     proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
     offset += 3;

     /* Authorized Envelope */
     offset = cops_best_effort_service_envelope(tvb, stt, offset, "Authorized Envelope", i05);

     if (n < 56) return offset;

     /* Reserved Envelope */
     offset = cops_best_effort_service_envelope(tvb, stt, offset, "Reserved Envelope", i05);

     if (n < 80) return offset;

     /* Committed Envelope */
     offset = cops_best_effort_service_envelope(tvb, stt, offset, "Committed Envelope", i05);

     return offset;
}

static int
cops_non_real_time_polling_service_envelope(tvbuff_t* tvb, proto_tree* st, uint32_t offset, const char* name, bool i05)
{
    proto_tree* object_tree;

    object_tree = proto_tree_add_subtree(st, tvb, offset, i05 ? 40 : 36, ett_cops_subtree, NULL, name);

    /* Traffic Priority */
    proto_tree_add_item(object_tree, hf_cops_pcmm_traffic_priority, tvb, offset, 1, ENC_NA);
    offset += 1;

    proto_tree_add_item(object_tree, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
    offset += 3;

    /* Request Transmission Policy */
    proto_tree_add_bitmask(object_tree, tvb, offset, hf_cops_pcmm_request_transmission_policy,
        ett_docsis_request_transmission_policy,
        docsis_request_transmission_policies,
        ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Sustained Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_sustained_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Traffic Burst */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_traffic_burst, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Minimum Reserved Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_min_reserved_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Assumed Minimum Reserved Traffic Rate Packet Size */
    proto_tree_add_item(object_tree, hf_cops_pcmm_ass_min_rtr_packet_size, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Maximum Concatenated Burst */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_concat_burst, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Nominal Polling Interval */
    proto_tree_add_item(object_tree, hf_cops_pcmm_nominal_polling_interval, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Required Attribute Mask */
    proto_tree_add_item(object_tree, hf_cops_pcmm_req_att_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Forbidden Attribute Mask */
    proto_tree_add_item(object_tree, hf_cops_pcmm_forbid_att_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    if (i05) {
        /* Attribute Aggregation Rule Mask */
        proto_tree_add_item(object_tree, hf_cops_pcmm_att_aggr_rule_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
        offset += 4;
    }

    return offset;
}

/* Cops - Section : Non-Real-Time Polling Service */
static int
cops_non_real_time_polling_service_i04_i05(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset, bool i05) {
     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Non-Real-Time Polling Service");
     offset += 4;

     /* Envelope */
     proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
     offset += 3;

     /* Authorized Envelope */
     offset = cops_non_real_time_polling_service_envelope(tvb, stt, offset, "Authorized Envelope", i05);

     if (n < 64) return offset;

     /* Reserved Envelope */
     offset = cops_non_real_time_polling_service_envelope(tvb, stt, offset, "Reserved Envelope", i05);

     if (n < 92) return offset;

     /* Committed Envelope */
     offset = cops_non_real_time_polling_service_envelope(tvb, stt, offset, "Committed Envelope", i05);

     return offset;
}

static int
cops_real_time_polling_service_envelope(tvbuff_t* tvb, proto_tree* st, uint32_t offset, const char* name, bool i05)
{
    proto_tree* object_tree;

    object_tree = proto_tree_add_subtree(st, tvb, offset, i05 ? 40 : 36, ett_cops_subtree, NULL, name);

    /* Request Transmission Policy */
    proto_tree_add_bitmask(object_tree, tvb, offset, hf_cops_pcmm_request_transmission_policy,
        ett_docsis_request_transmission_policy,
        docsis_request_transmission_policies,
        ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Sustained Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_sustained_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Traffic Burst */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_traffic_burst, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Minimum Reserved Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_min_reserved_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Assumed Minimum Reserved Traffic Rate Packet Size */
    proto_tree_add_item(object_tree, hf_cops_pcmm_ass_min_rtr_packet_size, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Maximum Concatenated Burst */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_concat_burst, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Nominal Polling Interval */
    proto_tree_add_item(object_tree, hf_cops_pcmm_nominal_polling_interval, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Tolerated Poll Jitter */
    proto_tree_add_item(object_tree, hf_cops_pcmm_tolerated_poll_jitter, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Required Attribute Mask */
    proto_tree_add_item(object_tree, hf_cops_pcmm_req_att_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Forbidden Attribute Mask */
    proto_tree_add_item(object_tree, hf_cops_pcmm_forbid_att_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    if (i05) {
        /* Attribute Aggregation Rule Mask */
        proto_tree_add_item(object_tree, hf_cops_pcmm_att_aggr_rule_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
        offset += 4;
    }

    return offset;
}

/* Cops - Section : Real-Time Polling Service */
static int
cops_real_time_polling_service_i04_i05(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset, bool i05) {
     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Real-Time Polling Service");
     offset += 4;

     /* Envelope */
     proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
     offset += 3;

     /* Authorized Envelope */
     offset = cops_real_time_polling_service_envelope(tvb, stt, offset, "Authorized Envelope", i05);

     if (n < 64) return offset;

     /* Reserved Envelope */
     offset = cops_real_time_polling_service_envelope(tvb, stt, offset, "Reserved Envelope", i05);

     if (n < 92) return offset;

     /* Committed Envelope */
     offset = cops_real_time_polling_service_envelope(tvb, stt, offset, "Committed Envelope", i05);

     return offset;
}

static int
cops_grant_data(tvbuff_t* tvb, proto_tree* tree, uint32_t offset)
{
    /* Unsolicited Grant Size */
    proto_tree_add_item(tree, hf_cops_pcmm_unsolicited_grant_size, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Grants Per Interval */
    proto_tree_add_item(tree, hf_cops_pcmm_grants_per_interval, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    proto_tree_add_item(tree, hf_cops_reserved8, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    /* Nominal Grant Interval */
    proto_tree_add_item(tree, hf_cops_pcmm_nominal_grant_interval, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Tolerated Grant Jitter */
    proto_tree_add_item(tree, hf_cops_pcmm_tolerated_grant_jitter, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    return offset;
}

static int
cops_grant_service_envelope(tvbuff_t* tvb, proto_tree* st, uint32_t offset, const char* name, bool i05)
{
    proto_tree* object_tree;

    /* Authorized Envelope */
    object_tree = proto_tree_add_subtree(st, tvb, offset, i05 ? 28 : 24, ett_cops_subtree, NULL, name);

    /* Request Transmission Policy */
    proto_tree_add_bitmask(object_tree, tvb, offset, hf_cops_pcmm_request_transmission_policy,
        ett_docsis_request_transmission_policy,
        docsis_request_transmission_policies,
        ENC_BIG_ENDIAN);
    offset += 4;

    offset = cops_grant_data(tvb, object_tree, offset);

    /* Required Attribute Mask */
    proto_tree_add_item(object_tree, hf_cops_pcmm_req_att_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Forbidden Attribute Mask */
    proto_tree_add_item(object_tree, hf_cops_pcmm_forbid_att_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    if (i05) {
        /* Attribute Aggregation Rule Mask */
        proto_tree_add_item(object_tree, hf_cops_pcmm_att_aggr_rule_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
        offset += 4;
    }

    return offset;
}

/* Cops - Section : Unsolicited Grant Service */
static int
cops_unsolicited_grant_service_i04_i05(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset, bool i05)
{
     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Unsolicited Grant Service");
     offset += 4;

     /* Envelope */
     proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
     offset += 3;

     /* Authorized Envelope */
     offset = cops_grant_service_envelope(tvb, stt, offset, "Authorized Envelope", i05);

     if (n < 40) return offset;

     /* Reserved Envelope */
     offset = cops_grant_service_envelope(tvb, stt, offset, "Reserved Envelope", i05);

     if (n < 56) return offset;

     /* Committed Envelope */
     offset = cops_grant_service_envelope(tvb, stt, offset, "Committed Envelope", i05);

     return offset;
}

static int
cops_grant_service_activity_envelope(tvbuff_t* tvb, proto_tree* st, uint32_t offset, const char* name, bool i05)
{
    proto_tree* object_tree;

    /* Authorized Envelope */
    object_tree = proto_tree_add_subtree(st, tvb, offset, i05 ? 36 : 32, ett_cops_subtree, NULL, name);

    /* Request Transmission Policy */
    proto_tree_add_bitmask(object_tree, tvb, offset, hf_cops_pcmm_request_transmission_policy,
        ett_docsis_request_transmission_policy,
        docsis_request_transmission_policies,
        ENC_BIG_ENDIAN);
    offset += 4;

    offset = cops_grant_data(tvb, object_tree, offset);

    /* Nominal Polling Interval */
    proto_tree_add_item(object_tree, hf_cops_pcmm_nominal_polling_interval, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Tolerated Poll Jitter */
    proto_tree_add_item(object_tree, hf_cops_pcmm_tolerated_poll_jitter, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Required Attribute Mask */
    proto_tree_add_item(object_tree, hf_cops_pcmm_req_att_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Forbidden Attribute Mask */
    proto_tree_add_item(object_tree, hf_cops_pcmm_forbid_att_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    if (i05) {
        /* Attribute Aggregation Rule Mask */
        proto_tree_add_item(object_tree, hf_cops_pcmm_att_aggr_rule_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
        offset += 4;
    }

    return offset;
}

/* Cops - Section : Unsolicited Grant Service with Activity Detection */
static int
cops_ugs_with_activity_detection_i04_i05(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset, bool i05) {
     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Unsolicited Grant Service with Activity Detection");
     offset += 4;

     /* Envelope */
     proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
     offset += 3;

     /* Authorized Envelope */
     offset = cops_grant_service_activity_envelope(tvb, st, offset, "Authorized Envelope", i05);

     if (n < 56) return offset;

     /* Reserved Envelope */
     offset = cops_grant_service_activity_envelope(tvb, st, offset, "Reserved Envelope", i05);

     if (n < 80) return offset;

     /* Committed Envelope */
     offset = cops_grant_service_activity_envelope(tvb, st, offset, "Committed Envelope", i05);

     return offset;
}

static int
cops_downstream_service_i04_i05_envelope(tvbuff_t* tvb, proto_tree* st, uint32_t offset, const char* name, bool i05)
{
    proto_tree* object_tree;

    object_tree = proto_tree_add_subtree(st, tvb, offset, i05 ? 40 : 36, ett_cops_subtree, NULL, name);

    /* Traffic Priority */
    proto_tree_add_item(object_tree, hf_cops_pcmm_traffic_priority, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    /* Downstream Resequencing */
    proto_tree_add_item(object_tree, hf_cops_pcmm_down_resequencing, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    proto_tree_add_item(object_tree, hf_cops_reserved16, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Maximum Sustained Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_sustained_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Traffic Burst */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_traffic_burst, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Minimum Reserved Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_min_reserved_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Assumed Minimum Reserved Traffic Rate Packet Size */
    proto_tree_add_item(object_tree, hf_cops_pcmm_ass_min_rtr_packet_size, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Reserved */
    proto_tree_add_item(object_tree, hf_cops_pc_reserved, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Maximum Downstream Latency */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_downstream_latency, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Downstream Peak Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_down_peak_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Required Attribute Mask */
    proto_tree_add_item(object_tree, hf_cops_pcmm_req_att_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Forbidden Attribute Mask */
    proto_tree_add_item(object_tree, hf_cops_pcmm_forbid_att_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    if (i05) {
        /* Attribute Aggregation Rule Mask */
        proto_tree_add_item(object_tree, hf_cops_pcmm_att_aggr_rule_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
        offset += 4;
    }

    return offset;
}

/* Cops - Section : Downstream Service */
static int
cops_downstream_service_i04_i05(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset, bool i05) {
    proto_tree *stt;

    /* Create a subtree */
    stt = info_to_cops_subtree(tvb,st,n,offset,"Downstream Service");
    offset += 4;

    /* Envelope */
    proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
    offset += 3;

    /* Authorized Envelope */
    offset = cops_downstream_service_i04_i05_envelope(tvb, stt, offset, "Authorized Envelope", i05);

    if (n < 56) return offset;

    /* Reserved Envelope */
    offset = cops_downstream_service_i04_i05_envelope(tvb, stt, offset, "Reserved Envelope", i05);

    if (n < 80) return offset;

    /* Committed Envelope */
    offset = cops_downstream_service_i04_i05_envelope(tvb, stt, offset, "Committed Envelope", i05);

    return offset;
}

/* Cops - Section : Upstream Drop */
static int
cops_upstream_drop_i04(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {
     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Upstream Drop");
     offset += 4;

     /* Envelope */
     proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
     offset += 3;

     return offset;
}

/* END PCMM I04 */

static int
cops_best_effort_service_envelope_i03(tvbuff_t* tvb, proto_tree* st, uint32_t offset, const char* name)
{
    proto_tree* object_tree;

    object_tree = proto_tree_add_subtree(st, tvb, offset, 24, ett_cops_subtree, NULL, name);

    /* Traffic Priority */
    proto_tree_add_item(object_tree, hf_cops_pcmm_traffic_priority, tvb, offset, 1, ENC_NA);
    offset += 1;

    proto_tree_add_item(object_tree, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
    offset += 3;

    /* Request Transmission Policy */
    proto_tree_add_bitmask(object_tree, tvb, offset, hf_cops_pcmm_request_transmission_policy,
        ett_docsis_request_transmission_policy,
        docsis_request_transmission_policies,
        ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Sustained Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_sustained_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Traffic Burst */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_traffic_burst, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Minimum Reserved Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_min_reserved_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Assumed Minimum Reserved Traffic Rate Packet Size */
    proto_tree_add_item(object_tree, hf_cops_pcmm_ass_min_rtr_packet_size, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Reserved */
    proto_tree_add_item(object_tree, hf_cops_pc_reserved, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    return offset;
}

/* Cops - Section : Best Effort Service */
static int
cops_best_effort_service(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {
     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Best Effort Service");
     offset += 4;

     /* Envelope */
     proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
     offset += 3;

     /* Authorized Envelope */
     offset = cops_best_effort_service_envelope_i03(tvb, stt, offset, "Authorized Envelope");

     if (n < 56) return offset;

     /* Reserved Envelope */
     offset = cops_best_effort_service_envelope_i03(tvb, stt, offset, "Reserved Envelope");

     if (n < 80) return offset;

     /* Committed Envelope */
     offset = cops_best_effort_service_envelope_i03(tvb, stt, offset, "Committed Envelope");

     return offset;
}

static int
cops_non_real_time_polling_service_envelope_i03(tvbuff_t* tvb, proto_tree* st, uint32_t offset, const char* name)
{
    proto_tree* object_tree;

    object_tree = proto_tree_add_subtree(st, tvb, offset, 28, ett_cops_subtree, NULL, name);

    /* Traffic Priority */
    proto_tree_add_item(object_tree, hf_cops_pcmm_traffic_priority, tvb, offset, 1, ENC_BIG_ENDIAN);
    offset += 1;

    proto_tree_add_item(object_tree, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
    offset += 3;

    /* Request Transmission Policy */
    proto_tree_add_bitmask(object_tree, tvb, offset, hf_cops_pcmm_request_transmission_policy,
        ett_docsis_request_transmission_policy,
        docsis_request_transmission_policies,
        ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Sustained Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_sustained_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Traffic Burst */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_traffic_burst, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Minimum Reserved Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_min_reserved_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Assumed Minimum Reserved Traffic Rate Packet Size */
    proto_tree_add_item(object_tree, hf_cops_pcmm_ass_min_rtr_packet_size, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Reserved */
    proto_tree_add_item(object_tree, hf_cops_pc_reserved, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Nominal Polling Interval */
    proto_tree_add_item(object_tree, hf_cops_pcmm_nominal_polling_interval, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 4;

    return offset;
}

/* Cops - Section : Non-Real-Time Polling Service */
static int
cops_non_real_time_polling_service(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {
     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Non-Real-Time Polling Service");
     offset += 4;

     /* Envelope */
     proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
     offset += 3;

     /* Authorized Envelope */
     offset = cops_non_real_time_polling_service_envelope_i03(tvb, stt, offset, "Authorized Envelope");

     if (n < 64) return offset;

     /* Reserved Envelope */
     offset = cops_non_real_time_polling_service_envelope_i03(tvb, stt, offset, "Reserved Envelope");

     if (n < 92) return offset;

     /* Committed Envelope */
     offset = cops_non_real_time_polling_service_envelope_i03(tvb, stt, offset, "Committed Envelope");

     return offset;
}

static int
cops_real_time_polling_service_envelope_i03(tvbuff_t* tvb, proto_tree* st, uint32_t offset, const char* name)
{
    proto_tree* object_tree;

    object_tree = proto_tree_add_subtree(st, tvb, offset, 28, ett_cops_subtree, NULL, name);

    /* Request Transmission Policy */
    proto_tree_add_bitmask(object_tree, tvb, offset, hf_cops_pcmm_request_transmission_policy,
        ett_docsis_request_transmission_policy,
        docsis_request_transmission_policies,
        ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Sustained Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_sustained_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Traffic Burst */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_traffic_burst, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Minimum Reserved Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_min_reserved_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Assumed Minimum Reserved Traffic Rate Packet Size */
    proto_tree_add_item(object_tree, hf_cops_pcmm_ass_min_rtr_packet_size, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Reserved */
    proto_tree_add_item(object_tree, hf_cops_pc_reserved, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Nominal Polling Interval */
    proto_tree_add_item(object_tree, hf_cops_pcmm_nominal_polling_interval, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Tolerated Poll Jitter */
    proto_tree_add_item(object_tree, hf_cops_pcmm_tolerated_poll_jitter, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    return offset;
}

/* Cops - Section : Real-Time Polling Service */
static int
cops_real_time_polling_service(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {
     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Real-Time Polling Service");
     offset += 4;

     /* Envelope */
     proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
     offset += 3;

     /* Authorized Envelope */
     offset = cops_real_time_polling_service_envelope_i03(tvb, stt, offset, "Authorized Envelope");

     if (n < 64) return offset;

     /* Reserved Envelope */
     offset = cops_real_time_polling_service_envelope_i03(tvb, stt, offset, "Reserved Envelope");

     if (n < 92) return offset;

     /* Committed Envelope */
     offset = cops_real_time_polling_service_envelope_i03(tvb, stt, offset, "Committed Envelope");

     return offset;
}

static int
cops_unsolicted_grant_service_envelope(tvbuff_t* tvb, proto_tree* st, uint32_t offset, const char* name)
{
    proto_tree* object_tree;

    object_tree = proto_tree_add_subtree(st, tvb, offset, 16, ett_cops_subtree, NULL, name);

    /* Request Transmission Policy */
    proto_tree_add_bitmask(object_tree, tvb, offset, hf_cops_pcmm_request_transmission_policy,
        ett_docsis_request_transmission_policy,
        docsis_request_transmission_policies,
        ENC_BIG_ENDIAN);
    offset += 4;

    offset = cops_grant_data(tvb, object_tree, offset);

    return offset;
}

/* Cops - Section : Unsolicited Grant Service */
static int
cops_unsolicited_grant_service(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {
     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Unsolicited Grant Service");
     offset += 4;

     /* Envelope */
     proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
     offset += 3;

     /* Authorized Envelope */
     offset = cops_unsolicted_grant_service_envelope(tvb, stt, offset, "Authorized Envelope");

     if (n < 40) return offset;

     /* Reserved Envelope */
     offset = cops_unsolicted_grant_service_envelope(tvb, stt, offset, "Reserved Envelope");

     if (n < 56) return offset;

     /* Committed Envelope */
     offset = cops_unsolicted_grant_service_envelope(tvb, stt, offset, "Committed Envelope");

     return offset;
}

static int
cops_ugs_with_activity_detection_envelope(tvbuff_t* tvb, proto_tree* st, uint32_t offset, const char* name)
{
    proto_tree* object_tree;

    object_tree = proto_tree_add_subtree(st, tvb, offset, 24, ett_cops_subtree, NULL, name);

    /* Request Transmission Policy */
    proto_tree_add_bitmask(object_tree, tvb, offset, hf_cops_pcmm_request_transmission_policy,
        ett_docsis_request_transmission_policy,
        docsis_request_transmission_policies,
        ENC_BIG_ENDIAN);
    offset += 4;

    offset = cops_grant_data(tvb, object_tree, offset);

    /* Nominal Polling Interval */
    proto_tree_add_item(object_tree, hf_cops_pcmm_nominal_polling_interval, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Tolerated Poll Jitter */
    proto_tree_add_item(object_tree, hf_cops_pcmm_tolerated_poll_jitter, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    return offset;
}

/* Cops - Section : Unsolicited Grant Service with Activity Detection */
static int
cops_ugs_with_activity_detection(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {
     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Unsolicited Grant Service with Activity Detection");
     offset += 4;

     /* Envelope */
     proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
     offset += 3;

     /* Authorized Envelope */
     offset = cops_ugs_with_activity_detection_envelope(tvb, stt, offset, "Authorized Envelope");

     if (n < 56) return offset;

     /* Reserved Envelope */
     offset = cops_ugs_with_activity_detection_envelope(tvb, stt, offset, "Reserved Envelope");

     if (n < 80) return offset;

     /* Committed Envelope */
     offset = cops_ugs_with_activity_detection_envelope(tvb, stt, offset, "Committed Envelope");

     return offset;
}

static int
cops_downstream_service_envelope(tvbuff_t* tvb, proto_tree* st, uint32_t offset, const char* name)
{
    proto_tree* object_tree;

    object_tree = proto_tree_add_subtree(st, tvb, offset, 24, ett_cops_subtree, NULL, name);

    /* Traffic Priority */
    proto_tree_add_item(object_tree, hf_cops_pcmm_traffic_priority, tvb, offset, 1, ENC_NA);
    offset += 1;

    proto_tree_add_item(object_tree, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
    offset += 3;

    /* Maximum Sustained Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_sustained_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Maximum Traffic Burst */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_traffic_burst, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Minimum Reserved Traffic Rate */
    proto_tree_add_item(object_tree, hf_cops_pcmm_min_reserved_traffic_rate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    /* Assumed Minimum Reserved Traffic Rate Packet Size */
    proto_tree_add_item(object_tree, hf_cops_pcmm_ass_min_rtr_packet_size, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Reserved */
    proto_tree_add_item(object_tree, hf_cops_pc_reserved, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    /* Maximum Downstream Latency */
    proto_tree_add_item(object_tree, hf_cops_pcmm_max_downstream_latency, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    return offset;
}

/* Cops - Section : Downstream Service */
static int
cops_downstream_service(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {
     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Downstream Service");
     offset += 4;

     /* Envelope */
     proto_tree_add_item(stt, hf_cops_pcmm_envelope, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     proto_tree_add_item(stt, hf_cops_reserved24, tvb, offset, 3, ENC_BIG_ENDIAN);
     offset += 3;

     /* Authorized Envelope */
     offset = cops_downstream_service_envelope(tvb, stt, offset, "Authorized Envelope");

     if (n < 56) return offset;

     /* Reserved Envelope */
     offset = cops_downstream_service_envelope(tvb, stt, offset, "Reserved Envelope");

     if (n < 80) return offset;

     /* Committed Envelope */
     offset = cops_downstream_service_envelope(tvb, stt, offset, "Committed Envelope");

     return offset;
}

/* Cops - Section : PacketCable Multimedia Event Gereration-Info */
static void
cops_mm_event_generation_info(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Event Generation Info");
     offset += 4;

     /* Primary Record Keeping Server IP Address */
     proto_tree_add_item(stt, hf_cops_pc_prks_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* Primary Record Keeping Server IP Port */
     proto_tree_add_item(stt, hf_cops_pc_prks_ip_port, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Reserved */
     proto_tree_add_item(stt, hf_cops_pc_reserved, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Secondary Record Keeping Server IP Address */
     proto_tree_add_item(stt, hf_cops_pc_srks_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* Secondary Record Keeping Server IP Port */
     proto_tree_add_item(stt, hf_cops_pc_srks_ip_port, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Reserved */
     proto_tree_add_item(stt, hf_cops_pc_reserved, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* BCID Timestamp */
     proto_tree_add_item(stt, hf_cops_pc_bcid_ts, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     /* BCID Element ID */
     proto_tree_add_item(stt, hf_cops_pc_bcid_id, tvb, offset, 8, ENC_ASCII);
     offset += 8;

     /* BCID Time Zone */
     proto_tree_add_item(stt, hf_cops_pc_bcid_tz, tvb, offset, 8, ENC_ASCII);
     offset += 8;

     /* BCID Event Counter */
     proto_tree_add_item(stt, hf_cops_pc_bcid_ev, tvb, offset, 4, ENC_BIG_ENDIAN);
}

/* Cops - Section : Volume-Based Usage Limit */
static int
cops_volume_based_usage_limit(tvbuff_t *tvb, proto_tree *st, unsigned object_len, uint32_t offset) {

    proto_tree *stt;

    /* Create a subtree */
    stt = info_to_cops_subtree(tvb,st,object_len,offset,"Volume-Based Usage Limit");
    offset += 4;

    /* Usage Limit */
    proto_tree_add_item(stt, hf_cops_pcmm_volume_based_usage_limit, tvb, offset, 8, ENC_BIG_ENDIAN);
    offset += 8;

    return offset;
}

/* Cops - Section : Time-Based Usage Limit */
static int
cops_time_based_usage_limit(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Time-Based Usage Limit");
     offset += 4;

     /* Time Limit */
     proto_tree_add_item(stt, hf_cops_pcmm_time_based_usage_limit, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     return offset;
}

/* Cops - Section : Opaque Data */
static void
cops_opaque_data(tvbuff_t *tvb, proto_tree *st, unsigned object_len, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,object_len,offset,"Opaque Data");
     offset += 4;

     /* Opaque Data */
     proto_tree_add_item(stt, hf_cops_opaque_data, tvb, offset, 8, ENC_NA);
}

/* Cops - Section : Gate Time Info */
static int
cops_gate_time_info(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Gate Time Info");
     offset += 4;

     /* Gate Time Info */
     proto_tree_add_item(stt, hf_cops_pcmm_gate_time_info, tvb, offset, 4, ENC_BIG_ENDIAN);
     offset += 4;

     return offset;
}

/* Cops - Section : Gate Usage Info */
static void
cops_gate_usage_info(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Gate Usage Info");
     offset += 4;

     /* Gate Usage Info */
     proto_tree_add_item(stt, hf_cops_pcmm_gate_usage_info, tvb, offset, 8, ENC_BIG_ENDIAN);
}

/* Cops - Section : PacketCable error */
static int
cops_packetcable_mm_error(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

    proto_tree *stt;
    uint32_t code;
    uint16_t subcode;

    /* Create a subtree */
    stt = info_to_cops_subtree(tvb,st,n,offset,"PacketCable Error");
    offset += 4;

    proto_tree_add_item_ret_uint(stt, hf_cops_pcmm_packetcable_error_code, tvb, offset, 2, ENC_BIG_ENDIAN, &code);
    offset += 2;

    subcode = tvb_get_ntohs(tvb, offset);
    if (code == 6 || code == 7)
        proto_tree_add_uint_format_value(stt, hf_cops_pcmm_packetcable_error_subcode,
                                   tvb, offset, 2, subcode, "0x%02x, S-Num: 0x%02x, S-Type: 0x%02x",
                                   subcode, subcode >> 8, subcode & 0xf);
    else
        proto_tree_add_item(stt, hf_cops_pcmm_packetcable_error_subcode, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    return offset;
}

/* Cops - Section : Gate State */
static int
cops_gate_state(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Gate State");
     offset += 4;

     /* State */
     proto_tree_add_item(stt, hf_cops_pcmm_packetcable_gate_state, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Reason */
     proto_tree_add_item(stt, hf_cops_pcmm_packetcable_gate_state_reason, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     return offset;
}

/* Cops - Section : Version Info */
static int
cops_version_info(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Version Info");
     offset += 4;

     /* State */
     proto_tree_add_item(stt, hf_cops_pcmm_packetcable_version_info_major, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Reason */
     proto_tree_add_item(stt, hf_cops_pcmm_packetcable_version_info_minor, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     return offset;
}

/* Cops - Section : PSID */
static void
cops_psid(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"PSID");
     offset += 4;

     /* PSID */
     proto_tree_add_item(stt, hf_cops_pcmm_psid, tvb, offset, 4, ENC_BIG_ENDIAN);
}

/* Cops - Section : Synch Options */
static int
cops_synch_options(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Synch Options");
     offset += 4;

     proto_tree_add_item(stt, hf_cops_reserved16, tvb, offset, 2, ENC_BIG_ENDIAN);
     offset += 2;

     /* Report Type */
     proto_tree_add_item(stt, hf_cops_pcmm_synch_options_report_type, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     /* Sych Type */
     proto_tree_add_item(stt, hf_cops_pcmm_synch_options_synch_type, tvb, offset, 1, ENC_BIG_ENDIAN);
     offset += 1;

     return offset;
}

/* Cops - Section : Msg Receipt Key */
static void
cops_msg_receipt_key(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"Msg Receipt Key");
     offset += 4;

     /* Msg Receipt Key */
     proto_tree_add_item(stt, hf_cops_pcmm_msg_receipt_key, tvb, offset, 4, ENC_BIG_ENDIAN);
}

/* Cops - Section : UserID */
static void
cops_userid(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb, st, n, offset, "UserID");
     offset += 4;

     /* UserID */
     proto_tree_add_item(stt, hf_cops_pcmm_userid, tvb, offset, n - 4, ENC_ASCII);
}

/* Cops - Section : SharedResourceID */
static void
cops_sharedresourceid(tvbuff_t *tvb, proto_tree *st, unsigned n, uint32_t offset) {

     proto_tree *stt;

     /* Create a subtree */
     stt = info_to_cops_subtree(tvb,st,n,offset,"SharedResourceID");
     offset += 4;

     /* SharedResourceID */
     proto_tree_add_item(stt, hf_cops_pcmm_sharedresourceid, tvb, offset, 4, ENC_BIG_ENDIAN);
}

/* PacketCable D-QoS S-Num/S-Type globs */
#define PCDQ_TRANSACTION_ID              0x0101
#define PCDQ_SUBSCRIBER_IDv4             0x0201
#define PCDQ_SUBSCRIBER_IDv6             0x0202
#define PCDQ_GATE_ID                     0x0301
#define PCDQ_ACTIVITY_COUNT              0x0401
#define PCDQ_GATE_SPEC                   0x0501
#define PCDQ_REMOTE_GATE_INFO            0x0601
#define PCDQ_EVENT_GENERATION_INFO       0x0701
#define PCDQ_MEDIA_CONNECTION_EVENT_INFO 0x0801
#define PCDQ_PACKETCABLE_ERROR           0x0901
#define PCDQ_PACKETCABLE_REASON          0x0d01
#define PCDQ_ELECTRONIC_SURVEILLANCE     0x0a01
#define PCDQ_SESSION_DESCRIPTION         0x0b01

/* Analyze the PacketCable objects */
static void
cops_analyze_packetcable_dqos_obj(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, uint8_t op_code, uint32_t offset) {

    int remdata;
    uint16_t object_len;
    uint8_t s_num, s_type;
    uint16_t num_type_glob;

    /* Only if this option is enabled by the Gui */
    if ( cops_packetcable == false ) {
        return;
    }

    /* Do the remaining client specific objects */
    remdata = tvb_reported_length_remaining(tvb, offset);
    while (remdata > 4) {

        /* In case we have remaining data, then lets try to get this analyzed */
        object_len   = tvb_get_ntohs(tvb, offset);
        if (object_len < 4) {
            proto_tree_add_expert_format(tree, pinfo, &ei_cops_bad_cops_object_length, tvb, offset, 2,
                                "Incorrect PacketCable object length %u < 4", object_len);
            return;
        }

        s_num        = tvb_get_uint8(tvb, offset + 2);
        s_type       = tvb_get_uint8(tvb, offset + 3);

        /* Glom the s_num and s_type together to make switching easier */
        num_type_glob = s_num << 8 | s_type;

        /* Perform the appropriate functions */
        switch (num_type_glob){
        case PCDQ_TRANSACTION_ID:
            cops_transaction_id(tvb, pinfo, tree, op_code, object_len, offset);
            break;
        case PCDQ_SUBSCRIBER_IDv4:
            cops_subscriber_id_v4(tvb, tree, object_len, offset);
            break;
        case PCDQ_SUBSCRIBER_IDv6:
            cops_subscriber_id_v6(tvb, tree, object_len, offset);
            break;
        case PCDQ_GATE_ID:
            cops_gate_id(tvb, tree, object_len, offset);
            break;
        case PCDQ_ACTIVITY_COUNT:
            cops_activity_count(tvb, tree, object_len, offset);
            break;
        case PCDQ_GATE_SPEC:
            cops_gate_specs(tvb, tree, object_len, offset);
            break;
        case PCDQ_REMOTE_GATE_INFO:
            cops_remote_gate_info(tvb, tree, object_len, offset);
            break;
        case PCDQ_EVENT_GENERATION_INFO:
            cops_event_generation_info(tvb, tree, object_len, offset);
            break;
        case PCDQ_PACKETCABLE_ERROR:
            cops_packetcable_error(tvb, tree, object_len, offset);
            break;
        case PCDQ_ELECTRONIC_SURVEILLANCE:
            cops_surveillance_parameters(tvb, tree, object_len, offset);
            break;
        case PCDQ_PACKETCABLE_REASON:
            cops_packetcable_reason(tvb, tree, object_len, offset);
            break;
        }

        /* Tune offset */
        offset += object_len;

        /* See what we can still get from the buffer */
        remdata = tvb_reported_length_remaining(tvb, offset);
    }
}

#define PCMM_TRANSACTION_ID                0x0101
#define PCMM_AMID                          0x0201
#define PCMM_SUBSCRIBER_ID                 0x0301
#define PCMM_SUBSCRIBER_ID_V6              0x0302
#define PCMM_GATE_ID                       0x0401
#define PCMM_GATE_SPEC                     0x0501
#define PCMM_CLASSIFIER                    0x0601
#define PCMM_EXTENDED_CLASSIFIER           0x0602
#define PCMM_IPV6_CLASSIFIER               0x0603
#define PCMM_FLOW_SPEC                     0x0701
#define PCMM_DOCSIS_SERVICE_CLASS_NAME     0x0702
#define PCMM_BEST_EFFORT_SERVICE           0x0703
#define PCMM_NON_REAL_TIME_POLLING_SERVICE 0x0704
#define PCMM_REAL_TIME_POLLING_SERVICE     0x0705
#define PCMM_UNSOLICITED_GRANT_SERVICE     0x0706
#define PCMM_UGS_WITH_ACTIVITY_DETECTION   0x0707
#define PCMM_DOWNSTREAM_SERVICE            0x0708
#define PCMM_UPSTREAM_DROP                 0x0709
#define PCMM_EVENT_GENERATION_INFO         0x0801
#define PCMM_VOLUME_BASED_USAGE_LIMIT      0x0901
#define PCMM_TIME_BASED_USAGE_LIMIT        0x0a01
#define PCMM_OPAQUE_DATA                   0x0b01
#define PCMM_GATE_TIME_INFO                0x0c01
#define PCMM_GATE_USAGE_INFO               0x0d01
#define PCMM_PACKETCABLE_ERROR             0x0e01
#define PCMM_GATE_STATE                    0x0f01
#define PCMM_VERSION_INFO                  0x1001
#define PCMM_PSID                          0x1101
#define PCMM_SYNCH_OPTIONS                 0x1201
#define PCMM_MSG_RECEIPT_KEY               0x1301
#define PCMM_USERID                        0x1501
#define PCMM_SHAREDRESOURCEID              0x1601


static void
cops_analyze_packetcable_mm_obj(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, uint8_t op_code, uint32_t offset) {

    uint16_t object_len;
    uint8_t s_num, s_type;
    uint16_t num_type_glob;

    /* Only if this option is enabled by the Gui */
    if ( cops_packetcable == false ) {
        return;
    }

    /* Do the remaining client specific objects */
    while (tvb_reported_length_remaining(tvb, offset) > 4) {

        /* In case we have remaining data, then lets try to get this analyzed */
        object_len   = tvb_get_ntohs(tvb, offset);
        if (object_len < 4) {
            proto_tree_add_expert_format(tree, pinfo, &ei_cops_bad_cops_object_length, tvb, offset, 2,
                                "Incorrect PacketCable object length %u < 4", object_len);
            return;
        }

        s_num        = tvb_get_uint8(tvb, offset + 2);
        s_type       = tvb_get_uint8(tvb, offset + 3);

        /* Glom the s_num and s_type together to make switching easier */
        num_type_glob = s_num << 8 | s_type;

        /* Perform the appropriate functions */
        switch (num_type_glob){
        case PCMM_TRANSACTION_ID:
            cops_mm_transaction_id(tvb, pinfo, tree, op_code, object_len, offset);
            break;
        case PCMM_AMID:
            cops_amid(tvb, tree, object_len, offset);
            break;
        case PCMM_SUBSCRIBER_ID:
            cops_subscriber_id_v4(tvb, tree, object_len, offset);
            break;
        case PCMM_SUBSCRIBER_ID_V6:
            cops_subscriber_id_v6(tvb, tree, object_len, offset);
            break;
        case PCMM_GATE_ID:
            cops_gate_id(tvb, tree, object_len, offset);
            break;
        case PCMM_GATE_SPEC:
            cops_mm_gate_spec(tvb, tree, object_len, offset);
            break;
        case PCMM_CLASSIFIER:
            cops_classifier(tvb, tree, object_len, offset, false);
            break;
        case PCMM_EXTENDED_CLASSIFIER:
            cops_classifier(tvb, tree, object_len, offset, true);
            break;
        case PCMM_IPV6_CLASSIFIER:
            cops_ipv6_classifier(tvb, tree, object_len, offset);
            break;
        case PCMM_FLOW_SPEC:
            cops_flow_spec(tvb, tree, object_len, offset);
            break;
        case PCMM_DOCSIS_SERVICE_CLASS_NAME:
            cops_docsis_service_class_name(tvb, pinfo, tree, object_len, offset);
            break;
        case PCMM_BEST_EFFORT_SERVICE:
            if (object_len == 44 || object_len == 80 || object_len == 116)
                cops_best_effort_service_i04_i05(tvb, tree, object_len, offset, true);
            else if (object_len == 40 || object_len == 72 || object_len == 104)
                cops_best_effort_service_i04_i05(tvb, tree, object_len, offset, false);
            else
                cops_best_effort_service(tvb, tree, object_len, offset);
            break;
        case PCMM_NON_REAL_TIME_POLLING_SERVICE:
            if (object_len == 48 || object_len == 88 || object_len == 128)
                cops_non_real_time_polling_service_i04_i05(tvb, tree, object_len, offset, true);
            else if (object_len == 44 || object_len == 80 || object_len == 116)
                cops_non_real_time_polling_service_i04_i05(tvb, tree, object_len, offset, false);
            else
                cops_non_real_time_polling_service(tvb, tree, object_len, offset);
            break;
        case PCMM_REAL_TIME_POLLING_SERVICE:
            if (object_len == 48 || object_len == 88 || object_len == 128)
                cops_real_time_polling_service_i04_i05(tvb, tree, object_len, offset, true);
            else if (object_len == 44 || object_len == 80 || object_len == 116)
                cops_real_time_polling_service_i04_i05(tvb, tree, object_len, offset, false);
            else
                cops_real_time_polling_service(tvb, tree, object_len, offset);
            break;
        case PCMM_UNSOLICITED_GRANT_SERVICE:
            if (object_len == 36 || object_len == 64 || object_len == 92)
                cops_unsolicited_grant_service_i04_i05(tvb, tree, object_len, offset, true);
            else if (object_len == 32 || object_len == 56 || object_len == 80)
                cops_unsolicited_grant_service_i04_i05(tvb, tree, object_len, offset, false);
            else
                cops_unsolicited_grant_service(tvb, tree, object_len, offset);
            break;
        case PCMM_UGS_WITH_ACTIVITY_DETECTION:
            if (object_len == 44 || object_len == 80 || object_len == 116)
                cops_ugs_with_activity_detection_i04_i05(tvb, tree, object_len, offset, true);
            else if (object_len == 40 || object_len == 72 || object_len == 104)
                cops_ugs_with_activity_detection_i04_i05(tvb, tree, object_len, offset, false);
            else
                cops_ugs_with_activity_detection(tvb, tree, object_len, offset);
            break;
        case PCMM_DOWNSTREAM_SERVICE:
            if (object_len == 48 || object_len == 88 || object_len == 128)
                cops_downstream_service_i04_i05(tvb, tree, object_len, offset, true);
            else if (object_len == 40 || object_len == 72 || object_len == 104)
                cops_downstream_service_i04_i05(tvb, tree, object_len, offset, false);
            else
                cops_downstream_service(tvb, tree, object_len, offset);
            break;
        case PCMM_UPSTREAM_DROP:
            cops_upstream_drop_i04(tvb, tree, object_len, offset);
            break;
        case PCMM_EVENT_GENERATION_INFO:
            cops_mm_event_generation_info(tvb, tree, object_len, offset);
            break;
        case PCMM_VOLUME_BASED_USAGE_LIMIT:
            cops_volume_based_usage_limit(tvb, tree, object_len, offset);
            break;
        case PCMM_TIME_BASED_USAGE_LIMIT:
            cops_time_based_usage_limit(tvb, tree, object_len, offset);
            break;
        case PCMM_OPAQUE_DATA:
            cops_opaque_data(tvb, tree, object_len, offset);
            break;
        case PCMM_GATE_TIME_INFO:
            cops_gate_time_info(tvb, tree, object_len, offset);
            break;
        case PCMM_GATE_USAGE_INFO:
            cops_gate_usage_info(tvb, tree, object_len, offset);
            break;
        case PCMM_PACKETCABLE_ERROR:
            cops_packetcable_mm_error(tvb, tree, object_len, offset);
            break;
        case PCMM_GATE_STATE:
            cops_gate_state(tvb, tree, object_len, offset);
            break;
        case PCMM_VERSION_INFO:
            cops_version_info(tvb, tree, object_len, offset);
            break;
        case PCMM_PSID:
            cops_psid(tvb, tree, object_len, offset);
            break;
        case PCMM_SYNCH_OPTIONS:
            cops_synch_options(tvb, tree, object_len, offset);
            break;
        case PCMM_MSG_RECEIPT_KEY:
            cops_msg_receipt_key(tvb, tree, object_len, offset);
            break;
        case PCMM_USERID:
            cops_userid(tvb, tree, object_len, offset);
            break;
        case PCMM_SHAREDRESOURCEID:
            cops_sharedresourceid(tvb, tree, object_len, offset);
            break;

        }

        /* Tune offset */
        offset += object_len;
    }
}


/* End of PacketCable Addition */




/* Register the protocol with Wireshark */
void proto_register_cops(void)
{
    /* Setup list of header fields */
    static hf_register_info hf[] = {
        { &hf_cops_ver_flags,
          { "Version and Flags",           "cops.ver_flags",
            FT_UINT8, BASE_HEX, NULL, 0x0,
            "Version and Flags in COPS Common Header", HFILL }
        },
        { &hf_cops_version,
          { "Version",           "cops.version",
            FT_UINT8, BASE_DEC, NULL, 0xF0,
            "Version in COPS Common Header", HFILL }
        },
        { &hf_cops_flags,
          { "Flags",           "cops.flags",
            FT_UINT8, BASE_HEX, VALS(cops_flags_vals), 0x0F,
            "Flags in COPS Common Header", HFILL }
        },
        { &hf_cops_response_in,
          { "Response In",     "cops.response_in",
            FT_FRAMENUM, BASE_NONE, FRAMENUM_TYPE(FT_FRAMENUM_RESPONSE), 0x0,
            "The response to this COPS request is in this frame", HFILL }
        },
        { &hf_cops_response_to,
          { "Request In",      "cops.response_to",
            FT_FRAMENUM, BASE_NONE, FRAMENUM_TYPE(FT_FRAMENUM_REQUEST), 0x0,
            "This is a response to the COPS request in this frame", HFILL }
        },
        { &hf_cops_response_time,
          { "Response Time",   "cops.response_time",
            FT_RELATIVE_TIME, BASE_NONE, NULL, 0x0,
            "The time between the Call and the Reply", HFILL }
        },
        { &hf_cops_op_code,
          { "Op Code",           "cops.op_code",
            FT_UINT8, BASE_DEC, VALS(cops_op_code_vals), 0x0,
            "Op Code in COPS Common Header", HFILL }
        },
        { &hf_cops_client_type,
          { "Client Type",           "cops.client_type",
            FT_UINT16, BASE_DEC, VALS(cops_client_type_vals), 0x0,
            "Client Type in COPS Common Header", HFILL }
        },
        { &hf_cops_msg_len,
          { "Message Length",           "cops.msg_len",
            FT_UINT32, BASE_DEC, NULL, 0x0,
            "Message Length in COPS Common Header", HFILL }
        },
        { &hf_cops_obj_len,
          { "Object Length",           "cops.obj.len",
            FT_UINT32, BASE_DEC, NULL, 0x0,
            "Object Length in COPS Object Header", HFILL }
        },
        { &hf_cops_obj_c_num,
          { "C-Num",           "cops.c_num",
            FT_UINT8, BASE_DEC, VALS(cops_c_num_vals), 0x0,
            "C-Num in COPS Object Header", HFILL }
        },
        { &hf_cops_obj_c_type,
          { "C-Type",           "cops.c_type",
            FT_UINT8, BASE_DEC, NULL, 0x0,
            "C-Type in COPS Object Header", HFILL }
        },

        { &hf_cops_obj_s_num,
          { "S-Num",           "cops.s_num",
            FT_UINT8, BASE_DEC, VALS(cops_s_num_vals), 0x0,
            "S-Num in COPS-PR Object Header", HFILL }
        },
        { &hf_cops_obj_s_type,
          { "S-Type",           "cops.s_type",
            FT_UINT8, BASE_DEC, NULL, 0x0,
            "S-Type in COPS-PR Object Header", HFILL }
        },
        { &hf_cops_handle,
          { "Handle",           "cops.handle",
            FT_UINT32, BASE_HEX, NULL, 0x0,
            "Handle in COPS Handle Object", HFILL }
        },
        { &hf_cops_r_type_flags,
          { "R-Type",           "cops.context.r_type",
            FT_UINT16, BASE_HEX, VALS(cops_r_type_vals), 0x0,
            "R-Type in COPS Context Object", HFILL }
        },
        { &hf_cops_m_type_flags,
          { "M-Type",           "cops.context.m_type",
            FT_UINT16, BASE_HEX, NULL, 0x0,
            "M-Type in COPS Context Object", HFILL }
        },
        { &hf_cops_in_int_ipv4,
          { "IPv4 address",           "cops.in-int.ipv4",
            FT_IPv4, BASE_NONE, NULL, 0,
            "IPv4 address in COPS IN-Int object", HFILL }
        },
        { &hf_cops_in_int_ipv6,
          { "IPv6 address",           "cops.in-int.ipv6",
            FT_IPv6, BASE_NONE, NULL, 0,
            "IPv6 address in COPS IN-Int object", HFILL }
        },
        { &hf_cops_out_int_ipv4,
          { "IPv4 address",           "cops.out-int.ipv4",
            FT_IPv4, BASE_NONE, NULL, 0,
            "IPv4 address in COPS OUT-Int object", HFILL }
        },
        { &hf_cops_out_int_ipv6,
          { "IPv6 address",           "cops.out-int.ipv6",
            FT_IPv6, BASE_NONE, NULL, 0,
            "IPv6 address in COPS OUT-Int", HFILL }
        },
        { &hf_cops_int_ifindex,
          { "ifIndex",           "cops.in-out-int.ifindex",
            FT_UINT32, BASE_DEC, NULL, 0x0,
            "If SNMP is supported, corresponds to MIB-II ifIndex", HFILL }
        },
        { &hf_cops_reason,
          { "Reason",           "cops.reason",
            FT_UINT16, BASE_DEC, VALS(cops_reason_vals), 0,
            "Reason in Reason object", HFILL }
        },
        { &hf_cops_reason_sub,
          { "Reason Sub-code",           "cops.reason_sub",
            FT_UINT16, BASE_HEX, NULL, 0,
            "Reason Sub-code in Reason object", HFILL }
        },
        { &hf_cops_dec_cmd_code,
          { "Command-Code",           "cops.decision.cmd",
            FT_UINT16, BASE_DEC, VALS(cops_dec_cmd_code_vals), 0,
            "Command-Code in Decision/LPDP Decision object", HFILL }
        },
        { &hf_cops_dec_flags,
          { "Flags",           "cops.decision.flags",
            FT_UINT16, BASE_HEX, VALS(cops_dec_cmd_flag_vals), 0xffff,
            "Flags in Decision/LPDP Decision object", HFILL }
        },
        { &hf_cops_error,
          { "Error",           "cops.error",
            FT_UINT16, BASE_DEC, VALS(cops_error_vals), 0,
            "Error in Error object", HFILL }
        },
        { &hf_cops_error_sub,
          { "Error Sub-code",           "cops.error_sub",
            FT_UINT16, BASE_HEX, NULL, 0,
            "Error Sub-code in Error object", HFILL }
        },
        { &hf_cops_katimer,
          { "Contents: KA Timer Value",           "cops.katimer.value",
            FT_UINT16, BASE_DEC, NULL, 0,
            "Keep-Alive Timer Value in KATimer object", HFILL }
        },
        { &hf_cops_pepid,
          { "Contents: PEP Id",           "cops.pepid.id",
            FT_STRING, BASE_NONE, NULL, 0,
            "PEP Id in PEPID object", HFILL }
        },
        { &hf_cops_report_type,
          { "Contents: Report-Type",           "cops.report_type",
            FT_UINT16, BASE_DEC, VALS(cops_report_type_vals), 0,
            "Report-Type in Report-Type object", HFILL }
        },
        { &hf_cops_pdprediraddr_ipv4,
          { "IPv4 address",           "cops.pdprediraddr.ipv4",
            FT_IPv4, BASE_NONE, NULL, 0,
            "IPv4 address in COPS PDPRedirAddr object", HFILL }
        },
        { &hf_cops_pdprediraddr_ipv6,
          { "IPv6 address",           "cops.pdprediraddr.ipv6",
            FT_IPv6, BASE_NONE, NULL, 0,
            "IPv6 address in COPS PDPRedirAddr object", HFILL }
        },
        { &hf_cops_lastpdpaddr_ipv4,
          { "IPv4 address",           "cops.lastpdpaddr.ipv4",
            FT_IPv4, BASE_NONE, NULL, 0,
            "IPv4 address in COPS LastPDPAddr object", HFILL }
        },
        { &hf_cops_lastpdpaddr_ipv6,
          { "IPv6 address",           "cops.lastpdpaddr.ipv6",
            FT_IPv6, BASE_NONE, NULL, 0,
            "IPv6 address in COPS LastPDPAddr object", HFILL }
        },
        { &hf_cops_pdp_tcp_port,
          { "TCP Port Number",           "cops.pdp.tcp_port",
            FT_UINT32, BASE_DEC, NULL, 0x0,
            "TCP Port Number of PDP in PDPRedirAddr/LastPDPAddr object", HFILL }
        },
        { &hf_cops_accttimer,
          { "Contents: ACCT Timer Value",           "cops.accttimer.value",
            FT_UINT16, BASE_DEC, NULL, 0,
            "Accounting Timer Value in AcctTimer object", HFILL }
        },
        { &hf_cops_key_id,
          { "Contents: Key ID",           "cops.integrity.key_id",
            FT_UINT32, BASE_DEC, NULL, 0,
            "Key ID in Integrity object", HFILL }
        },
        { &hf_cops_seq_num,
          { "Contents: Sequence Number",           "cops.integrity.seq_num",
            FT_UINT32, BASE_DEC, NULL, 0,
            "Sequence Number in Integrity object", HFILL }
        },
        { &hf_cops_keyed_message_digest,
          { "Contents: Keyed Message Digest",           "cops.integrity.keyed_message_digest",
            FT_BYTES, BASE_NONE, NULL, 0,
            NULL, HFILL }
        },
        { &hf_cops_integrity_contents,
          { "Contents",           "cops.integrity.contents",
            FT_BYTES, BASE_NONE, NULL, 0,
            NULL, HFILL }
        },
        { &hf_cops_opaque_data,
          { "Opaque Data",           "cops.opaque_data",
            FT_BYTES, BASE_NONE, NULL, 0,
            NULL, HFILL }
        },
        { &hf_cops_gperror,
          { "Error",           "cops.gperror",
            FT_UINT16, BASE_DEC, VALS(cops_gperror_vals), 0,
            "Error in Error object", HFILL }
        },
        { &hf_cops_gperror_sub,
          { "Error Sub-code",           "cops.gperror_sub",
            FT_UINT16, BASE_HEX, NULL, 0,
            "Error Sub-code in Error object", HFILL }
        },
        { &hf_cops_cperror,
          { "Error",           "cops.cperror",
            FT_UINT16, BASE_DEC, VALS(cops_cperror_vals), 0,
            "Error in Error object", HFILL }
        },
        { &hf_cops_cperror_sub,
          { "Error Sub-code",           "cops.cperror_sub",
            FT_UINT16, BASE_HEX, NULL, 0,
            "Error Sub-code in Error object", HFILL }
        },

        { &hf_cops_reserved8, { "Reserved", "cops.reserved", FT_UINT8, BASE_HEX, NULL, 0, NULL, HFILL } },
        { &hf_cops_reserved16, { "Reserved", "cops.reserved", FT_UINT16, BASE_HEX, NULL, 0, NULL, HFILL } },
        { &hf_cops_reserved24, { "Reserved", "cops.reserved", FT_UINT24, BASE_HEX, NULL, 0, NULL, HFILL } },

        { &hf_cops_prid_oid, { "PRID Instance Identifier", "cops.prid.instance_id", FT_OID, BASE_NONE, NULL, 0, NULL, HFILL } },
        { &hf_cops_pprid_oid, { "Prefix Identifier", "cops.pprid.prefix_id", FT_OID, BASE_NONE, NULL, 0, NULL, HFILL } },
        { &hf_cops_errprid_oid, { "ErrorPRID Instance Identifier", "cops.errprid.instance_id", FT_OID, BASE_NONE, NULL, 0, NULL, HFILL } },
        { &hf_cops_epd_unknown, { "EPD Unknown Data", "cops.epd.unknown", FT_BYTES, BASE_NONE, NULL, 0, NULL, HFILL } },
        { &hf_cops_epd_null, { "EPD Null Data", "cops.epd.null", FT_BYTES, BASE_NONE, NULL, 0, NULL, HFILL } },
        { &hf_cops_epd_int, { "EPD Integer Data", "cops.epd.int", FT_INT64, BASE_DEC, NULL, 0, NULL, HFILL } },
        { &hf_cops_epd_octets, { "EPD Octet String Data", "cops.epd.octets", FT_BYTES, BASE_NONE, NULL, 0, NULL, HFILL } },
        { &hf_cops_epd_oid, { "EPD OID Data", "cops.epd.oid", FT_OID, BASE_NONE, NULL, 0, NULL, HFILL } },
        { &hf_cops_epd_ipv4, { "EPD IPAddress Data", "cops.epd.ipv4", FT_IPv4, BASE_NONE, NULL, 0, NULL, HFILL } },
        { &hf_cops_epd_u32, { "EPD Unsigned32 Data", "cops.epd.unsigned32", FT_UINT64, BASE_DEC, NULL, 0, NULL, HFILL } },
        { &hf_cops_epd_ticks, { "EPD TimeTicks Data", "cops.epd.timeticks", FT_UINT64, BASE_DEC, NULL, 0, NULL, HFILL } },
        { &hf_cops_epd_opaque, { "EPD Opaque Data", "cops.epd.opaque", FT_BYTES, BASE_NONE, NULL, 0, NULL, HFILL } },
        { &hf_cops_epd_i64, { "EPD Integer64 Data", "cops.epd.integer64", FT_INT64, BASE_DEC, NULL, 0, NULL, HFILL } },
        { &hf_cops_epd_u64, { "EPD Unsigned64 Data", "cops.epd.unsigned64", FT_UINT64, BASE_DEC, NULL, 0, NULL, HFILL } },

        /* Added for PacketCable */

        { &hf_cops_subtree,
          { "Object Subtree", "cops.pc_subtree",
            FT_NONE, BASE_NONE, NULL, 0,
            NULL, HFILL }
        },
        { &hf_cops_pc_ds_field,
          { "DS Field (DSCP or TOS)", "cops.pc_ds_field",
            FT_UINT8, BASE_HEX, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_direction,
          { "Direction", "cops.pc_direction",
            FT_UINT8, BASE_DEC, VALS(table_cops_direction), 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_gate_spec_flags,
          { "Flags", "cops.pc_gate_spec_flags",
            FT_UINT16, BASE_HEX, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_protocol_id,
          { "Protocol ID", "cops.pc_protocol_id",
            FT_UINT8, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_session_class,
          { "Session Class", "cops.pc_session_class",
            FT_UINT8, BASE_DEC, VALS(table_cops_session_class), 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_algorithm,
          { "Algorithm", "cops.pc_algorithm",
            FT_UINT16, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_cmts_ip_port,
          { "CMTS IP Port", "cops.pc_cmts_ip_port",
            FT_UINT16, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_prks_ip_port,
          { "PRKS IP Port", "cops.pc_prks_ip_port",
            FT_UINT16, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_srks_ip_port,
          { "SRKS IP Port", "cops.pc_srks_ip_port",
            FT_UINT16, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_dest_port,
          { "Destination IP Port", "cops.pc_dest_port",
            FT_UINT16, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_packetcable_err_code,
          { "Error Code", "cops.pc_packetcable_err_code",
            FT_UINT16, BASE_DEC, VALS(table_cops_packetcable_error), 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_packetcable_sub_code,
          { "Error Sub Code", "cops.pc_packetcable_sub_code",
            FT_UINT16, BASE_HEX, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_remote_flags,
          { "Flags", "cops.pc_remote_flags",
            FT_UINT16, BASE_HEX, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_close_subcode,
          { "Reason Sub Code", "cops.pc_close_subcode",
            FT_UINT16, BASE_DEC, VALS(table_cops_reason_subcode_close), 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_gate_command_type,
          { "Gate Command Type", "cops.pc_gate_command_type",
            FT_UINT16, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_dqos_gate_command_type,
          { "Gate Command Type", "cops.pc_dqos_gate_command_type",
            FT_UINT16, BASE_DEC, VALS(table_cops_dqos_transaction_id), 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_mm_gate_command_type,
          { "Gate Command Type", "cops.pc_mm_gate_command_type",
            FT_UINT16, BASE_DEC, VALS(table_cops_mm_transaction_id), 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_reason_code,
          { "Reason Code", "cops.pc_reason_code",
            FT_UINT16, BASE_HEX, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_delete_subcode,
          { "Reason Sub Code", "cops.pc_delete_subcode",
            FT_UINT16, BASE_DEC, VALS(table_cops_reason_subcode_delete), 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_src_port,
          { "Source IP Port", "cops.pc_src_port",
            FT_UINT16, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_t1_value,
          { "Timer T1 Value", "cops.pc_t1_value",
            FT_UINT16, BASE_DEC | BASE_UNIT_STRING, UNS(&units_second_seconds), 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_t7_value,
          { "Timer T7 Value", "cops.pc_t7_value",
            FT_UINT16, BASE_DEC | BASE_UNIT_STRING, UNS(&units_second_seconds), 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_t8_value,
          { "Timer T8 Value", "cops.pc_t8_value",
            FT_UINT16, BASE_DEC | BASE_UNIT_STRING, UNS(&units_second_seconds), 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_transaction_id,
          { "Transaction Identifier", "cops.pc_transaction_id",
            FT_UINT16, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_dqos_transaction_id,
          { "D-QoS Transaction Identifier", "cops.pc_dqos_transaction_id",
            FT_UINT16, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_mm_transaction_id,
          { "Multimedia Transaction Identifier", "cops.pc_mm_transaction_id",
            FT_UINT16, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_cmts_ip,
          { "CMTS IP Address", "cops.pc_cmts_ip",
            FT_IPv4, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_prks_ip,
          { "PRKS IP Address", "cops.pc_prks_ip",
            FT_IPv4, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_srks_ip,
          { "SRKS IP Address", "cops.pc_srks_ip",
            FT_IPv4, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_dfcdc_ip,
          { "DF IP Address CDC", "cops.pc_dfcdc_ip",
            FT_IPv4, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_dfccc_ip,
          { "DF IP Address CCC", "cops.pc_dfccc_ip",
            FT_IPv4, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_dfcdc_ip_port,
          { "DF IP Port CDC", "cops.pc_dfcdc_ip_port",
            FT_UINT16, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_dfccc_ip_port,
          { "DF IP Port CCC", "cops.pc_dfccc_ip_port",
            FT_UINT16, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_dfccc_id,
          { "CCC ID", "cops.pc_dfccc_id",
            FT_UINT32, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_activity_count,
          { "Count", "cops.pc_activity_count",
            FT_UINT32, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_dest_ip,
          { "Destination IP Address", "cops.pc_dest_ip",
            FT_IPv4, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_gate_id,
          { "Gate Identifier", "cops.pc_gate_id",
            FT_UINT32, BASE_HEX, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_max_packet_size,
          { "Maximum Packet Size", "cops.pc_max_packet_size",
            FT_UINT32, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_min_policed_unit,
          { "Minimum Policed Unit", "cops.pc_min_policed_unit",
            FT_UINT32, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_peak_data_rate,
          { "Peak Data Rate", "cops.pc_peak_data_rate",
            FT_FLOAT, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_spec_rate,
          { "Rate", "cops.pc_spec_rate",
            FT_FLOAT, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_remote_gate_id,
          { "Remote Gate ID", "cops.pc_remote_gate_id",
            FT_UINT32, BASE_HEX, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_reserved,
          { "Reserved", "cops.pc_reserved",
            FT_UINT32, BASE_HEX, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_key,
          { "Security Key", "cops.pc_key",
            FT_BYTES, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_slack_term,
          { "Slack Term", "cops.pc_slack_term",
            FT_UINT32, BASE_DEC, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_src_ip,
          { "Source IP Address", "cops.pc_src_ip",
            FT_IPv4, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_subscriber_id_ipv4,
          { "Subscriber Identifier (IPv4)", "cops.pc_subscriber_id4",
            FT_IPv4, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_subscriber_id_ipv6,
          { "Subscriber Identifier (IPv6)", "cops.pc_subscriber_id6",
            FT_IPv6, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_token_bucket_rate,
          { "Token Bucket Rate", "cops.pc_token_bucket_rate",
            FT_FLOAT, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_token_bucket_size,
          { "Token Bucket Size", "cops.pc_token_bucket_size",
            FT_FLOAT, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_bcid_id,
          { "BCID - Element ID", "cops.pc_bcid.id",
            FT_STRING, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_bcid_tz,
          { "BCID - Time Zone", "cops.pc_bcid.tz",
            FT_STRING, BASE_NONE, NULL, 0x00,
            NULL, HFILL }
        },
        { &hf_cops_pc_bcid_ts,
          { "BDID Timestamp", "cops.pc_bcid_ts",
            FT_UINT32, BASE_HEX, NULL, 0x00,
            "BCID Timestamp", HFILL }
        },
        { &hf_cops_pc_bcid_ev,
          { "BDID Event Counter", "cops.pc_bcid_ev",
            FT_UINT32, BASE_DEC, NULL, 0x00,
            "BCID Event Counter", HFILL }
        },

        { &hf_cops_pcmm_amid_app_type,
          { "AMID Application Type", "cops.pc_mm_amid_application_type",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia AMID Application Type", HFILL }
        },
        { &hf_cops_pcmm_amid_am_tag,
          { "AMID Application Manager Tag", "cops.pc_mm_amid_am_tag",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia AMID Application Manager Tag", HFILL }
        },

        { &hf_cops_pcmm_gate_spec_flags,
          { "Flags", "cops.pc_mm_gs_flags",
            FT_UINT8, BASE_HEX, NULL, 0,
            "PacketCable Multimedia GateSpec Flags", HFILL }
        },

        { &hf_cops_pcmm_gate_spec_flags_gate,
          { "Gate", "cops.pc_mm_gs_flags.gate",
            FT_BOOLEAN, 8, TFS(&tfs_upstream_downstream), 0x1,
            NULL, HFILL }
        },

        { &hf_cops_pcmm_gate_spec_flags_dscp_overwrite,
          { "DSCP/TOS overwrite", "cops.pc_mm_gs_flags.dscp_overwrite",
            FT_BOOLEAN, 8, TFS(&tfs_enabled_disabled), 0x2,
            NULL, HFILL }
        },

        { &hf_cops_pcmm_gate_spec_dscp_tos_field,
          { "DSCP/TOS Field",           "cops.pc_mm_gs_dscp",
            FT_UINT8, BASE_HEX, NULL, 0,
            "PacketCable Multimedia GateSpec DSCP/TOS Field", HFILL }
        },
        { &hf_cops_pcmm_gate_spec_dscp_tos_mask,
          { "DSCP/TOS Mask",           "cops.pc_mm_gs_dscp_mask",
            FT_UINT8, BASE_HEX, NULL, 0,
            "PacketCable Multimedia GateSpec DSCP/TOS Mask", HFILL }
        },
        { &hf_cops_pcmm_gate_spec_session_class_id,
          { "SessionClassID", "cops.pc_mm_gs_scid",
            FT_UINT8, BASE_DEC, VALS(table_cops_session_class), 0,
            "PacketCable Multimedia GateSpec SessionClassID", HFILL }
        },
        { &hf_cops_pcmm_gate_spec_session_class_id_priority,
          { "SessionClassID Priority", "cops.pc_mm_gs_scid_prio",
            FT_UINT8, BASE_DEC, NULL, 0x07,
            "PacketCable Multimedia GateSpec SessionClassID Priority", HFILL }
        },
        { &hf_cops_pcmm_gate_spec_session_class_id_preemption,
          { "SessionClassID Preemption", "cops.pc_mm_gs_scid_preempt",
            FT_UINT8, BASE_DEC, NULL, 0x08,
            "PacketCable Multimedia GateSpec SessionClassID Preemption", HFILL }
        },
        { &hf_cops_pcmm_gate_spec_session_class_id_configurable,
          { "SessionClassID Configurable", "cops.pc_mm_gs_scid_conf",
            FT_UINT8, BASE_DEC, NULL, 0xf0,
            "PacketCable Multimedia GateSpec SessionClassID Configurable", HFILL }
        },
        { &hf_cops_pcmm_gate_spec_timer_t1,
          { "Timer T1", "cops.pc_mm_gs_timer_t1",
            FT_UINT16, BASE_DEC | BASE_UNIT_STRING, UNS(&units_second_seconds), 0,
            "PacketCable Multimedia GateSpec Timer T1", HFILL }
        },
        { &hf_cops_pcmm_gate_spec_timer_t2,
          { "Timer T2", "cops.pc_mm_gs_timer_t2",
            FT_UINT16, BASE_DEC | BASE_UNIT_STRING, UNS(&units_second_seconds), 0,
            "PacketCable Multimedia GateSpec Timer T2", HFILL }
        },
        { &hf_cops_pcmm_gate_spec_timer_t3,
          { "Timer T3", "cops.pc_mm_gs_timer_t3",
            FT_UINT16, BASE_DEC | BASE_UNIT_STRING, UNS(&units_second_seconds), 0,
            "PacketCable Multimedia GateSpec Timer T3", HFILL }
        },
        { &hf_cops_pcmm_gate_spec_timer_t4,
          { "Timer T4", "cops.pc_mm_gs_timer_t4",
            FT_UINT16, BASE_DEC | BASE_UNIT_STRING, UNS(&units_second_seconds), 0,
            "PacketCable Multimedia GateSpec Timer T4", HFILL }
        },

        { &hf_cops_pcmm_classifier_protocol_id,
          { "Protocol ID", "cops.pc_mm_classifier_proto_id",
            FT_UINT16, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Classifier Protocol ID", HFILL }
        },
        { &hf_cops_pcmm_classifier_dscp_tos_field,
          { "DSCP/TOS Field", "cops.pc_mm_classifier_dscp",
            FT_UINT8, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Classifier DSCP/TOS Field", HFILL }
        },
        { &hf_cops_pcmm_classifier_dscp_tos_mask,
          { "DSCP/TOS Mask", "cops.pc_mm_classifier_dscp_mask",
            FT_UINT8, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Classifier DSCP/TOS Mask", HFILL }
        },
        { &hf_cops_pcmm_classifier_src_addr,
          { "Source address", "cops.pc_mm_classifier_src_addr",
            FT_IPv4, BASE_NONE, NULL, 0,
            "PacketCable Multimedia Classifier Source IP Address", HFILL }
        },
        { &hf_cops_pcmm_classifier_src_mask,
          { "Source mask", "cops.pc_mm_classifier_src_mask",
            FT_IPv4, BASE_NONE, NULL, 0,
            "PacketCable Multimedia Classifier Source Mask", HFILL }
        },
        { &hf_cops_pcmm_classifier_dst_addr,
          { "Destination address", "cops.pc_mm_classifier_dst_addr",
            FT_IPv4, BASE_NONE, NULL, 0,
            "PacketCable Multimedia Classifier Destination IP Address", HFILL }
        },
        { &hf_cops_pcmm_classifier_dst_mask,
          { "Destination mask", "cops.pc_mm_classifier_dst_mask",
            FT_IPv4, BASE_NONE, NULL, 0,
            "PacketCable Multimedia Classifier Destination Mask", HFILL }
        },
        { &hf_cops_pcmm_classifier_src_port,
          { "Source Port", "cops.pc_mm_classifier_src_port",
            FT_UINT16, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Classifier Source Port", HFILL }
        },
        { &hf_cops_pcmm_classifier_src_port_end,
          { "Source Port End", "cops.pc_mm_classifier_src_port_end",
            FT_UINT16, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Classifier Source Port End", HFILL }
        },
        { &hf_cops_pcmm_classifier_dst_port,
          { "Destination Port", "cops.pc_mm_classifier_dst_port",
            FT_UINT16, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Classifier Source Port", HFILL }
        },
        { &hf_cops_pcmm_classifier_dst_port_end,
          { "Destination Port End", "cops.pc_mm_classifier_dst_port_end",
            FT_UINT16, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Classifier Source Port End", HFILL }
        },
        { &hf_cops_pcmm_classifier_priority,
          { "Priority", "cops.pc_mm_classifier_priority",
            FT_UINT8, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Classifier Priority", HFILL }
        },
        { &hf_cops_pcmm_classifier_classifier_id,
          { "Classifier Id", "cops.pc_mm_classifier_id",
            FT_UINT16, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Classifier ID", HFILL }
        },
        { &hf_cops_pcmm_classifier_activation_state,
          { "Activation State", "cops.pc_mm_classifier_activation_state",
            FT_UINT8, BASE_HEX, VALS(pcmm_activation_state_vals), 0,
            "PacketCable Multimedia Classifier Activation State", HFILL }
        },
        { &hf_cops_pcmm_classifier_action,
          { "Action", "cops.pc_mm_classifier_action",
            FT_UINT8, BASE_HEX, VALS(pcmm_action_vals), 0,
            "PacketCable Multimedia Classifier Action", HFILL }
        },
        { &hf_cops_pcmm_classifier_flags,
          { "Flags", "cops.pc_mm_classifier_flags",
            FT_UINT8, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Classifier Flags", HFILL }
        },
        { &hf_cops_pcmm_classifier_tc_low,
          { "tc-low", "cops.pc_mm_classifier_tc_low",
            FT_UINT8, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Classifier tc-low", HFILL }
        },
        { &hf_cops_pcmm_classifier_tc_high,
          { "tc-high", "cops.pc_mm_classifier_tc_high",
            FT_UINT8, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Classifier tc-high", HFILL }
        },
        { &hf_cops_pcmm_classifier_tc_mask,
          { "tc-mask", "cops.pc_mm_classifier_tc_mask",
            FT_UINT8, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Classifier tc-mask", HFILL }
        },
        { &hf_cops_pcmm_classifier_flow_label,
          { "Flow Label", "cops.pc_mm_classifier_flow_label",
            FT_UINT32, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Classifier Flow Label", HFILL }
        },
        { &hf_cops_pcmm_classifier_next_header_type,
          { "Next Header Type", "cops.pc_mm_classifier_next_header_type",
            FT_UINT16, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Classifier Next Header Type", HFILL }
        },
        { &hf_cops_pcmm_classifier_source_prefix_length,
          { "Source Prefix Length", "cops.pc_mm_classifier_source_prefix_length",
            FT_UINT8, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Classifier Source Prefix Length", HFILL }
        },
        { &hf_cops_pcmm_classifier_destination_prefix_length,
          { "Destination Prefix Length", "cops.pc_mm_classifier_destination_prefix_length",
            FT_UINT8, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Classifier Destination Prefix Length", HFILL }
        },
        { &hf_cops_pcmm_classifier_src_addr_v6,
          { "IPv6 Source Address", "cops.pc_mm_classifier_src_addr_v6",
            FT_IPv6, BASE_NONE, NULL, 0,
            "PacketCable Multimedia Classifier IPv6 Source Address", HFILL }
        },
        { &hf_cops_pcmm_classifier_dst_addr_v6,
          { "IPv6 Destination Address", "cops.pc_mm_classifier_dst_addr_v6",
            FT_IPv6, BASE_NONE, NULL, 0,
            "PacketCable Multimedia Classifier IPv6 Destination Address", HFILL }
        },

        { &hf_cops_pcmm_flow_spec_envelope,
          { "Envelope", "cops.pc_mm_fs_envelope",
            FT_UINT8, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Flow Spec Envelope", HFILL }
        },
        { &hf_cops_pcmm_flow_spec_service_number,
          { "Service Number", "cops.pc_mm_fs_svc_num",
            FT_UINT8, BASE_DEC, VALS(pcmm_flow_spec_service_vals), 0,
            "PacketCable Multimedia Flow Spec Service Number", HFILL }
        },

        { &hf_cops_pcmm_docsis_scn,
          { "Service Class Name", "cops.pc_mm_docsis_scn",
            FT_STRINGZ, BASE_NONE, NULL, 0,
            "PacketCable Multimedia DOCSIS Service Class Name", HFILL }
        },

        { &hf_cops_pcmm_envelope,
          { "Envelope", "cops.pc_mm_envelope",
            FT_UINT8, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Envelope", HFILL }
        },

        { &hf_cops_pcmm_traffic_priority,
          { "Traffic Priority", "cops.pc_mm_tp",
            FT_UINT8, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Committed Envelope Traffic Priority", HFILL }
        },
        { &hf_cops_pcmm_request_transmission_policy,
          { "Request Transmission Policy", "cops.pc_mm_rtp",
            FT_UINT32, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Committed Envelope Traffic Priority", HFILL }
        },
        { &hf_cops_pcmm_request_transmission_policy_sf_all_cm,
          { "The Service Flow MUST NOT use \"all CMs\" broadcast request opportunities", "cops.pc_mm_rtp.sf.all_cm",
            FT_BOOLEAN, 32, TFS(&tfs_yes_no), 0x00000001,
            NULL, HFILL }
        },
        { &hf_cops_pcmm_request_transmission_policy_sf_priority,
          { "The Service Flow MUST NOT use Priority Request multicast request opportunities", "cops.pc_mm_rtp.sf.priority",
            FT_BOOLEAN, 32, TFS(&tfs_yes_no), 0x00000002,
            NULL, HFILL }
        },
        { &hf_cops_pcmm_request_transmission_policy_sf_request_for_request,
          { "The Service Flow MUST NOT use Request/Data opportunities for Requests", "cops.pc_mm_rtp.sf.request_for_request",
            FT_BOOLEAN, 32, TFS(&tfs_yes_no), 0x00000004,
            NULL, HFILL }
        },
        { &hf_cops_pcmm_request_transmission_policy_sf_data_for_data,
          { "The Service Flow MUST NOT use Request/Data opportunities for Data", "cops.pc_mm_rtp.sf.data_for_data",
            FT_BOOLEAN, 32, TFS(&tfs_yes_no), 0x00000008,
            NULL, HFILL }
        },
        { &hf_cops_pcmm_request_transmission_policy_sf_piggyback,
          { "The Service Flow MUST NOT piggyback requests with data", "cops.pc_mm_rtp.sf.piggyback",
            FT_BOOLEAN, 32, TFS(&tfs_yes_no), 0x00000010,
            NULL, HFILL }
        },
        { &hf_cops_pcmm_request_transmission_policy_sf_concatenate,
          { "The Service Flow MUST NOT concatenate data", "cops.pc_mm_rtp.sf.concatenate",
            FT_BOOLEAN, 32, TFS(&tfs_yes_no), 0x00000020,
            NULL, HFILL }
        },
        { &hf_cops_pcmm_request_transmission_policy_sf_fragment,
          { "The Service Flow MUST NOT fragment data", "cops.pc_mm_rtp.sf.fragment",
            FT_BOOLEAN, 32, TFS(&tfs_yes_no), 0x00000040,
            NULL, HFILL }
        },
        { &hf_cops_pcmm_request_transmission_policy_sf_suppress,
          { "The Service Flow MUST NOT suppress payload headers", "cops.pc_mm_rtp.sf.suppress",
            FT_BOOLEAN, 32, TFS(&tfs_yes_no), 0x00000080,
            NULL, HFILL }
        },
        { &hf_cops_pcmm_request_transmission_policy_sf_drop_packets,
          { "The Service Flow MUST drop packets that do not fit in the Unsolicited Grant Size", "cops.pc_mm_rtp.sf.drop_packets",
            FT_BOOLEAN, 32, TFS(&tfs_yes_no), 0x00000100,
            NULL, HFILL }
        },
        { &hf_cops_pcmm_max_sustained_traffic_rate,
          { "Maximum Sustained Traffic Rate", "cops.pc_mm_mstr",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Committed Envelope Maximum Sustained Traffic Rate", HFILL }
        },
        { &hf_cops_pcmm_max_traffic_burst,
          { "Maximum Traffic Burst", "cops.pc_mm_mtb",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Committed Envelope Maximum Traffic Burst", HFILL }
        },
        { &hf_cops_pcmm_min_reserved_traffic_rate,
          { "Minimum Reserved Traffic Rate", "cops.pc_mm_mrtr",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Committed Envelope Minimum Reserved Traffic Rate", HFILL }
        },
        { &hf_cops_pcmm_ass_min_rtr_packet_size,
          { "Assumed Minimum Reserved Traffic Rate Packet Size", "cops.pc_mm_amrtrps",
            FT_UINT16, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Committed Envelope Assumed Minimum Reserved Traffic Rate Packet Size", HFILL }
        },
        { &hf_cops_pcmm_max_concat_burst,
          { "Maximum Concatenated Burst", "cops.pc_mm_mcburst",
            FT_UINT16, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Committed Envelope Maximum Concatenated Burst", HFILL }
        },
        { &hf_cops_pcmm_req_att_mask,
          { "Required Attribute Mask", "cops.pc_mm_ramask",
            FT_UINT32, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Committed Envelope Required Attribute Mask", HFILL }
        },
        { &hf_cops_pcmm_forbid_att_mask,
          { "Forbidden Attribute Mask", "cops.pc_mm_famask",
            FT_UINT32, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Committed Envelope Forbidden Attribute Mask", HFILL }
        },
        { &hf_cops_pcmm_att_aggr_rule_mask,
          { "Attribute Aggregation Rule Mask", "cops.pc_mm_aarmask",
            FT_UINT32, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Committed Envelope Attribute Aggregation Rule Mask", HFILL }
        },

        { &hf_cops_pcmm_nominal_polling_interval,
          { "Nominal Polling Interval", "cops.pc_mm_npi",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Nominal Polling Interval", HFILL }
        },

        { &hf_cops_pcmm_tolerated_poll_jitter,
          { "Tolerated Poll Jitter", "cops.pc_mm_tpj",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Tolerated Poll Jitter", HFILL }
        },

        { &hf_cops_pcmm_unsolicited_grant_size,
          { "Unsolicited Grant Size", "cops.pc_mm_ugs",
            FT_UINT16, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Unsolicited Grant Size", HFILL }
        },
        { &hf_cops_pcmm_grants_per_interval,
          { "Grants Per Interval", "cops.pc_mm_gpi",
            FT_UINT8, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Grants Per Interval", HFILL }
        },
        { &hf_cops_pcmm_nominal_grant_interval,
          { "Nominal Grant Interval", "cops.pc_mm_ngi",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Nominal Grant Interval", HFILL }
        },
        { &hf_cops_pcmm_tolerated_grant_jitter,
          { "Tolerated Grant Jitter", "cops.pc_mm_tgj",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Tolerated Grant Jitter", HFILL }
        },

        { &hf_cops_pcmm_down_resequencing,
          { "Downstream Resequencing", "cops.pc_mm_downres",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Downstream Resequencing", HFILL }
        },

        { &hf_cops_pcmm_down_peak_traffic_rate,
          { "Downstream Peak Traffic Rate", "cops.pc_mm_downpeak",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Downstream Peak Traffic Rate", HFILL }
        },

        { &hf_cops_pcmm_max_downstream_latency,
          { "Maximum Downstream Latency", "cops.pc_mm_mdl",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Maximum Downstream Latency", HFILL }
        },

        { &hf_cops_pcmm_volume_based_usage_limit,
          { "Usage Limit", "cops.pc_mm_vbul_ul",
            FT_UINT64, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Volume-Based Usage Limit", HFILL }
        },

        { &hf_cops_pcmm_time_based_usage_limit,
          { "Usage Limit", "cops.pc_mm_tbul_ul",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Time-Based Usage Limit", HFILL }
        },

        { &hf_cops_pcmm_gate_time_info,
          { "Gate Time Info", "cops.pc_mm_gti",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Gate Time Info", HFILL }
        },

        { &hf_cops_pcmm_gate_usage_info,
          { "Gate Usage Info", "cops.pc_mm_gui",
            FT_UINT64, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Gate Usage Info", HFILL }
        },

        { &hf_cops_pcmm_packetcable_error_code,
          { "Error Code", "cops.pc_mm_error_ec",
            FT_UINT16, BASE_DEC, VALS(pcmm_packetcable_error_code), 0,
            "PacketCable Multimedia PacketCable-Error Error-Code", HFILL }
        },
        { &hf_cops_pcmm_packetcable_error_subcode,
          { "Error Subcode", "cops.pc_mm_error_esc",
            FT_UINT16, BASE_HEX, NULL, 0,
            "PacketCable Multimedia PacketCable-Error Error Sub-code", HFILL }
        },

        { &hf_cops_pcmm_packetcable_gate_state,
          { "State", "cops.pc_mm_gs_state",
            FT_UINT16, BASE_DEC, VALS(pcmm_gate_state), 0,
            "PacketCable Multimedia Gate State", HFILL }
        },
        { &hf_cops_pcmm_packetcable_gate_state_reason,
          { "Reason", "cops.pc_mm_gs_reason",
            FT_UINT16, BASE_DEC, VALS(pcmm_gate_state_reason), 0,
            "PacketCable Multimedia Gate State Reason", HFILL }
        },
        { &hf_cops_pcmm_packetcable_version_info_major,
          { "Major Version Number", "cops.pc_mm_vi_major",
            FT_UINT16, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Major Version Number", HFILL }
        },
        { &hf_cops_pcmm_packetcable_version_info_minor,
          { "Minor Version Number", "cops.pc_mm_vi_minor",
            FT_UINT16, BASE_DEC, NULL, 0,
            "PacketCable Multimedia Minor Version Number", HFILL }
        },

        { &hf_cops_pcmm_psid,
          { "PSID", "cops.pc_mm_psid",
            FT_UINT32, BASE_DEC, NULL, 0,
            "PacketCable Multimedia PSID", HFILL }
        },

        { &hf_cops_pcmm_synch_options_report_type,
          { "Report Type", "cops.pc_mm_synch_options_report_type",
            FT_UINT8, BASE_DEC, VALS(pcmm_report_type_vals), 0,
            "PacketCable Multimedia Synch Options Report Type", HFILL }
        },
        { &hf_cops_pcmm_synch_options_synch_type,
          { "Synch Type", "cops.pc_mm_synch_options_synch_type",
            FT_UINT8, BASE_DEC, VALS(pcmm_synch_type_vals), 0,
            "PacketCable Multimedia Synch Options Synch Type", HFILL }
        },

        { &hf_cops_pcmm_msg_receipt_key,
          { "Msg Receipt Key", "cops.pc_mm_msg_receipt_key",
            FT_UINT32, BASE_HEX, NULL, 0,
            "PacketCable Multimedia Msg Receipt Key", HFILL }
        },

        { &hf_cops_pcmm_userid,
          { "UserID", "cops.pc_mm_userid",
            FT_STRING, BASE_NONE, NULL, 0,
            "PacketCable Multimedia UserID", HFILL }
        },

        { &hf_cops_pcmm_sharedresourceid,
          { "SharedResourceID", "cops.pc_mm_sharedresourceid",
            FT_UINT32, BASE_HEX, NULL, 0,
            "PacketCable Multimedia SharedResourceID", HFILL }
        },

        /* End of addition for PacketCable */

    };

    /* Setup protocol subtree array */
    static int* ett[] = {
        &ett_cops,
        &ett_cops_ver_flags,
        &ett_cops_obj,
        &ett_cops_pr_obj,
        &ett_cops_obj_data,
        &ett_cops_r_type_flags,
        &ett_cops_itf,
        &ett_cops_reason,
        &ett_cops_decision,
        &ett_cops_error,
        &ett_cops_clientsi,
        &ett_cops_asn1,
        &ett_cops_gperror,
        &ett_cops_cperror,
        &ett_cops_pdp,
        &ett_cops_subtree,
        &ett_docsis_request_transmission_policy,
        &ett_cops_pcmm_gate_spec_flags,
        &ett_cops_pcmm_gate_spec_session_class_id,
    };

    static ei_register_info ei[] = {
        { &ei_cops_pepid_not_null, { "cops.pepid.not_null", PI_MALFORMED, PI_NOTE, "PEP Id is not a NULL terminated ASCII string", EXPFILL }},
        { &ei_cops_trailing_garbage, { "cops.trailing_garbage", PI_UNDECODED, PI_NOTE, "Trailing garbage", EXPFILL }},
        { &ei_cops_bad_cops_object_length, { "cops.bad_cops_object_length", PI_MALFORMED, PI_ERROR, "COPS object length is too short", EXPFILL }},
        { &ei_cops_bad_cops_pr_object_length, { "cops.bad_cops_pr_object_length", PI_MALFORMED, PI_ERROR, "COPS-PR object length is too short", EXPFILL }},
        { &ei_cops_unknown_c_num, { "cops.unknown_c_num", PI_UNDECODED, PI_NOTE, "Unknown C-Num value", EXPFILL }},
#if 0
        { &ei_cops_unknown_s_num, { "cops.unknown_s_num", PI_UNDECODED, PI_NOTE, "Unknown S-Num value", EXPFILL }},
#endif
    };


    module_t* cops_module;
    expert_module_t* expert_cops;

    /* Register the protocol name and description */
    proto_cops = proto_register_protocol("Common Open Policy Service", "COPS", "cops");

    /* Required function calls to register the header fields and subtrees used */
    proto_register_field_array(proto_cops, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));
    expert_cops = expert_register_protocol(proto_cops);
    expert_register_field_array(expert_cops, ei, array_length(ei));

    /* Make dissector findable by name */
    cops_handle = register_dissector("cops", dissect_cops, proto_cops);

    /* Register our configuration options for cops */
    cops_module = prefs_register_protocol(proto_cops, NULL);
    prefs_register_bool_preference(cops_module, "desegment",
        "Reassemble COPS messages spanning multiple TCP segments",
        "Whether the COPS dissector should reassemble messages spanning multiple TCP segments."
        " To use this option, you must also enable \"Allow subdissectors to reassemble TCP streams\" in the TCP protocol settings.",
        &cops_desegment);

    /* For PacketCable */
    prefs_register_bool_preference(cops_module, "packetcable",
        "Decode for PacketCable clients",
        "Decode the COPS messages using PacketCable clients. (Select port 2126)",
        &cops_packetcable);

    prefs_register_static_text_preference(cops_module, "info_pibs",
        "PIB settings can be changed in the Name Resolution preferences",
        "PIB settings can be changed in the Name Resolution preferences");

    prefs_register_obsolete_preference(cops_module, "typefrommib");
}

void proto_reg_handoff_cops(void)
{
    /* These could use a separate "preference name" (to avoid collision),
        but they are IANA registered and users could still use Decode As */
    dissector_add_uint("tcp.port", TCP_PORT_PKTCABLE_COPS, cops_handle);
    dissector_add_uint("tcp.port", TCP_PORT_PKTCABLE_MM_COPS, cops_handle);

    dissector_add_uint_with_preference("tcp.port", TCP_PORT_COPS, cops_handle);
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
