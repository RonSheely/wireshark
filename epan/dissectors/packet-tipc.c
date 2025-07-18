/* packet-tipc.c
 * Routines for Transparent Inter Process Communication packet dissection
 *
 * Copyright 2005-2006, Anders Broman <anders.broman@ericsson.com>
 *
 * TIPCv2 protocol updates
 * Copyright 2006-2008, Martin Peylo <wireshark@izac.de>
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Protocol ref:
 * https://tipc.sourceforge.net/
 * https://tipc.sourceforge.net/protocol.html
 */


#include "config.h"

#include <epan/packet.h>
#include <epan/prefs.h>
#include <epan/proto_data.h>
#include <epan/expert.h>
#include <epan/etypes.h>
#include <epan/address_types.h>
#include <epan/reassemble.h>

#include <wsutil/ws_roundup.h>
#include <wsutil/ws_padding_to.h>

#include "packet-tcp.h"

void proto_register_tipc(void);

static int proto_tipc;

static int hf_tipc_msg_fragments;
static int hf_tipc_msg_fragment;
static int hf_tipc_msg_fragment_overlap;
static int hf_tipc_msg_fragment_overlap_conflicts;
static int hf_tipc_msg_fragment_multiple_tails;
static int hf_tipc_msg_fragment_too_long_fragment;
static int hf_tipc_msg_fragment_error;
static int hf_tipc_msg_fragment_count;
static int hf_tipc_msg_reassembled_in;
static int hf_tipc_msg_reassembled_length;

static int hf_tipc_ver;
static int hf_tipc_usr;
static int hf_tipcv2_usr;
static int hf_tipc_hdr_size;
static int hf_tipc_nonsequenced;
static int hf_tipc_destdrop;
static int hf_tipc_unused;
static int hf_tipc_msg_size;
static int hf_tipc_ack_link_lev_seq;
static int hf_tipc_link_lev_seq;
static int hf_tipc_prev_proc;
static int hf_tipc_org_port;
static int hf_tipc_dst_port;
static int hf_tipc_data_msg_type;
static int hf_tipc_err_code;
static int hf_tipc_reroute_cnt;
static int hf_tipc_act_id;
static int hf_tipc_org_proc;
static int hf_tipc_dst_proc;
static int hf_tipc_unused2;
static int hf_tipc_importance;
static int hf_tipc_link_selector;
static int hf_tipc_msg_cnt;
static int hf_tipc_probe;
static int hf_tipc_bearer_id;
static int hf_tipc_link_selector2;
static int hf_tipc_remote_addr;
static int hf_tipc_rm_msg_type;
static int hf_tipc_nd_msg_type;
static int hf_tipc_cm_msg_type;
static int hf_tipc_lp_msg_type;
static int hf_tipc_cng_prot_msg_type;
static int hf_tipc_sm_msg_type;
static int hf_tipc_unknown_msg_type;
static int hf_tipc_seq_gap;
static int hf_tipc_nxt_snt_pkg;
static int hf_tipc_unused_word;
static int hf_tipc_bearer_name;
static int hf_tipc_data;
static int hf_tipc_msg_no_bundle;
static int hf_tipc_changeover_protocol;
static int hf_tipc_named_msg_hdr;
static int hf_tipc_port_name_type;
static int hf_tipc_port_name_instance;
static int hf_tipc_data_fragment;
static int hf_tipc_message_bundle;


static int hf_tipc_name_dist_type;
static int hf_tipc_name_dist_lower;
static int hf_tipc_name_dist_upper;
static int hf_tipc_name_dist_port;
static int hf_tipc_name_dist_key;

static int hf_tipcv2_srcdrop;
static int hf_tipcv2_data_msg_type;
static int hf_tipcv2_bcast_mtype;
static int hf_tipcv2_bundler_mtype;
static int hf_tipcv2_link_mtype;
static int hf_tipcv2_connmgr_mtype;
static int hf_tipcv2_route_mtype_1_6;
static int hf_tipcv2_route_mtype_1_7;
static int hf_tipcv2_changeover_mtype;
static int hf_tipcv2_naming_mtype;
static int hf_tipcv2_fragmenter_mtype;
static int hf_tipcv2_neighbour_mtype;
static int hf_tipcv2_errorcode;
static int hf_tipcv2_rer_cnt;
static int hf_tipcv2_lookup_scope;
static int hf_tipcv2_opt_p;
static int hf_tipcv2_broadcast_ack_no;
static int hf_tipcv2_link_level_ack_no;
static int hf_tipcv2_link_level_seq_no;
/* static int hf_tipcv2_bcast_seq_no; */
static int hf_tipcv2_prev_node;
static int hf_tipcv2_orig_node;
static int hf_tipcv2_dest_node;
static int hf_tipcv2_port_name_type;
static int hf_tipcv2_port_name_instance;
static int hf_tipcv2_multicast_lower;
static int hf_tipcv2_multicast_upper;

static int hf_tipcv2_sequence_gap;
static int hf_tipcv2_next_sent_broadcast;
static int hf_tipcv2_fragment_number;
static int hf_tipcv2_fragment_msg_number;
static int hf_tipcv2_next_sent_packet;
static int hf_tipcv2_session_no;
static int hf_tipcv2_link_prio;
static int hf_tipcv2_network_plane;
static int hf_tipcv2_probe;
static int hf_tipcv2_link_tolerance;
static int hf_tipcv2_bearer_instance;
static int hf_tipcv2_padding;
static int hf_tipcv2_bearer_level_orig_addr;
static int hf_tipcv2_cluster_address;
static int hf_tipcv2_bitmap;
static int hf_tipcv2_node_address;
static int hf_tipcv2_destination_domain;
static int hf_tipcv2_network_id;

static int hf_tipcv2_bcast_tag;
static int hf_tipcv2_msg_count;
static int hf_tipcv2_max_packet;
static int hf_tipcv2_transport_seq_no;
static int hf_tipcv2_redundant_link;
static int hf_tipcv2_bearer_id;
static int hf_tipcv2_conn_mgr_msg_ack;
static int hf_tipcv2_minor_pv;
static int hf_tipcv2_node_sig;
static int hf_tipcv2_filler_mtu_discovery;
static int hf_tipcv2_vendor_specific_data;
static int hf_tipcv2_options;

/* added for TIPC v1.7 */
static int hf_tipcv2_timestamp;
static int hf_tipcv2_item_size;
static int hf_tipcv2_network_region;
static int hf_tipcv2_local_router;
static int hf_tipcv2_remote_router;
static int hf_tipcv2_dist_dist;
static int hf_tipcv2_dist_scope;
static int hf_tipcv2_name_dist_port_id_node;
static int hf_tipcv2_media_id;

/* added in minor PV 1 */
static int hf_tipcv2_syn;


static int ett_tipc_msg_fragment;
static int ett_tipc_msg_fragments;


/* Initialize the subtree pointer */
static int ett_tipc;
static int ett_tipc_data;

static expert_field ei_tipc_field_not_specified;
static expert_field ei_tipc_invalid_bundle_size;
static expert_field ei_tipc_max_recursion_depth_reached;

static int tipc_address_type = -1;

/* protocol preferences */
static bool tipc_defragment = true;
static bool dissect_tipc_data = true;
static bool try_heuristic_first;
#define V2_AS_ALL  0x1
#define V2_AS_1_6  0x2
#define V2_AS_1_7  0x4
static int      handle_v2_as = V2_AS_ALL;
static bool tipc_tcp_desegment = true;

static dissector_handle_t tipc_handle;
static dissector_handle_t tipc_tcp_handle;

/* IANA have assigned port 6118 port for TIPC UDP transport. */
#define DEFAULT_TIPC_PORT_RANGE   "6118"

/* this is used to find encapsulated protocols */
static dissector_table_t tipc_user_dissector;
static dissector_table_t tipc_type_dissector;

static heur_dissector_list_t tipc_heur_subdissector_list;

static proto_tree *top_tree;

static const fragment_items tipc_msg_frag_items = {
	/* Fragment subtrees */
	&ett_tipc_msg_fragment,
	&ett_tipc_msg_fragments,
	/* Fragment fields */
	&hf_tipc_msg_fragments,
	&hf_tipc_msg_fragment,
	&hf_tipc_msg_fragment_overlap,
	&hf_tipc_msg_fragment_overlap_conflicts,
	&hf_tipc_msg_fragment_multiple_tails,
	&hf_tipc_msg_fragment_too_long_fragment,
	&hf_tipc_msg_fragment_error,
	&hf_tipc_msg_fragment_count,
	/* Reassembled in field */
	&hf_tipc_msg_reassembled_in,
	/* Reassembled length field */
	&hf_tipc_msg_reassembled_length,
	/* Reassembled data field */
	NULL,
	/* Tag */
	"TIPC Message fragments"
};


#define MAX_TIPC_ADDRESS_STR_LEN   15
#define TIPCv1 1
#define TIPCv2 2
/* Users */
#define TIPC_DATA_PRIO_0            0
#define TIPC_DATA_PRIO_1            1
#define TIPC_DATA_PRIO_2            2
#define TIPC_DATA_NON_REJECTABLE    3

#define TIPC_ROUTING_MANAGER        8
#define TIPC_NAME_DISTRIBUTOR       9
#define TIPC_CONNECTION_MANAGER    10
#define TIPC_LINK_PROTOCOL         11
#define TIPC_CHANGEOVER_PROTOCOL   13
#define TIPC_SEGMENTATION_MANAGER  14
#define TIPC_MSG_BUNDLER           15

#define TIPC_LINK_PROTOCO_STATE_MSG 0

static const value_string tipc_user_values[] = {
	{ TIPC_DATA_PRIO_0,          "DATA_PRIO_0"},
	{ TIPC_DATA_PRIO_1,          "DATA_PRIO_1"},
	{ TIPC_DATA_PRIO_2,          "DATA_PRIO_2"},
	{ TIPC_DATA_NON_REJECTABLE,  "DATA_NON_REJECTABLE"},
	{ TIPC_ROUTING_MANAGER,      "ROUTING_MANAGER"},
	{ TIPC_NAME_DISTRIBUTOR,     "NAME_DISTRIBUTOR"},
	{ TIPC_CONNECTION_MANAGER,   "CONNECTION_MANAGER"},
	{ TIPC_LINK_PROTOCOL,        "LINK_PROTOCOL"},
	{ TIPC_CHANGEOVER_PROTOCOL,  "CHANGEOVER_PROTOCOL"},
	{ TIPC_SEGMENTATION_MANAGER, "SEGMENTATION_MANAGER"},
	{ TIPC_MSG_BUNDLER,          "MSG_BUNDLER"},
	{ 0, NULL}
};

#define TIPCv2_DATA_LOW             0
#define TIPCv2_DATA_NORMAL          1
#define TIPCv2_DATA_HIGH            2
#define TIPCv2_DATA_NON_REJECTABLE  3

#define TIPCv2_BCAST_PROTOCOL       5
#define TIPCv2_MSG_BUNDLER          6
#define TIPCv2_LINK_PROTOCOL        7
#define TIPCv2_CONN_MANAGER         8
#define TIPCv2_ROUTE_DISTRIBUTOR    9
#define TIPCv2_CHANGEOVER_PROTOCOL 10
#define TIPCv2_NAME_DISTRIBUTOR    11
#define TIPCv2_MSG_FRAGMENTER      12
#define TIPCv2_NEIGHBOUR_DISCOVERY  13

#define TIPCv2_USER_FIRST_FRAGMENT	0
#define TIPCv2_USER_FRAGMENT		1
#define TIPCv2_USER_LAST_FRAGMENT	2

static const value_string tipcv2_user_values[] = {
	{ TIPCv2_DATA_LOW,            "Low Priority Payload Data"},
	{ TIPCv2_DATA_NORMAL,         "Normal Priority Payload Data"},
	{ TIPCv2_DATA_HIGH,           "High Priority Payload Data"},
	{ TIPCv2_DATA_NON_REJECTABLE, "Non-Rejectable Payload Data"},
	{ TIPCv2_BCAST_PROTOCOL,      "Broadcast Maintenance Protocol"},
	{ TIPCv2_MSG_BUNDLER,         "Message Bundler Protocol"},
	{ TIPCv2_LINK_PROTOCOL,       "Link State Maintenance Protocol"},
	{ TIPCv2_CONN_MANAGER,        "Connection Manager"},
	{ TIPCv2_ROUTE_DISTRIBUTOR,   "Routing Table Update Protocol"},
	{ TIPCv2_CHANGEOVER_PROTOCOL, "Link Changeover Protocol"},
	{ TIPCv2_NAME_DISTRIBUTOR,    "Name Table Update Protocol"},
	{ TIPCv2_MSG_FRAGMENTER,      "Message Fragmentation Protocol"},
	{ TIPCv2_NEIGHBOUR_DISCOVERY,  "Neighbour Discovery Protocol"},
	{ 0, NULL}
};

static const value_string tipcv2_user_short_str_vals[] = {
	{ TIPCv2_DATA_LOW,            "Payld:Low"},
	{ TIPCv2_DATA_NORMAL,         "Payld:Normal"},
	{ TIPCv2_DATA_HIGH,           "Payld:High"},
	{ TIPCv2_DATA_NON_REJECTABLE, "Payld:NoRej"},
	{ TIPCv2_BCAST_PROTOCOL,      "Broadcast"},
	{ TIPCv2_MSG_BUNDLER,         "Bundler"},
	{ TIPCv2_LINK_PROTOCOL,       "Link State"},
	{ TIPCv2_CONN_MANAGER,        "Conn Mgr"},
	{ TIPCv2_ROUTE_DISTRIBUTOR,   "Route Dist"},
	{ TIPCv2_CHANGEOVER_PROTOCOL, "Changeover"},
	{ TIPCv2_NAME_DISTRIBUTOR,    "Name Dist"},
	{ TIPCv2_MSG_FRAGMENTER,      "Fragmenter"},
	{ TIPCv2_NEIGHBOUR_DISCOVERY,  "Ngbr Disc"},
	{ 0, NULL}
};

#define TIPC_CONNECTED_MSG	0
#define TIPC_NAMED_MSG		2
#define TIPC_DIRECT_MSG		3
#define TIPC_OVERLOAD_W_MSG	4

static const value_string tipc_data_msg_type_values[] = {
	{ 0, "CONN_MSG"},
	{ 2, "NAMED_MSG"},
	{ 3, "DIRECT_MSG"},
	{ 4, "OVERLOAD_W_MSG"},
	{ 0, NULL}
};

static const value_string tipcv2_data_msg_type_defines[] = {
	{ 0, "ConnMsg"},
	{ 1, "McastMsg"},
	{ 2, "NamedMsg"},
	{ 3, "DirectMsg"},
	{ 0, NULL}
};

static const value_string tipcv2_data_msg_type_values[] _U_ = {
	{ 0, "Sent on connection (CONN_MSG)"},
	{ 1, "Logical multicast (MCAST_MSG)"},
	{ 2, "Port name destination address (NAMED_MSG)"},
	{ 3, "Port identity destination address (DIRECT_MSG)"},
	{ 0, NULL}
};

static const value_string tipc_error_code_values[] = {
	{ 0, "MSG_OK"},
	{ 1, "NO_PORT_NAME"},
	{ 2, "NO_REMOTE_PORT"},
	{ 3, "NO_REMOTE_PROCESSOR"},
	{ 4, "DEST_OVERLOADED"},
	{ 5, "CONN_SHUTDOWN"},
	{ 6, "NO_CONNECTION"},
	{ 7, "COMMUNICATION_ERROR"},
	{ 0, NULL}
};

static const value_string tipcv2_error_code_strings[] = {
	{ 0, "No error (TIPC_OK)"},
	{ 1, "Destination port name unknown (TIPC_ERR_NO_NAME)"},
	{ 2, "Destination port does not exist (TIPC_ERR_NO_PORT)"},
	{ 3, "Destination node unavailable (TIPC_ERR_NO_NODE)"},
	{ 4, "Destination node overloaded (TIPC_ERR_OVERLOAD)"},
	{ 5, "Connection Shutdown (No error) (TIPC_CONN_SHUTDOWN)"},
	{ 6, "Communication Error (TIPC_CONN_ERROR)"},
	{ 0, NULL}
};

static const value_string tipcv2_error_code_short_strings[] = {
	{ 0, "OK"},
	{ 1, "ErrNoName"},
	{ 2, "ErrNoPort"},
	{ 3, "ErrNoNode"},
	{ 4, "ErrOverload"},
	{ 5, "ConnShutdown"},
	{ 6, "ConnError"},
	{ 0, NULL}
};

static const value_string tipcv2_lookup_scope_strings[] = {
	{ 0, "Zone Scope"},
	{ 1, "Cluster Scope"},
	{ 2, "Node Scope"},
	{ 0, NULL}
};

static const value_string tipc_routing_mgr_msg_type_values[] = {
	{ 0, "EXT_ROUTING_TABLE"},
	{ 1, "LOCAL_ROUTING_TABLE"},
	{ 2, "DP_ROUTING_TABLE"},
	{ 3, "ROUTE_ADDITION"},
	{ 4, "ROUTE_REMOVAL"},
	{ 0, NULL}
};

static const value_string tipc_name_dist_msg_type_values[] = {
	{ 0, "PUBLICATION"},
	{ 1, "WITHDRAWAL"},
	{ 0, NULL}
};

/* CONNECTION_MANAGER */
static const value_string tipc_cm_msg_type_values[] = {
	{ 0, "CONNECTION_PROBE"},
	{ 1, "CONNECTION_PROBE_REPLY"},
	{ 0, NULL}
};

static const value_string tipc_link_prot_msg_type_values[] = {
	{ 10, "RESET_MSG"},
	{ 11, "ACTIVATE_MSG"},
	{ 12, "STATE_MSG"},
	{ 0, NULL}
};

/* CHANGEOVER_PROTOCOL */
static const value_string tipc_cng_prot_msg_type_values[] = {
	{ 0, "DUPLICATE_MSG"},
	{ 1, "ORIGINAL_MSG"},
	{ 2, "INFO_MSG"},
	{ 0, NULL}
};

/* SEGMENTATION_MANAGER */
#define TIPC_FIRST_SEGMENT	1
#define TIPC_SEGMENT		2
static const value_string tipc_sm_msg_type_values[] = {
	{ 1, "FIRST_SEGMENT"},
	{ 2, "SEGMENT"},
	{ 0, NULL}
};

/* TIPCv2_BCAST_PROTOCOL - Broadcast Maintenance Protocol */
static const value_string tipcv2_bcast_mtype_strings[] = {
	{ 0, "Bcast"},
	{ 0, NULL}
};

/* TIPCv2_MSG_BUNDLER - Message Bundler Protocol */
static const value_string tipcv2_bundler_mtype_strings[] = {
	{ 1, "Bundler"},
	{ 0, NULL}
};

/* TIPCv2_LINK_PROTOCOL - Link State Maintenance Protocol */
#define TIPCv2_STATE_MSG	0
#define TIPCv2_RESET_MSG	1
#define TIPCv2_ACTIV_MSG	2

static const value_string tipcv2_link_mtype_strings[] = {
	{ TIPCv2_STATE_MSG, "State"},
	{ TIPCv2_RESET_MSG, "Reset"},
	{ TIPCv2_ACTIV_MSG, "Activate"},
	{ 0, NULL}
};

/* TIPCv2_CONN_MANAGER - Connection Manager */
#define TIPCv2_CONMGR_CONN_PROBE	0
#define TIPCv2_CONMGR_CONN_PROBE_REPLY  1
#define TIPCv2_CONMGR_MSG_ACK		2
static const value_string tipcv2_connmgr_mtype_strings[] = {
	{ TIPCv2_CONMGR_CONN_PROBE       , "Probe"},
	{ TIPCv2_CONMGR_CONN_PROBE_REPLY , "ProbeReply"},
	{ TIPCv2_CONMGR_MSG_ACK          , "Ack"},
	{ 0, NULL}
};

/* TIPCv2_ROUTE_DISTRIBUTOR - Routing Table Update Protocol */
/* for TIPC 1.6 */
#define TIPCv2_EXT_ROUTING_TABLE	0
#define TIPCv2_LOCAL_ROUTING_TABLE	1
#define TIPCv2_SEC_ROUTING_TABLE	2
#define TIPCv2_ROUTE_ADDITION		3
#define TIPCv2_ROUTE_REMOVAL		4
/* for TIPC 1.7 */
#define TIPCv2_DIST_PUBLISH	0
#define TIPCv2_DIST_WITHDRAW	1
#define TIPCv2_DIST_PURGE	2

static const value_string tipcv2_route_mtype_strings_1_6[] = {
	{ 0, "ExtRoutingTab"},
	{ 1, "LocalRoutingTab"},
	{ 2, "SecRoutingTab"},
	{ 3, "RouteAddition"},
	{ 4, "RouteRemoval"},
	{ 0, NULL}
};

static const value_string tipcv2_route_mtype_strings_1_7[] = {
	{ 0, "Dist Publish"},
	{ 1, "Dist Withdraw"},
	{ 2, "Dist Purge"},
	{ 0, NULL}
};

static const value_string tipcv2_dist_dist_strings[] = {
	{ 0, "Nowhere"},
	{ 1, "To Cluster"},
	{ 2, "To Zone"},
	{ 3, "To Cluster and Zone"},
	{ 4, "To Network"},
	{ 5, "To Cluster and Network"},
	{ 6, "To Zone and Network"},
	{ 7, "To Cluster, Zone and Network"},
	{ 0, NULL}
};

static const value_string tipcv2_dist_scope_strings[] = {
	{ 0, "Zone Scope"},
	{ 1, "Cluster Scope"},
	{ 2, "Node Scope"},
	{ 0, NULL}
};

/* TIPCv2_CHANGEOVER_PROTOCOL - Link Changeover Protocol */
static const value_string tipcv2_changeover_mtype_strings[] = {
	{ 0, "Duplicate"},
	{ 1, "Original"},
	{ 0, NULL}
};

/* TIPCv2_NAME_DISTRIBUTOR - Name Table Update Protocol */
static const value_string tipcv2_naming_mtype_strings[] = {
	{ 0, "Publication"},
	{ 1, "Withdrawal"},
	{ 0, NULL}
};

/* TIPCv2_MSG_FRAGMENTER - Message Fragmentation Protocol" */
static const value_string tipcv2_fragmenter_mtype_strings[] = {
	{ 0, "First"},
	{ 1, "Fragment"},
	{ 2, "Last"},
	{ 0, NULL}
};

/* TIPCv2_NEIGHBOUR_DISCOVERY
 * 4.3.9 Neighbour Detection Protocol
 */

static const value_string tipcv2_neighbour_mtype_strings[] = {
	{ 0, "Request"},
	{ 1, "Response"},
	{ 0, NULL}
};

static const value_string tipcv2_networkplane_strings[] = {
	{ 0, "A"},
	{ 1, "B"},
	{ 2, "C"},
	{ 3, "D"},
	{ 4, "E"},
	{ 5, "F"},
	{ 0, NULL}
};

/* functions forward declarations */
static int dissect_tipc(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void* data _U_);
void proto_reg_handoff_tipc(void);

static reassembly_table tipc_msg_reassembly_table;

static char*
tipc_addr_value_to_buf(unsigned tipc_address, char *buf, int buf_len)
{
	uint8_t zone;
	uint16_t subnetwork;
	uint16_t processor;

	processor = tipc_address & 0x0fff;

	tipc_address = tipc_address >> 12;
	subnetwork = tipc_address & 0x0fff;

	tipc_address = tipc_address >> 12;
	zone = tipc_address & 0xff;

	snprintf(buf, buf_len, "%u.%u.%u", zone, subnetwork, processor);
	return buf;
}

static char *
tipc_addr_to_str(wmem_allocator_t* allocator, unsigned tipc_address)
{
	char *buf;

	buf = (char *)wmem_alloc(allocator, MAX_TIPC_ADDRESS_STR_LEN);
	return tipc_addr_value_to_buf(tipc_address, buf, MAX_TIPC_ADDRESS_STR_LEN);
}

static int
tipc_addr_to_str_buf(const address* addr, char *buf, int buf_len)
{
	const uint8_t *data = (const uint8_t *)addr->data;
	uint32_t tipc_address;

	tipc_address = data[0];
	tipc_address = (tipc_address << 8) ^ data[1];
	tipc_address = (tipc_address << 8) ^ data[2];
	tipc_address = (tipc_address << 8) ^ data[3];

	tipc_addr_value_to_buf(tipc_address, buf, buf_len);
	return (int)(strlen(buf)+1);
}

static int
tipc_addr_str_len(const address* addr _U_)
{
	return MAX_TIPC_ADDRESS_STR_LEN;
}


/*
   All name distributor messages have a data part containing one or more table elements with
   the following five-word structure:
   struct DistributionItem {
   unsigned int type; / Published port name type /
   unsigned int lower; / Lower bound of published sequence /
   unsigned int upper; / Upper bound of published sequence /
   unsigned int port; / Random number part of port identity /
   unsigned int key; / Use for verification at withdrawal /
   };
   */
static void
dissect_tipc_name_dist_data(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, uint8_t item_size)
{
	int offset = 0;
	uint32_t dword;
	char *addr_str_ptr;

	if ((handle_v2_as & V2_AS_1_6) || ((handle_v2_as & (V2_AS_ALL) && item_size == 0))) {
		/* TIPC 1.6 */
		while (tvb_reported_length_remaining(tvb, offset) > 0) {
			proto_tree_add_item(tree, hf_tipc_name_dist_type, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset+4;
			proto_tree_add_item(tree, hf_tipc_name_dist_lower, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset+4;
			proto_tree_add_item(tree, hf_tipc_name_dist_upper, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset+4;
			proto_tree_add_item(tree, hf_tipc_name_dist_port, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset+4;
			proto_tree_add_item(tree, hf_tipc_name_dist_key, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset+4;
		}
	} else {
		/* TIPC 1.7 */
		while (tvb_reported_length_remaining(tvb, offset) > 0) {
			proto_tree_add_item(tree, hf_tipc_name_dist_type, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset+4;
			proto_tree_add_item(tree, hf_tipc_name_dist_lower, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset+4;
			proto_tree_add_item(tree, hf_tipc_name_dist_upper, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset+4;
			proto_tree_add_item(tree, hf_tipc_name_dist_port, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset+4;
			proto_tree_add_item(tree, hf_tipc_name_dist_key, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset+4;
			dword = tvb_get_ntohl(tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tree, hf_tipcv2_name_dist_port_id_node, tvb, offset, 4, addr_str_ptr);
			offset = offset+4;
			proto_tree_add_item(tree, hf_tipcv2_dist_dist, tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tree, hf_tipcv2_dist_scope, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			if (item_size == 7) continue;
			/* if item_size is >7, the following fields are ignored
			 * so far */
			proto_tree_add_expert(tree, pinfo, &ei_tipc_field_not_specified, tvb, offset, ((item_size-7)*4));
			offset += (item_size-7)*4;
		}
	}
}

/* Set message type in COL INFO and return type of message (data or Internal message type */
static void
tipc_v2_set_info_col(tvbuff_t *tvb, packet_info *pinfo, uint8_t user, uint8_t msg_type, uint8_t hdr_size)
{
	uint32_t portNameInst, dword;
	uint32_t portNameType, portNameInstLow, portNameInstHigh;
	uint8_t error;
	/*uint8_t item_size = 0;*/

	switch (user) {
		case TIPCv2_DATA_LOW:
		case TIPCv2_DATA_NORMAL:
		case TIPCv2_DATA_HIGH:
		case TIPCv2_DATA_NON_REJECTABLE:
			col_append_fstr(pinfo->cinfo, COL_INFO, " %s", val_to_str_const(msg_type, tipcv2_data_msg_type_defines, "unknown"));

			/* Display Error != 0 in Info Column */
			dword = tvb_get_ntohl(tvb, 4);
			error = (dword>>25) & 0xf;
			if (error > 0)
				col_append_fstr(pinfo->cinfo, COL_INFO, " %s", val_to_str_const(error, tipcv2_error_code_short_strings, "unknown"));
			if (hdr_size > 8) {
				/* Port Name Type: 32 bits */
				portNameType = tvb_get_ntohl(tvb, 32);
				col_append_fstr(pinfo->cinfo, COL_INFO, " type:%d", portNameType);
				if (hdr_size > 9) {
					/* W9 name instance/multicast lower bound  */
					portNameInst = tvb_get_ntohl(tvb, 36);
					col_append_fstr(pinfo->cinfo, COL_INFO, " inst:%d", portNameInst);
					/*  Port Name Sequence Lower: 32 bits */
					if (hdr_size > 10) {
						portNameInst = tvb_get_ntohl(tvb, 40);
						col_append_fstr(pinfo->cinfo, COL_INFO, "-%d", portNameInst);
					}
				}
			}
			break;
		case TIPCv2_BCAST_PROTOCOL:
			col_append_fstr(pinfo->cinfo, COL_INFO, " %s", val_to_str_const(msg_type, tipcv2_bcast_mtype_strings, "unknown"));
			break;
		case TIPCv2_MSG_BUNDLER:
			/* No message types */
			break;
		case TIPCv2_LINK_PROTOCOL:
			col_append_fstr(pinfo->cinfo, COL_INFO, " %s", val_to_str_const(msg_type, tipcv2_link_mtype_strings, "unknown"));
			break;
		case TIPCv2_CONN_MANAGER:
			dword = tvb_get_ntohl(tvb, 4);
			/* Display Error != 0 in Info Column */
			error = (dword>>25) & 0xf;
			if (error > 0)
				col_append_fstr(pinfo->cinfo, COL_INFO, " %s", val_to_str_const(error, tipcv2_error_code_short_strings, "unknown"));
			col_append_fstr(pinfo->cinfo, COL_INFO, " %s", val_to_str_const(msg_type, tipcv2_connmgr_mtype_strings, "unknown"));
			break;
		case TIPCv2_ROUTE_DISTRIBUTOR:
			/* determine if it is TIPC v1.6 or v1.7 */
			/*dword = tvb_get_ntohl(tvb, 36);  */
			/*item_size = (dword >> 24) & 0xff;*/
			if ((handle_v2_as & V2_AS_1_6) || ((handle_v2_as & V2_AS_ALL) == 0)) {
				col_append_fstr(pinfo->cinfo, COL_INFO, " %s", val_to_str_const(msg_type, tipcv2_route_mtype_strings_1_6, "unknown"));
			} else {
				col_append_fstr(pinfo->cinfo, COL_INFO, " %s", val_to_str_const(msg_type, tipcv2_route_mtype_strings_1_7, "unknown"));
			}
			break;
		case TIPCv2_CHANGEOVER_PROTOCOL:
			col_append_fstr(pinfo->cinfo, COL_INFO, " %s", val_to_str_const(msg_type, tipcv2_changeover_mtype_strings, "unknown"));
			break;
		case TIPCv2_NAME_DISTRIBUTOR:
			portNameType     = tvb_get_ntohl(tvb, 40);
			portNameInstLow  = tvb_get_ntohl(tvb, 44);
			portNameInstHigh = tvb_get_ntohl(tvb, 48);

			if (portNameInstLow == portNameInstHigh) {
				col_append_fstr(pinfo->cinfo, COL_INFO, " %s type:%d inst:%d",
						val_to_str_const(msg_type, tipcv2_naming_mtype_strings, "unknown"), portNameType, portNameInstLow);
			} else {
				/* sequence */
				col_append_fstr(pinfo->cinfo, COL_INFO, " %s type:%d seq:%d-%d",
						val_to_str_const(msg_type, tipcv2_naming_mtype_strings, "unknown"), portNameType, portNameInstLow, portNameInstHigh);
			}
			break;
		case TIPCv2_MSG_FRAGMENTER:
			col_append_fstr(pinfo->cinfo, COL_INFO, " %s", val_to_str_const(msg_type, tipcv2_fragmenter_mtype_strings, "unknown"));
			break;
		case TIPCv2_NEIGHBOUR_DISCOVERY:
			col_append_fstr(pinfo->cinfo, COL_INFO, " %s", val_to_str_const(msg_type, tipcv2_neighbour_mtype_strings, "unknown"));
			break;
		default:
			break;
	}
}

/* Set message type in COL INFO and return type of message (data or Internal message type */
static bool
tipc_v1_set_col_msgtype(packet_info *pinfo, uint8_t user, uint8_t msg_type)
{
	bool datatype_hdr = false;

	switch (user) {
		case TIPC_DATA_PRIO_0:
		case TIPC_DATA_PRIO_1:
		case TIPC_DATA_PRIO_2:
		case TIPC_DATA_NON_REJECTABLE:
			/*
			 * src and dest address will be found at different location depending on User as hdr_size
			 */
			datatype_hdr = true;
			col_append_fstr(pinfo->cinfo, COL_INFO, "%s(%u) ", val_to_str_const(msg_type, tipc_data_msg_type_values, "unknown"), msg_type);
			break;
		case TIPC_NAME_DISTRIBUTOR:
			col_append_fstr(pinfo->cinfo, COL_INFO, "%s(%u) ", val_to_str_const(msg_type, tipc_name_dist_msg_type_values, "unknown"), msg_type);
			break;
		case TIPC_CONNECTION_MANAGER:
			col_append_fstr(pinfo->cinfo, COL_INFO, "%s(%u) ", val_to_str_const(msg_type, tipc_cm_msg_type_values, "unknown"), msg_type);
			break;
		case TIPC_ROUTING_MANAGER:
			col_append_fstr(pinfo->cinfo, COL_INFO, "%s(%u) ", val_to_str_const(msg_type, tipc_routing_mgr_msg_type_values, "unknown"), msg_type);
			break;
		case TIPC_LINK_PROTOCOL:
			col_append_fstr(pinfo->cinfo, COL_INFO, "%s(%u) ", val_to_str_const(msg_type, tipc_link_prot_msg_type_values, "unknown"), msg_type);
			break;
		case TIPC_CHANGEOVER_PROTOCOL:
			col_append_fstr(pinfo->cinfo, COL_INFO, "%s(%u) ", val_to_str_const(msg_type, tipc_cng_prot_msg_type_values, "unknown"), msg_type);
			break;
		case TIPC_SEGMENTATION_MANAGER:
			col_append_fstr(pinfo->cinfo, COL_INFO, "%s(%u) ", val_to_str_const(msg_type, tipc_sm_msg_type_values, "unknown"), msg_type);
			break;
		case TIPC_MSG_BUNDLER:
			break;
		default:
			break;
	}
	return datatype_hdr;
}

/*
   Version 2(draft-maloy-tipc-01.txt):
   4.2.1 Internal Message Header Format

   0                   1                   2                   3
   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w0:|vers |msg usr|hdr sz |n|resrv|            packet size          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w1:|m typ|      sequence gap       |       broadcast ack no        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w2:|        link level ack no      |   broadcast/link level seq no |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w3:|                       previous node                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w4:|  next sent broadcast/fragm no | next sent pkt/ fragm msg no   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w5:|          session no           | res |r|berid|link prio|netpl|p|
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w6:|                      originating node                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w7:|                      destination node                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w8:|                  transport sequence number                    |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w9:|          msg count            |       link tolerance          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   \                                                               \
   /                     User Specific Data                        /
   \                                                               \
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

static int
// NOLINTNEXTLINE(misc-no-recursion)
dissect_tipc_v2_internal_msg(tvbuff_t *tipc_tvb, proto_tree *tipc_tree, packet_info *pinfo, int offset, uint8_t user, uint32_t msg_size, uint8_t orig_hdr_size)
{
	uint32_t dword;
	char *addr_str_ptr;
	tvbuff_t *data_tvb;
	uint8_t message_type;
	uint8_t item_size = 0;
	uint16_t message_count;
	unsigned msg_no = 0;
	uint32_t msg_in_bundle_size;
	uint8_t msg_in_bundle_user;
	uint32_t b_inst_strlen;
	unsigned padlen;

	/* for fragmented messages */
	int len, reported_len;
	bool save_fragmented;
	uint32_t frag_no, frag_msg_no;
	tvbuff_t* new_tvb = NULL;
	fragment_head *frag_msg = NULL;
	proto_item *ti;

	message_type = (tvb_get_uint8(tipc_tvb, offset) >>5) & 0x7;

	switch (user) {
		case TIPCv2_BCAST_PROTOCOL:
			/* W1 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_bcast_mtype, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			/* NO sequence gap */
			proto_tree_add_item(tipc_tree, hf_tipcv2_broadcast_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W2 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_seq_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W3 */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_prev_node, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;
			if (handle_v2_as & (V2_AS_1_6)) {
				/* W4 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 4 unused for this user");
				offset = offset + 4;
				/* W5 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 5 unused for this user");
				offset = offset + 4;
				/* W6 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 6 unused for this user");
				offset = offset + 4;
				/* W7 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 7 unused for this user");
				offset = offset + 4;
				/* W8 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 8 unused for this user");
				offset = offset + 4;
			} else {
				/* W4 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 4 unused for this user");
				offset = offset + 4;
				/* W5 */
				proto_tree_add_item(tipc_tree, hf_tipcv2_network_id, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset = offset + 4;
				/* W6 */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_orig_node, tipc_tvb, offset, 4, addr_str_ptr);
				offset = offset + 4;
				/* W7 */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_dest_node, tipc_tvb, offset, 4, addr_str_ptr);
				offset = offset + 4;
				/* W8 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 8 unused for this user");
				offset = offset + 4;
			}
			/* W9 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_bcast_tag, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			/* NO link tolerance */
			offset = offset + 4;
			break;
		case TIPCv2_MSG_BUNDLER:
			if (handle_v2_as & (V2_AS_1_6)) {
				/* W1 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 1 unused for this user");
				offset = offset + 4;
				/* W2 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 2 unused for this user");
				offset = offset + 4;
			} else {
				/* W1 */
				proto_tree_add_item(tipc_tree, hf_tipcv2_bundler_mtype, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				proto_tree_add_item(tipc_tree, hf_tipcv2_broadcast_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset = offset + 4;
				/* W2 */
				proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_seq_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset = offset + 4;
			}
			/* W3 */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_prev_node, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;
			if (handle_v2_as & (V2_AS_1_6)) {
				/* W4 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 4 unused for this user");
				offset = offset + 4;
				/* W5 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 5 unused for this user");
				offset = offset + 4;
				/* W6 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 6 unused for this user");
				offset = offset + 4;
				/* W7 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 7 unused for this user");
				offset = offset + 4;
				/* W8 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 8 unused for this user");
				offset = offset + 4;
			} else {
				/* W4 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 4 unused for this user");
				offset = offset + 4;
				/* W5 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 5 unused for this user");
				offset = offset + 4;
				/* W6 */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_orig_node, tipc_tvb, offset, 4, addr_str_ptr);
				offset = offset + 4;
				/* W7 */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_dest_node, tipc_tvb, offset, 4, addr_str_ptr);
				offset = offset + 4;
				/* W8 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 8 unused for this user");
				offset = offset + 4;
			}
			/* W9 */
			/* Message Count: 16 bits. */
			proto_tree_add_item(tipc_tree, hf_tipcv2_msg_count, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			message_count = tvb_get_ntohs(tipc_tvb, offset);
			/* According to the spec this should not be set here,
			 * while there is data != 0 in this field when capturing
			 *
			 proto_tree_add_item(tipc_tree, hf_tipcv2_link_tolerance, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			 */
			offset = offset + 4;
			/* This should give equal results like
			 * while (message_count-- > 0) */
			while ((uint32_t)offset < msg_size) {
				msg_no++;

				dword = tvb_get_ntohl(tipc_tvb, offset);
				msg_in_bundle_size = dword & 0x1ffff;
				msg_in_bundle_user = (dword >> 25) & 0xf;

				ti = proto_tree_add_uint_format(top_tree, hf_tipc_msg_no_bundle, tipc_tvb, offset, 1, msg_no, "Message %u of %u in Bundle (%s)",
						msg_no, message_count, val_to_str_const(msg_in_bundle_user, tipcv2_user_short_str_vals, "unknown"));
				proto_item_set_len(ti, msg_in_bundle_size);
				data_tvb = tvb_new_subset_length(tipc_tvb, offset, msg_in_bundle_size);

				/* the info column shall not be deleted by the
				 * encapsulated messages */
				col_append_str(pinfo->cinfo, COL_INFO, " | ");
				col_set_fence(pinfo->cinfo, COL_INFO);

				dissect_tipc(data_tvb, pinfo, top_tree, NULL);

				/* round up message size to align the messages to 4 Bytes */
				offset += WS_ROUNDUP_4(msg_in_bundle_size);
			}
			break;
		case TIPCv2_LINK_PROTOCOL:
			/* W1 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_mtype, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			/* Sequence Gap:  13 bits. */
			proto_tree_add_item(tipc_tree, hf_tipcv2_sequence_gap, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_broadcast_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W2 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_seq_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W3 */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_prev_node, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;
			/* W4 */
			/* Next Sent Broadcast: 16 bits */
			proto_tree_add_item(tipc_tree, hf_tipcv2_next_sent_broadcast, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			/* Next Sent Packet:  16 bits. */
			proto_tree_add_item(tipc_tree, hf_tipcv2_next_sent_packet, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W5 */
			/* Session Number: 16 bits. */
			proto_tree_add_item(tipc_tree, hf_tipcv2_session_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			/* Reserved: 3 bits Must be set to zero. */
			/* the following two fields appear in this user according to */
			/* Jon Maloy on the tipc-discussion mailing list */
			/* Redundant Link: 1 bit */
			proto_tree_add_item(tipc_tree, hf_tipcv2_redundant_link, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			/* Bearer Identity: 3 bits */
			proto_tree_add_item(tipc_tree, hf_tipcv2_bearer_id, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			/* Link Priority: 5 bits. */
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_prio, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			/* Network Plane: 3 bits. */
			proto_tree_add_item(tipc_tree, hf_tipcv2_network_plane, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			/* Probe: 1 bit. */
			proto_tree_add_item(tipc_tree, hf_tipcv2_probe, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			if (handle_v2_as & (V2_AS_1_6)) {
				/* W6 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 6 unused for this user");
				offset = offset + 4;
				/* W7 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 7 unused for this user");
				offset = offset + 4;
				/* W8 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 8 unused for this user");
				offset = offset + 4;
			} else {
				/* W6 */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_orig_node, tipc_tvb, offset, 4, addr_str_ptr);
				offset = offset + 4;
				/* W7 */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_dest_node, tipc_tvb, offset, 4, addr_str_ptr);
				offset = offset + 4;
				/* W8 */
				proto_tree_add_item(tipc_tree, hf_tipcv2_timestamp, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset = offset + 4;
			}
			/* W9 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_max_packet, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			/* Link Tolerance:  16 bits */
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_tolerance, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;

			if ((message_type == TIPCv2_RESET_MSG)
					|| ((message_type == TIPCv2_STATE_MSG) && ((msg_size-(orig_hdr_size*4)) != 0))){ /* is allowed */
				proto_tree_add_item_ret_length(tipc_tree, hf_tipcv2_bearer_instance, tipc_tvb, offset, -1, ENC_ASCII, &b_inst_strlen);
				offset += b_inst_strlen;
				/* the bearer instance string is padded with \0 to the next word boundary */
				if ((padlen = WS_PADDING_TO_4(b_inst_strlen)) != 0) {
					proto_tree_add_bytes_format_value(tipc_tree, hf_tipcv2_padding, tipc_tvb, offset, padlen, NULL, "%d byte%c", padlen, (padlen!=1?'s':0));
					offset += padlen;
				}
				/*
				 * If there's any data left, show it as
				 * padding for MTU discovery.
				 */
				if ((uint32_t)offset < msg_size) {
					uint32_t filler_len;

					filler_len = msg_size - (uint32_t)offset;
					proto_tree_add_bytes_format_value(tipc_tree, hf_tipcv2_filler_mtu_discovery, tipc_tvb, offset, filler_len, NULL,
													"%d byte%c", filler_len, (filler_len!=1?'s':0));
				}
			}
			break;
		case TIPCv2_CONN_MANAGER:
			/* CONN_MANAGER uses the 36-byte header format of CONN_MSG payload messages */
			/* W1 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_connmgr_mtype, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_errorcode, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_rer_cnt, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_lookup_scope, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);

			/* Options Position: 3 bits */
			/* this is not used by this user according to Jon Maloy in tipc-discussion mailing list
			   opt_p = tvb_get_uint8(tipc_tvb, offset+1) & 0x7;
			   proto_tree_add_item(tipc_tree, hf_tipcv2_opt_p , tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			   if (opt_p != 0) {
			   hdr_size = hdr_size - (opt_p << 2);
			   }
			   */
			proto_tree_add_item(tipc_tree, hf_tipcv2_broadcast_ack_no , tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;

			/* W2 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_ack_no , tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_seq_no , tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;

			/* W3 */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_prev_node, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;

			/* W4 */
			proto_tree_add_item(tipc_tree, hf_tipc_org_port , tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;

			/* W5 */
			proto_tree_add_item(tipc_tree, hf_tipc_dst_port , tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;

			/* W6 */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_orig_node, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;

			/* W7 */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_dest_node, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;

			/* W8 Unused */
			/* might be set prior to 1.7.3 but according to Allan Stephens this was never verfied by the receiver
			   proto_tree_add_item(tipc_tree, hf_tipcv2_transport_seq_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			   */
			proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 8 unused for this user");
			offset = offset + 4;


			/* this is not used here according to Jon Maloy in tipc-discussion mailing list
			 * Options

			 if (opt_p != 0) {
			 proto_tree_add_subtree(tipc_tree, tipc_tvb, offset, (opt_p >> 2), "Options");
			 offset = offset + (opt_p << 2);
			 }
			 */

			if (message_type == TIPCv2_CONMGR_MSG_ACK || (handle_v2_as & (V2_AS_ALL + V2_AS_1_7)))
			{
				/* W9 */
				proto_tree_add_item(tipc_tree, hf_tipcv2_conn_mgr_msg_ack, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset += 4;
			}
			break;
		case TIPCv2_ROUTE_DISTRIBUTOR:
			/* W1 */
			/* determine if it is TIPC v1.6 or v1.7 */
			dword = tvb_get_ntohl(tipc_tvb, offset+28);
			item_size = (dword >> 24) & 0xff;
			if ((handle_v2_as & V2_AS_1_6) || ((handle_v2_as & V2_AS_ALL) == 0)) {
				proto_tree_add_item(tipc_tree, hf_tipcv2_route_mtype_1_6, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			} else {
				proto_tree_add_item(tipc_tree, hf_tipcv2_route_mtype_1_7, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			}
			proto_tree_add_item(tipc_tree, hf_tipcv2_broadcast_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W2 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_seq_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W3 */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_prev_node, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;
			/* W4 */

			if (handle_v2_as & V2_AS_1_6) {
				/* W4 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 4 unused for this user");
				offset = offset + 4;
				/* W5 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 5 unused for this user");
				offset = offset + 4;
				/* W6 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 6 unused for this user");
				offset = offset + 4;
				/* W7 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 7 unused for this user");
				offset = offset + 4;
				/* W8 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 8 unused for this user");
				offset = offset + 4;
				/* W9 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 9 unused for this user");
				offset = offset + 4;
			} else {
				/* W4 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 4 unused for this user");
				offset = offset + 4;
				/* W5 */
				proto_tree_add_item(tipc_tree, hf_tipc_dst_port, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset = offset + 4;
				/* W6 */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_orig_node, tipc_tvb, offset, 4, addr_str_ptr);
				offset = offset + 4;
				/* W7 */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_dest_node, tipc_tvb, offset, 4, addr_str_ptr);
				offset = offset + 4;
				/* W8 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 8 unused for this user");
				offset = offset + 4;
				/* W9 */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				item_size = (dword >> 24) & 0xff;
				proto_tree_add_item(tipc_tree, hf_tipcv2_item_size, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset = offset + 4;
			}

			/* item_size == 0 indicates that it's TIPC v1.6 style */
			if ((handle_v2_as & V2_AS_1_6) || ((handle_v2_as & V2_AS_ALL) && (item_size == 0))) {
				/* W10 */
				switch (message_type) {
					case TIPCv2_EXT_ROUTING_TABLE:		/* 0  */
					case TIPCv2_LOCAL_ROUTING_TABLE:	/* 1  */
					case TIPCv2_SEC_ROUTING_TABLE:		/* 2  */
						/* Cluster Address */
						dword = tvb_get_ntohl(tipc_tvb, offset);
						addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
						proto_tree_add_string(tipc_tree, hf_tipcv2_cluster_address, tipc_tvb, offset, 4, addr_str_ptr);
						offset = offset + 4;
						/* bitmap */
						proto_tree_add_item(tipc_tree, hf_tipcv2_bitmap, tipc_tvb, offset, -1, ENC_NA);
						break;
					case TIPCv2_ROUTE_ADDITION:			/* 3  */
					case TIPCv2_ROUTE_REMOVAL:			/* 4  */
						/* Node Address */
						dword = tvb_get_ntohl(tipc_tvb, offset);
						addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
						proto_tree_add_string(tipc_tree, hf_tipcv2_node_address, tipc_tvb, offset, 4, addr_str_ptr);
						offset = offset + 4;
					default:
						break;
				}
			} else {
				/* what if item_size is set to a value fitting to TIPC v1.6 ? */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_network_region, tipc_tvb, offset, 4, addr_str_ptr);
				offset += 4;
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_local_router, tipc_tvb, offset, 4, addr_str_ptr);
				offset += 4;
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_remote_router, tipc_tvb, offset, 4, addr_str_ptr);
				offset += 4;
				proto_tree_add_item(tipc_tree, hf_tipcv2_dist_dist, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				proto_tree_add_item(tipc_tree, hf_tipcv2_dist_scope, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset = offset + 4;
			}
			break;

		case TIPCv2_CHANGEOVER_PROTOCOL:
			/* W1 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_changeover_mtype, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_broadcast_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W2 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_seq_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W3 */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_prev_node, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;
			/* W4 Unused */
			proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 4 unused for this user");
			offset = offset + 4;
			/* W5 */
			/* the following two fields appear in this user according to */
			/* Jon Maloy on the tipc-discussion mailing list */
			/* Redundant Link: 1 bit */
			if (handle_v2_as & (V2_AS_1_6)) {
				proto_tree_add_item(tipc_tree, hf_tipcv2_redundant_link, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			}
			/* Bearer Identity: 3 bits */
			proto_tree_add_item(tipc_tree, hf_tipcv2_bearer_id, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W6-W8 */
			if (handle_v2_as & (V2_AS_1_6)) {
				/* W6 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 6 unused for this user");
				offset = offset + 4;
				/* W7 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 7 unused for this user");
				offset = offset + 4;
				/* W8 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 8 unused for this user");
				offset = offset + 4;
			} else {
				/* W6 */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_orig_node, tipc_tvb, offset, 4, addr_str_ptr);
				offset = offset + 4;
				/* W7 */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_dest_node, tipc_tvb, offset, 4, addr_str_ptr);
				offset = offset + 4;
				/* W8 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 8 unused for this user");
				offset = offset + 4;
			}
			/* W9 */
			switch (message_type)
			{
				case 0:
					/* DUPLICATE_MSG */
					/* W9 Unused */
					proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 9 unused for this user");
					break;
				case 1:
					/* ORIGINAL_MSG */
					/* Message Count: 16 bits. */
					proto_tree_add_item(tipc_tree, hf_tipcv2_msg_count, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
					break;
				default:
					break;
			}
			offset = offset + 4;
			break;
		case TIPCv2_NAME_DISTRIBUTOR:
			/* W1 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_naming_mtype, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_broadcast_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W2 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_seq_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W3 */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_prev_node, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;
			/* W4 Unused */
			proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 4 unused for this user");
			offset = offset + 4;
			/* W5 Unused */
			proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 5 unused for this user");
			offset = offset + 4;
			/* W6 */
			/* Originating Node: 32 bits. */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_orig_node, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;
			/* W7 */
			/* Destination Node: 32 bits.  */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_dest_node, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;
			if (handle_v2_as & (V2_AS_1_6 + V2_AS_ALL)) {
				/* W8 */
				/* Transport Level Sequence Number: 32 bits */
				proto_tree_add_item(tipc_tree, hf_tipcv2_transport_seq_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset = offset + 4;
			} else {
				/* W8 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 8 unused for this user");
				offset = offset + 4;
			}
			/* W9 */
			if (handle_v2_as & V2_AS_1_6) {
				/* W9 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 9 unused for this user");
				offset = offset + 4;
			} else {
				dword = tvb_get_ntohl(tipc_tvb, offset);
				item_size = (dword >> 24) & 0xff;
				proto_tree_add_item(tipc_tree, hf_tipcv2_item_size, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset = offset + 4;
			}
			/* W10 */
			/* dissect the (one or more) Publications */
			data_tvb = tvb_new_subset_remaining(tipc_tvb, offset);
			dissect_tipc_name_dist_data(data_tvb, pinfo, tipc_tree, item_size);
			break;
		case TIPCv2_MSG_FRAGMENTER:
			/* W1 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_fragmenter_mtype, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_broadcast_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W2 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_seq_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W3 */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_prev_node, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;
			/* W4 */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			/* Fragment Number: 16 Bits. */
			proto_tree_add_item(tipc_tree, hf_tipcv2_fragment_number, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			frag_no = (dword >> 16) & 0x0000ffff;
			/* Fragment msg Number: 16 bits */
			proto_tree_add_item(tipc_tree, hf_tipcv2_fragment_msg_number, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			frag_msg_no = dword & 0x0000ffff;
			offset = offset + 4;
			if (handle_v2_as & (V2_AS_1_6)) {
				/* W5 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 5 unused for this user");
				offset = offset + 4;
				/* W6 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 6 unused for this user");
				offset = offset + 4;
				/* W7 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 7 unused for this user");
				offset = offset + 4;
				/* W8 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 8 unused for this user");
				offset = offset + 4;
				/* W9 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 9 unused for this user");
				offset = offset + 4;
			} else {
				/* W5 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 5 unused for this user");
				offset = offset + 4;
				/* W6 */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_orig_node, tipc_tvb, offset, 4, addr_str_ptr);
				offset = offset + 4;
				/* W7 */
				dword = tvb_get_ntohl(tipc_tvb, offset);
				addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
				proto_tree_add_string(tipc_tree, hf_tipcv2_dest_node, tipc_tvb, offset, 4, addr_str_ptr);
				offset = offset + 4;
				/* W8 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 8 unused for this user");
				offset = offset + 4;
				/* W9 Unused */
				proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tipc_tvb, offset, 4, "word 9 unused for this user");
				offset = offset + 4;
			}

			len = (msg_size - (orig_hdr_size<<2));
			reported_len = tvb_reported_length_remaining(tipc_tvb, offset);

			if (tipc_defragment) {
				/* reassemble fragmented packages */
				save_fragmented = pinfo->fragmented;
				pinfo->fragmented = true;

				frag_msg = fragment_add_seq_check(&tipc_msg_reassembly_table,
						tipc_tvb, offset,
						pinfo,
						frag_msg_no,					/* ID for fragments belonging together */
						NULL,
						/* TODO: make sure that fragments are on the same LINK */
						/* TIPC starts with "1" but we * need "0" */
						(frag_no-1),					/* number of the fragment */
						len,						/* fragment length - to the end of the data */
						(message_type != TIPCv2_USER_LAST_FRAGMENT));	/* More fragments? */

				new_tvb = process_reassembled_data(tipc_tvb, offset, pinfo,
						"Reassembled TIPC", frag_msg, &tipc_msg_frag_items,
						NULL, tipc_tree);

				if (frag_msg) { /* Reassembled */
					col_append_str(pinfo->cinfo, COL_INFO,
								" (Message Reassembled)");
				} else { /* Not last packet of reassembled Short Message */
					col_append_fstr(pinfo->cinfo, COL_INFO,
							" (Message fragment %u)", frag_no);
				}
				if (new_tvb) { /* take it all */

					/* the info column shall not be deleted by the
					 * encapsulated messages */
					col_append_str(pinfo->cinfo, COL_INFO, " | ");
					col_set_fence(pinfo->cinfo, COL_INFO);
					dissect_tipc(new_tvb, pinfo, top_tree, NULL);
				} else { /* make a new subset */
					data_tvb = tvb_new_subset_length_caplen(tipc_tvb, offset, len, reported_len);
					call_data_dissector(data_tvb, pinfo, top_tree);
				}

				pinfo->fragmented = save_fragmented;
			} else {
				/* don't reassemble is set in the "preferences" */
				data_tvb = tvb_new_subset_length_caplen(tipc_tvb, offset, len, reported_len);
				call_data_dissector(data_tvb, pinfo, top_tree);
			}

			break;
		case TIPCv2_NEIGHBOUR_DISCOVERY:
/*
The protocol for neighbour detection
uses a special message format, with the following generic structure:

0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w0:|vers |msg usr|hdr sz |n|resrv|            packet size          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w1:|m typ|00000|     minor_pv      |        node signature         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w2:|                      destination domain                       |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w3:|                       previous node                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w4:|                      network identity                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w5:|                                                               |
   +-+-+-+-+-+-+-                                    +-+-+-+-+-+-+-+
w6:|                                                               |
   +-+-+-+-+-+-+-  bearer level originating address  +-+-+-+-+-+-+-+
w7:|                                                               |
   +-+-+-+-+-+-+-                                    +-+-+-+-+-+-+-+
w8:|                                                               |
   +-+-+-+-+-+-+-                                    +-+-+-+-+-+-+-+
w9:|                                                               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   \                                                               \
   /                 vendor specific data  (optional)              /
   \                                                               \
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
			/* W1 */
			proto_tree_add_item(tipc_tree, hf_tipcv2_neighbour_mtype, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			/* Reserved 5 bits */
			/* Minor pv 8 bits */
			proto_tree_add_item(tipc_tree, hf_tipcv2_minor_pv, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			proto_tree_add_item(tipc_tree, hf_tipcv2_node_sig, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			/* W2 */
			/* Destination Domain */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_destination_domain, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;
			/* W3 */
			dword = tvb_get_ntohl(tipc_tvb, offset);
			addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
			proto_tree_add_string(tipc_tree, hf_tipcv2_prev_node, tipc_tvb, offset, 4, addr_str_ptr);
			offset = offset + 4;
			/* W4 */
			/* Network Identity: */
			proto_tree_add_item(tipc_tree, hf_tipcv2_network_id, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			offset = offset + 4;
			if (handle_v2_as & (V2_AS_1_6)) {
				/* W5 - W9 Bearer Level Originating Address: */
				proto_tree_add_item(tipc_tree, hf_tipcv2_bearer_level_orig_addr, tipc_tvb, offset, 20, ENC_NA);
				offset = offset + 20;
			} else {
				/* W5 */
				proto_tree_add_item(tipc_tree, hf_tipcv2_media_id, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset = offset + 4;
				/* W6 - W9 Bearer Level Originating Address: */
				proto_tree_add_item(tipc_tree, hf_tipcv2_bearer_level_orig_addr, tipc_tvb, offset, 16, ENC_NA);
				offset = offset + 16;
			}
			if (msg_size-(orig_hdr_size*4) != 0) {
				proto_tree_add_item(tipc_tree, hf_tipcv2_vendor_specific_data, tipc_tvb, offset, -1, ENC_NA);
			}
			break;
		default:
			break;
	}

	return offset;

}

/* Version 2 Header
http://tipc.sourceforge.net/doc/draft-spec-tipc-02.html#sec:TIPC_Pkt_Format
3.1.1. Payload Message Header Format

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w0:|vers | user  |hdr sz |n|d|s|r|          message size           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w1:|mstyp| error |rer cnt|lsc|opt p|      broadcast ack no         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w2:|        link level ack no      |   broadcast/link level seq no |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w3:|                       previous node                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w4:|                      originating port                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w5:|                      destination port                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w6:|                      originating node                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w7:|                      destination node                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w8:|             name type / transport sequence number             |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
w9:|              name instance/multicast lower bound              |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
wA:|                    multicast upper bound                      |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   /                                                               /
   \                           options                             \
   /                                                               /
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

/* this function tries to call subdissectors for encapsulated data
 * @name_type pointer to the used port name type, NULL if not available
 * @user      uint8_t holding the used TIPC user, is allways available
 */
static void
call_tipc_v2_data_subdissectors(tvbuff_t *data_tvb, packet_info *pinfo, uint32_t *name_type_p, uint8_t user)
{
	if (dissect_tipc_data) {
		heur_dtbl_entry_t *hdtbl_entry;
		/* dissection of TIPC data is set in preferences */

		/* check for heuristic dissectors if specified in the
		 * preferences to try them first */
		if (try_heuristic_first) {
			if (dissector_try_heuristic(tipc_heur_subdissector_list, data_tvb, pinfo, top_tree, &hdtbl_entry, NULL))
				return;
		}
		/* This triggers if a dissectors if
		 * tipc.user is just set to a TIPC user holding data */
		if (dissector_try_uint(tipc_user_dissector, user, data_tvb, pinfo, top_tree))
			return;
		/* The Name Type is not always explicitly set in a TIPC Data
		 * Message.
		 *
		 * On the tipc-discussion mailing list, Allan Stephens described
		 * where the Port Name is not set with the following words:
		 *
		 * <cite>
		 * The "named" and "mcast" message types have info in the TIPC header to
		 * specify the message's destination (a port name and port name sequence,
		 * respectively); these message types typically occur when an application
		 * sends connectionless traffic.  The "conn" type is used to carry
		 * connection-oriented traffic over an already established connection;
		 * since the sending socket/port already knows the port ID of the other end
		 * of the connection, there is no need for any port name information to be
		 * present in the TIPC header.
		 *
		 * The "direct" type is used to carry connectionless traffic to a
		 * destination that was specified using a port ID, rather than a port name;
		 * again, no port name info is present in the TIPC header because it is not
		 * required.  Situations where this sort of message might be generated
		 * include: a) an application obtains a port ID as part of a subscription
		 * event generated by TIPC's topology server and then sends a message to
		 * that port ID (using sendto() or sendmsg()), and b) a server obtains a
		 * client's port ID when it receives a message from the client (using
		 * recvfrom() or recvmsg()) and then sends a reply back to that client port
		 * ID (using sendto() or sendmsg()).
		 * </cite>
		 *
		 * TODO: it should be determined by
		 * some kind of static function which port name type a message
		 * is going to, if it is not specified explicitly in a message */
		if (name_type_p)
			if (dissector_try_uint(tipc_type_dissector, *name_type_p, data_tvb, pinfo, top_tree))
				return;
		/* check for heuristic dissectors if specified in the
		 * preferences not to try them first */
		if (!try_heuristic_first) {
			if (dissector_try_heuristic(tipc_heur_subdissector_list, data_tvb, pinfo, top_tree, &hdtbl_entry, NULL))
				return;
		}
	}

	/* dissection of TIPC data is not set in preferences or no subdissector found */

	call_data_dissector(data_tvb, pinfo, top_tree);
}


static void
// NOLINTNEXTLINE(misc-no-recursion)
dissect_tipc_v2(tvbuff_t *tipc_tvb, proto_tree *tipc_tree, packet_info *pinfo, int offset, uint8_t user, uint32_t msg_size, uint8_t hdr_size, bool datatype_hdr)
{
	uint32_t dword;
	char *addr_str_ptr;
	uint8_t opt_p = 0;
	proto_item *item;
	/* The unit used is 32 bit words */
	uint8_t orig_hdr_size;

	uint32_t name_type = 0;
	uint32_t *name_type_p = NULL;
	tvbuff_t *data_tvb;
	int len, reported_len;

	orig_hdr_size = hdr_size;

	/* Word 0 */
	/* Version: 3 bits */
	proto_tree_add_item(tipc_tree, hf_tipc_ver, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	/* User: 4 bits */
	proto_tree_add_item(tipc_tree, hf_tipcv2_usr, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	/* Header Size: 4 bits */
	item = proto_tree_add_item(tipc_tree, hf_tipc_hdr_size, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_item_append_text(item, " = %u bytes", (hdr_size * 4));
	/* Non-sequenced: 1 bit */
	proto_tree_add_item(tipc_tree, hf_tipc_nonsequenced, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	if (datatype_hdr) {
		/* Destination Droppable: 1 bit */
		proto_tree_add_item(tipc_tree, hf_tipc_destdrop, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
		/* Source Droppable: 1 bit */
		proto_tree_add_item(tipc_tree, hf_tipcv2_srcdrop, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
		/* SYN: 1 bit */
		proto_tree_add_item(tipc_tree, hf_tipcv2_syn, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	}
	/* Reserved: 1 bits */

	/* Message Size: 17 bits */
	proto_tree_add_item(tipc_tree, hf_tipc_msg_size, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	offset = offset + 4;

	if (!datatype_hdr) {
		dissect_tipc_v2_internal_msg(tipc_tvb, tipc_tree, pinfo, offset, user, msg_size, orig_hdr_size);
		return;
	}

	/* Word 1 */
	/* Message Type: 3 bits */
	proto_tree_add_item(tipc_tree, hf_tipcv2_data_msg_type, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	/* Error Code: 4 bits */
	proto_tree_add_item(tipc_tree, hf_tipcv2_errorcode, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);

	/* Reroute Counter: 4 bits */
	proto_tree_add_item(tipc_tree, hf_tipcv2_rer_cnt, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	/* Lookup Scope: 2 bits */
	proto_tree_add_item(tipc_tree, hf_tipcv2_lookup_scope, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);

	/* Options Position: 3 bits */
	if (handle_v2_as & (V2_AS_ALL + V2_AS_1_6)) {
		opt_p = tvb_get_uint8(tipc_tvb, offset+1) & 0x7;
		proto_tree_add_item(tipc_tree, hf_tipcv2_opt_p, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
		if (opt_p != 0) {
			hdr_size = hdr_size - (opt_p << 2);
		}
	}
	/* Broadcast Acknowledge Number: 16 bits */
	proto_tree_add_item(tipc_tree, hf_tipcv2_broadcast_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	offset = offset + 4;

	/* W2 */
	/* Link Level Acknowledge Number: 16 bits */
	proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_ack_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	/* broadcast/link level seq no */
	proto_tree_add_item(tipc_tree, hf_tipcv2_link_level_seq_no, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	offset = offset + 4;
	/* W3 previous node */
	dword = tvb_get_ntohl(tipc_tvb, offset);
	addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
	proto_tree_add_string(tipc_tree, hf_tipcv2_prev_node, tipc_tvb, offset, 4, addr_str_ptr);
	offset = offset + 4;

	/* W4 Originating Port: 32 bits */
	proto_tree_add_item(tipc_tree, hf_tipc_org_port, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	offset = offset + 4;

	/* W5 Destination Port: 32 bits */
	proto_tree_add_item(tipc_tree, hf_tipc_dst_port, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	offset = offset + 4;
	if (hdr_size > 6) {

		/* W6 Originating Node: 32 bits */
		dword = tvb_get_ntohl(tipc_tvb, offset);
		addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
		proto_tree_add_string(tipc_tree, hf_tipcv2_orig_node, tipc_tvb, offset, 4, addr_str_ptr);
		offset = offset + 4;
		/* W7 Destination Node: 32 bits */
		dword = tvb_get_ntohl(tipc_tvb, offset);
		addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
		proto_tree_add_string(tipc_tree, hf_tipcv2_dest_node, tipc_tvb, offset, 4, addr_str_ptr);
		offset = offset + 4;
		if (hdr_size > 8) {
			/* W8 name type / transport sequence number */
			/* Transport Level Sequence Number: 32 bits */
			/* Port Name Type: 32 bits */
			proto_tree_add_item(tipc_tree, hf_tipcv2_port_name_type, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
			name_type = tvb_get_ntohl(tipc_tvb, offset);
			name_type_p = &name_type;
			offset = offset + 4;

			if (hdr_size > 9) {
				/* W9 name instance/multicast lower bound  */
				if (hdr_size < 11)
					/* no multicast */
					/* Port Name Instance: 32 bits */
					proto_tree_add_item(tipc_tree, hf_tipcv2_port_name_instance, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				else
					/* multicast */
					/* Port Name Sequence Lower: 32 bits */
					proto_tree_add_item(tipc_tree, hf_tipcv2_multicast_lower, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset = offset + 4;
				if (hdr_size > 10) {

					/* W10 multicast upper bound */
					/* Port Name Sequence Upper: 32 bits */
					proto_tree_add_item(tipc_tree, hf_tipcv2_multicast_upper, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
					offset = offset + 4;
				}
			}
		}
	}
	/* Options */
	if (handle_v2_as & (V2_AS_ALL + V2_AS_1_6)) {
		if (opt_p != 0) {
			proto_tree_add_bytes_format(tipc_tree, hf_tipcv2_options, tipc_tvb, offset, (opt_p >> 2), NULL, "Options");
			offset = offset + (opt_p << 2);
		}
	}
	/* TIPCv2 data */
	len = (msg_size - (orig_hdr_size<<2));
	reported_len = tvb_reported_length_remaining(tipc_tvb, offset);
	data_tvb = tvb_new_subset_length_caplen(tipc_tvb, offset, len, reported_len);

	call_tipc_v2_data_subdissectors(data_tvb, pinfo, name_type_p, user);
}

/*  From message.h (http://cvs.sourceforge.net/viewcvs.py/tipc/source/stable_ericsson/TIPC_SCC/src/Message.h?rev=1.2&view=markup)
////////////////////////////////////////////////////////////////////
TIPC internal header format, version 1:

   :                                                               :
   |                 Word 0-2: common to all users                 |
   |                                                               |
   +-------+-------+-------+-------+-------+-------+-------+-------+
   |netw-|imp|link |                               | |p|bea- |link |
w3:|ork  |ort|sel- |        message count          | |r|rer  |sel- |
   |id   |anc|ector|                               | |b|id   |ector|
   +-------+-------+-------+-------+-------+-------+-------+-------+
   |                                                               |
w4:|                        remote address                         |
   |                                                               |
   +-------+-------+-------+-------+-------+-------+-------+-------+
   | msg   |                       |                               |
w5:| type  |           gap         |           next sent           |
   |       |                       |                               |
   +-------+-------+-------+-------+-------+-------+-------+-------+
   |                       | link    |                             |
w6:|        reserve        | prio-   |        link tolerance       |
   |                       | ity     |                             |
   +-------+-------+-------+-------+-------+-------+-------+-------+
   |                                                               |
w7:|                                                               |
   |                                                               |
   +-------+-------+                               +-------+-------+
   |                                                               |
w8:|                                                               |
   |                                                               |
   +-------+-------+       bearer name             +-------+-------+
   |                                                               |
w9:|                                                               |
   |                                                               |
   +-------+-------+                               +-------+-------+
   |                                                               |
wa:|                                                               |
   |                                                               |
   +-------+-------+-------+-------+-------+-------+-------+-------+

NB: Connection Manager and Name Distributor use data message format.
*/

static void
// NOLINTNEXTLINE(misc-no-recursion)
dissect_tipc_int_prot_msg(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tipc_tree, int offset, uint8_t user, uint32_t msg_size)
{
	uint8_t msg_type;
	tvbuff_t *data_tvb;
	uint32_t msg_in_bundle_size;
	uint32_t dword;
	unsigned msg_no = 0;
	uint8_t link_sel;
	uint16_t link_lev_seq_no;
	uint32_t reassembled_msg_length = 0;
	uint32_t no_of_segments = 0;

	bool save_fragmented;
	tvbuff_t* new_tvb = NULL;
	tvbuff_t* next_tvb = NULL;
	fragment_head *frag_msg = NULL;
	proto_item *item;

	link_lev_seq_no = tvb_get_ntohl(tvb, 4) & 0xffff;
	/* Internal Protocol Header */
	/* Unused */

	msg_type = tvb_get_uint8(tvb, 20)>>4;
	/* W3 */
	dword = tvb_get_ntohl(tvb, offset);
	link_sel = dword & 0x7;
	proto_tree_add_item(tipc_tree, hf_tipc_unused2, tvb, offset, 4, ENC_BIG_ENDIAN);
	/* Importance */
	if (user == TIPC_SEGMENTATION_MANAGER)
		proto_tree_add_item(tipc_tree, hf_tipc_importance, tvb, offset, 4, ENC_BIG_ENDIAN);
	/* Link selector */
	if (user == TIPC_SEGMENTATION_MANAGER || user == TIPC_NAME_DISTRIBUTOR || user == TIPC_CHANGEOVER_PROTOCOL)
		proto_tree_add_item(tipc_tree, hf_tipc_link_selector, tvb, offset, 4, ENC_BIG_ENDIAN);
	/* Message count */
	if (user == TIPC_MSG_BUNDLER || user == TIPC_CHANGEOVER_PROTOCOL) {
		proto_tree_add_item(tipc_tree, hf_tipc_msg_cnt, tvb, offset, 4, ENC_BIG_ENDIAN);
	}
	/* Unused */
	/* Probe */
	if (user == TIPC_LINK_PROTOCOL)
		proto_tree_add_item(tipc_tree, hf_tipc_probe, tvb, offset, 4, ENC_BIG_ENDIAN);
	/* Bearer identity */
	if (user == TIPC_LINK_PROTOCOL || user == TIPC_CHANGEOVER_PROTOCOL)
		proto_tree_add_item(tipc_tree, hf_tipc_bearer_id, tvb, offset, 4, ENC_BIG_ENDIAN);
	/* Link selector */
	if (user == TIPC_SEGMENTATION_MANAGER || user == TIPC_NAME_DISTRIBUTOR || user == TIPC_CHANGEOVER_PROTOCOL)
		proto_tree_add_item(tipc_tree, hf_tipc_link_selector2, tvb, offset, 4, ENC_BIG_ENDIAN);

	offset = offset + 4;

	/* W4 */
	/* Remote address */
	if (user == TIPC_ROUTING_MANAGER)
		proto_tree_add_item(tipc_tree, hf_tipc_remote_addr, tvb, offset, 4, ENC_BIG_ENDIAN);
	offset = offset + 4;

	/* W5 */
	/* Message type */
	switch (user) {
		case TIPC_ROUTING_MANAGER:
			proto_tree_add_item(tipc_tree, hf_tipc_rm_msg_type, tvb, offset, 4, ENC_BIG_ENDIAN);
			break;
		case TIPC_NAME_DISTRIBUTOR:
			proto_tree_add_item(tipc_tree, hf_tipc_nd_msg_type, tvb, offset, 4, ENC_BIG_ENDIAN);
			break;
		case TIPC_CONNECTION_MANAGER:
			break;
		case TIPC_LINK_PROTOCOL:
			proto_tree_add_item(tipc_tree, hf_tipc_lp_msg_type, tvb, offset, 4, ENC_BIG_ENDIAN);
			break;
		case TIPC_CHANGEOVER_PROTOCOL:
			proto_tree_add_item(tipc_tree, hf_tipc_cng_prot_msg_type, tvb, offset, 4, ENC_BIG_ENDIAN);
			break;
		case TIPC_SEGMENTATION_MANAGER:
			proto_tree_add_item(tipc_tree, hf_tipc_sm_msg_type, tvb, offset, 4, ENC_BIG_ENDIAN);
			break;
		default:
			proto_tree_add_item(tipc_tree, hf_tipc_unknown_msg_type, tvb, offset, 4, ENC_BIG_ENDIAN);
			break;
	}
	/* Sequence gap */
	if (user == TIPC_LINK_PROTOCOL && msg_type == TIPC_LINK_PROTOCO_STATE_MSG)
		proto_tree_add_item(tipc_tree, hf_tipc_seq_gap, tvb, offset, 4, ENC_BIG_ENDIAN);
	/* Next sent packet */
	proto_tree_add_item(tipc_tree, hf_tipc_nxt_snt_pkg, tvb, offset, 4, ENC_BIG_ENDIAN);

	offset = offset + 4;
	/* W6 Unused */
	proto_tree_add_none_format(tipc_tree, hf_tipc_unused_word, tvb, offset, 4, "word 6 unused for this user");
	offset = offset + 4;
	/* W7 */
	if (msg_size == 28) /* No data */
		return;

	switch (user) {
		case TIPC_LINK_PROTOCOL:
			proto_tree_add_item(tipc_tree, hf_tipc_bearer_name, tvb, offset, -1, ENC_ASCII);
			break;
		case TIPC_CHANGEOVER_PROTOCOL:
			switch (msg_type) {
				case 0: /* DUPLICATE_MSG */
				case 1: /* ORIGINAL_MSG */
					item = proto_tree_add_uint_format(tipc_tree, hf_tipc_changeover_protocol, tvb, offset, 1,
							msg_type, "TIPC_CHANGEOVER_PROTOCOL %s (%u)",
							val_to_str_const(msg_type, tipc_cng_prot_msg_type_values, "unknown"), msg_type);
                    proto_item_set_len(item, tvb_reported_length_remaining(tvb, offset));
					data_tvb = tvb_new_subset_remaining(tvb, offset);
					col_set_fence(pinfo->cinfo, COL_INFO);
					dissect_tipc(data_tvb, pinfo, tipc_tree, NULL);
					break;
				default:
					/* INFO_MSG: Even when there are no packets in the send queue of a removed link, the other
					 * endpoint must be informed about this fact, so it can be unblocked when it has terminated its
					 * part of the changeover procedure. This message type may be regarded as an empty
					 * ORIGINAL_MSG, where message count is zero, and no packet is wrapped inside.
					 */
					item = proto_tree_add_uint_format(tipc_tree, hf_tipc_changeover_protocol, tvb, offset, 1, msg_type,
							"TIPC_CHANGEOVER_PROTOCOL Protocol/dissection Error");
                    proto_item_set_len(item, tvb_reported_length_remaining(tvb, offset));
					break;
			}
			break;
		case TIPC_SEGMENTATION_MANAGER:
			save_fragmented = pinfo->fragmented;
			if (tipc_defragment) {
				pinfo->fragmented = true;

				frag_msg = fragment_add_seq_next(&tipc_msg_reassembly_table,
						tvb, offset,
						pinfo,
						link_sel,				/* ID for fragments belonging together - NEEDS IMPROVING? */
						NULL,
						tvb_captured_length_remaining(tvb, offset),	/* fragment length - to the end */
						true);					/* More fragments? */
				if (msg_type == TIPC_FIRST_SEGMENT) {
					reassembled_msg_length = tvb_get_ntohl(tvb, offset) & 0x1ffff;
					/* The number of segments needed for he complete message (Including header) will be
					 * The size of the data section of the first message, divided by the complete message size
					 * + one segment for the remainder (if any).
					 */
					no_of_segments = reassembled_msg_length/(msg_size - 28);
					if (reassembled_msg_length > (no_of_segments * (msg_size - 28)))
						no_of_segments++;
					fragment_set_tot_len(&tipc_msg_reassembly_table,
						pinfo, link_sel, NULL,
						no_of_segments-1);
					item = proto_tree_add_bytes_format(tipc_tree, hf_tipc_data_fragment, tvb, offset, -1, NULL, "Segmented message size %u bytes -> No segments = %i",
							reassembled_msg_length, no_of_segments);
					proto_item_set_generated(item);
				}

				new_tvb = process_reassembled_data(tvb, offset, pinfo,
						"Reassembled TIPC", frag_msg, &tipc_msg_frag_items,
						NULL, tipc_tree);

				if (frag_msg) { /* Reassembled */
					col_append_str(pinfo->cinfo, COL_INFO,
								" (Message Reassembled)");
				} else { /* Not last packet of reassembled Short Message */
					col_append_fstr(pinfo->cinfo, COL_INFO,
							" (Message fragment %u)", link_lev_seq_no);
				}
			}

			if (new_tvb) { /* take it all */
				next_tvb = new_tvb;
			} else { /* make a new subset */
				next_tvb = tvb_new_subset_remaining(tvb, offset);
			}
			pinfo->fragmented = save_fragmented;
			if (new_tvb) {
				col_set_fence(pinfo->cinfo, COL_INFO);
				dissect_tipc(next_tvb, pinfo, tipc_tree, NULL);
				return;
			}

			proto_tree_add_bytes_format(tipc_tree, hf_tipc_data_fragment, next_tvb, 0, -1, NULL, "%u bytes Data Fragment", (msg_size - 28));
			break;
		case TIPC_MSG_BUNDLER:
			proto_tree_add_item(tipc_tree, hf_tipc_message_bundle, tvb, offset, -1, ENC_NA);
			while ((uint32_t)offset < msg_size) {
				msg_no++;
				msg_in_bundle_size = tvb_get_ntohl(tvb, offset) & 0x1FFFF;
				item = proto_tree_add_uint_format(tipc_tree, hf_tipc_msg_no_bundle, tvb, offset, 1, msg_no, "%u Message in Bundle", msg_no);
				int remaining = tvb_reported_length_remaining(tvb, offset);
				if (remaining > 0 && msg_in_bundle_size <= (unsigned)remaining) {
					proto_item_set_len(item, msg_in_bundle_size);
					data_tvb = tvb_new_subset_length(tvb, offset, msg_in_bundle_size);
					col_set_fence(pinfo->cinfo, COL_INFO);
					dissect_tipc(data_tvb, pinfo, tipc_tree, NULL);
					offset += msg_in_bundle_size;
				} else {
					proto_tree_add_expert(tipc_tree, pinfo, &ei_tipc_invalid_bundle_size, tvb, offset, 4);
					break;
				}
			}
			break;
		default:
			proto_tree_add_item(tipc_tree, hf_tipc_data, tvb, offset, -1, ENC_NA);
			break;
	}
}


/* determines the length of a TIPC package */
static unsigned
get_tipc_pdu_len(packet_info *pinfo _U_, tvbuff_t *tvb, int offset, void *data _U_)
{
	return tvb_get_ntohl(tvb, offset) & 0x0001FFFF;
}


/* triggers the dissection of TIPC-over-TCP */
static int
dissect_tipc_tcp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *parent_tree, void *data)
{
	tcp_dissect_pdus(tvb, pinfo, parent_tree, tipc_tcp_desegment, 4, get_tipc_pdu_len,
			dissect_tipc, data);
	return tvb_captured_length(tvb);
}

#define TIPC_MAX_RECURSION_DEPTH 10 // Arbitrary
static int
// NOLINTNEXTLINE(misc-no-recursion)
dissect_tipc(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void* data _U_)
{
	proto_item *ti, *item;
	proto_tree *tipc_tree, *tipc_data_tree;
	int offset = 0;
	uint32_t srcport, destport = 0, dword;
	uint8_t version;
	uint32_t msg_size;
	uint8_t hdr_size;
	uint8_t user;
	char   *addr_str_ptr;
	tvbuff_t *data_tvb, *tipc_tvb;
	bool datatype_hdr = false;
	uint8_t  msg_type = 0;

	/* Make entry in Protocol column on summary display */
	col_set_str(pinfo->cinfo, COL_PROTOCOL, "TIPC");

	col_clear(pinfo->cinfo, COL_INFO);

	top_tree = tree;
	dword = tvb_get_ntohl(tvb, offset);
	version = (dword >>29) & 0xf;
	hdr_size = (dword >>21) & 0xf;
	user = (dword>>25) & 0xf;
	msg_size = dword & 0x1ffff;

	unsigned recursion_depth = p_get_proto_depth(pinfo, proto_tipc);
	if (++recursion_depth >= TIPC_MAX_RECURSION_DEPTH) {
		proto_tree_add_expert(tree, pinfo, &ei_tipc_max_recursion_depth_reached, tvb, 0, 0);
		return tvb_captured_length(tvb);
	}
	p_set_proto_depth(pinfo, proto_tipc, recursion_depth);

	if ((uint32_t)tvb_reported_length_remaining(tvb, offset) < msg_size) {
		tipc_tvb = tvb;
	} else {
		tipc_tvb = tvb_new_subset_length(tvb, offset, msg_size);
	}
	/* user == 7 only works for v2, this will decode the legacy TIPC configuration protocol */
	if (user == TIPCv2_LINK_PROTOCOL) version = TIPCv2;
	/* Set User values in COL INFO different in V1 and V2 */
	switch (version) {
		case 0:
		case TIPCv1:
			msg_type = tvb_get_uint8(tipc_tvb, offset + 20)>>4;
			col_append_fstr(pinfo->cinfo, COL_INFO, " %s(%u) ", val_to_str_const(user, tipc_user_values, "unknown"), user);
			/* Set msg type in info col and find out if it's a data hdr or not */
			datatype_hdr = tipc_v1_set_col_msgtype(pinfo, user, msg_type);
			if (datatype_hdr) {
				/* Data type header */
				if (hdr_size > 5 && user <4) {
					/* W6 Originating Processor */
					set_address_tvb(&pinfo->src, tipc_address_type, 4, tipc_tvb, offset + 24);

					/* W7 Destination Processor */
					set_address_tvb(&pinfo->dst, tipc_address_type, 4, tipc_tvb, offset + 28);
				} else {
					/* Short data hdr */
					/* W2 Previous Processor */
					set_address_tvb(&pinfo->src, tipc_address_type, 4, tipc_tvb, offset + 8);
				}
			} else {
				/* W2 Previous Processor */
				set_address_tvb(&pinfo->src, tipc_address_type, 4, tipc_tvb, offset + 8);
			}
			break;
		case TIPCv2:
			msg_type = tvb_get_uint8(tipc_tvb, offset + 4)>>5;
			col_append_fstr(pinfo->cinfo, COL_INFO, "%-12s", val_to_str_const(user, tipcv2_user_short_str_vals, "unknown"));
			/* Set msg type in info col */
			tipc_v2_set_info_col(tvb, pinfo, user, msg_type, hdr_size);

			/* find out if it's a data hdr or not */
			switch (user) {
				case TIPCv2_DATA_LOW:
				case TIPCv2_DATA_NORMAL:
				case TIPCv2_DATA_HIGH:
				case TIPCv2_DATA_NON_REJECTABLE:
					datatype_hdr = true;
					break;
				default:
					datatype_hdr = false;
					break;
			}

			if (datatype_hdr) {
				if (hdr_size > 6) {
					/* W6 Originating Processor */
					set_address_tvb(&pinfo->src, tipc_address_type, 4, tipc_tvb, offset + 24);

					/* W7 Destination Processor */
					set_address_tvb(&pinfo->dst, tipc_address_type, 4, tipc_tvb, offset + 28);
				} else {
					/* W3 Previous Processor */
					set_address_tvb(&pinfo->src, tipc_address_type, 4, tipc_tvb, offset + 12);
				}

			} else {
				if (user != TIPCv2_NEIGHBOUR_DISCOVERY) {
					/* W6 Originating Processor */
					set_address_tvb(&pinfo->src, tipc_address_type, 4, tipc_tvb, offset + 24);

					/* W7 Destination Processor */
					set_address_tvb(&pinfo->dst, tipc_address_type, 4, tipc_tvb, offset + 28);
				} else {
					/* W2 Destination Domain */
					set_address_tvb(&pinfo->dst, tipc_address_type, 4, tipc_tvb, offset + 8);

					/* W3 Previous Node */
					set_address_tvb(&pinfo->src, tipc_address_type, 4, tipc_tvb, offset + 12);
				}
			}
			break;
		default:
			break;
	}

	ti = proto_tree_add_item(tree, proto_tipc, tipc_tvb, offset, -1, ENC_NA);
	tipc_tree = proto_item_add_subtree(ti, ett_tipc);
	if (version == TIPCv2) {
		dissect_tipc_v2(tipc_tvb, tipc_tree, pinfo, offset, user, msg_size, hdr_size, datatype_hdr);
		p_set_proto_depth(pinfo, proto_tipc, recursion_depth - 1);
		return tvb_captured_length(tvb);
	}

	/* Word 0-2 common for all messages */
	/* Word 0 */
	proto_tree_add_item(tipc_tree, hf_tipc_ver, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(tipc_tree, hf_tipc_usr, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	item = proto_tree_add_item(tipc_tree, hf_tipc_hdr_size, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_item_append_text(item, " = %u bytes", (hdr_size * 4));
	proto_tree_add_item(tipc_tree, hf_tipc_nonsequenced, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(tipc_tree, hf_tipc_unused, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	if (datatype_hdr) {
		proto_tree_add_item(tipc_tree, hf_tipc_destdrop, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
		proto_tree_add_item(tipc_tree, hf_tipcv2_srcdrop, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	}

	proto_tree_add_item(tipc_tree, hf_tipc_msg_size, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	offset = offset + 4;

	/* Word 1 */
	proto_tree_add_item(tipc_tree, hf_tipc_ack_link_lev_seq, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(tipc_tree, hf_tipc_link_lev_seq, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
	offset = offset + 4;

	/* Word 2 */
	dword = tvb_get_ntohl(tipc_tvb, offset);
	addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);
	proto_tree_add_string(tipc_tree, hf_tipc_prev_proc, tipc_tvb, offset, 4, addr_str_ptr);

	offset = offset + 4;
	switch (user) {
		case TIPC_ROUTING_MANAGER:
		case TIPC_LINK_PROTOCOL:
		case TIPC_CHANGEOVER_PROTOCOL:
		case TIPC_SEGMENTATION_MANAGER:
		case TIPC_MSG_BUNDLER:
			dissect_tipc_int_prot_msg(tipc_tvb, pinfo, tipc_tree, offset, user, msg_size);
			p_set_proto_depth(pinfo, proto_tipc, recursion_depth - 1);
			return tvb_captured_length(tvb);
		default:
			break;
	}

	proto_tree_add_item_ret_uint(tipc_tree, hf_tipc_org_port, tipc_tvb, offset, 4, ENC_BIG_ENDIAN, &srcport);
	offset = offset + 4;
	if (user != TIPC_NAME_DISTRIBUTOR) {
		proto_tree_add_item_ret_uint(tipc_tree, hf_tipc_dst_port, tipc_tvb, offset, 4, ENC_BIG_ENDIAN, &destport);
	}

	conversation_set_conv_addr_port_endpoints(pinfo, &pinfo->src, &pinfo->dst, CONVERSATION_TIPC, srcport, destport);

	offset = offset + 4;
	/* 20 - 24 Bytes
	   20 bytes: Used in subnetwork local, connection oriented messages, where error code, reroute
	   counter and activity identity are zero. A recipient finding that the header size field is 20 does
	   by default know both user (DATA), message type (CONNECTED_MSG), error code
	   (MSG_OK), reroute counter (0), and activity identity (undefined). Since no more testing for
	   this is needed these fields can be left out in the header. Furthermore, since such messages
	   only will do zero or one inter-processor hop, we know that previous processor is the real
	   origin of the message. Hence the field originating processor can be omitted. For the same
	   reason, the recipient processor will know that it is identical to destination processor, so even
	   this field can be skipped. Finally, because the link layer guarantees delivery and sequence
	   order for this single hop, even the connection sequence number is redundant. So the message
	   can just be passed directly on to the destination port. Since this type of message statistically
	   should be by far the most frequent one this small optimization pays off.
	   */
	if (hdr_size <= 6) {
		proto_tree_add_item(tipc_tree, hf_tipc_data, tipc_tvb, offset, -1, ENC_NA);
	} else {
		switch (user) {
			case TIPC_NAME_DISTRIBUTOR:
				proto_tree_add_item(tipc_tree, hf_tipc_nd_msg_type, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				break;
			case TIPC_CONNECTION_MANAGER:
				proto_tree_add_item(tipc_tree, hf_tipc_cm_msg_type, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				break;
			default:
				proto_tree_add_item(tipc_tree, hf_tipc_data_msg_type, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				break;
		}
		proto_tree_add_item(tipc_tree, hf_tipc_err_code, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
		proto_tree_add_item(tipc_tree, hf_tipc_reroute_cnt, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
		proto_tree_add_item(tipc_tree, hf_tipc_act_id, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
		offset = offset + 4;

		dword = tvb_get_ntohl(tipc_tvb, offset);
		addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);

		proto_tree_add_string(tipc_tree, hf_tipc_org_proc, tipc_tvb, offset, 4, addr_str_ptr);
		offset = offset + 4;

		dword = tvb_get_ntohl(tipc_tvb, offset);
		addr_str_ptr = tipc_addr_to_str(pinfo->pool, dword);

		proto_tree_add_string(tipc_tree, hf_tipc_dst_proc, tipc_tvb, offset, 4, addr_str_ptr);
		offset = offset + 4;
		/* 32 bytes
		   32 bytes: The size of all data messages containing an explicit port identity as destination
		   address.
		   */
		if (hdr_size > 8) {
			if (user == TIPC_NAME_DISTRIBUTOR) {
				/*
				   Although an internal service, the name distributor uses the full 40-byte "external" data header
				   format when updating the naming table instances. This is because its messages may need
				   routing, - all system processor must contain the publications from all device processors and
				   vice versa, whether they are directly linked or not. The fields name type, name instance, and
				   destination port of that header have no meaning for such messages
				   */
				offset = offset + 8;
				tipc_data_tree = proto_tree_add_subtree_format(tipc_tree, tvb, offset, -1, ett_tipc_data, NULL,
													"TIPC_NAME_DISTRIBUTOR %u bytes User Data", (msg_size - hdr_size*4));
				data_tvb = tvb_new_subset_remaining(tipc_tvb, offset);
				dissect_tipc_name_dist_data(data_tvb, pinfo, tipc_data_tree, 0);
				p_set_proto_depth(pinfo, proto_tipc, recursion_depth - 1);
				return tvb_captured_length(tvb);
			} else {
				/* Port name type / Connection level sequence number */
				proto_tree_add_item(tipc_tree, hf_tipc_port_name_type, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset = offset + 4;
				/* Port name instance */
				proto_tree_add_item(tipc_tree, hf_tipc_port_name_instance, tipc_tvb, offset, 4, ENC_BIG_ENDIAN);
				offset = offset + 4;
			}
		}

		if (user < 4 && dissect_tipc_data) { /* DATA type user */
			tvbuff_t *next_tvb;
			uint32_t msg_type32 = msg_type;
			uint32_t *name_type_p = &msg_type32;
			switch (msg_type) {
				case TIPC_CONNECTED_MSG:
					proto_tree_add_item(tipc_tree, hf_tipc_data, tipc_tvb, offset, -1, ENC_NA);
					break;
				case TIPC_NAMED_MSG:
					proto_tree_add_item(tipc_tree, hf_tipc_named_msg_hdr, tipc_tvb, offset, 14, ENC_NA);
					proto_tree_add_item(tipc_tree, hf_tipc_data, tipc_tvb, offset+14, -1, ENC_NA);
					break;
				case TIPC_DIRECT_MSG:
					proto_tree_add_item(tipc_tree, hf_tipc_data, tipc_tvb, offset, -1, ENC_NA);
					break;
				default:
					proto_tree_add_item(tipc_tree, hf_tipc_data, tipc_tvb, offset, -1, ENC_NA);
					break;
			}
			/* tipc data type user doesn't change format, reuse v2 function */
			next_tvb = tvb_new_subset_remaining(tvb, offset);
			call_tipc_v2_data_subdissectors(next_tvb, pinfo, name_type_p, user);
		}
	} /*if (hdr_size <= 5) */

	p_set_proto_depth(pinfo, proto_tipc, recursion_depth - 1);
	return tvb_captured_length(tvb);
}

/* Register TIPC with Wireshark */
void
proto_register_tipc(void)
{
	static hf_register_info hf[] = {
		{ &hf_tipc_msg_fragments,
			{ "Message fragments", "tipc.msg.fragments",
				FT_NONE, BASE_NONE, NULL, 0x00, NULL, HFILL }
		},
		{ &hf_tipc_msg_fragment,
			{ "Message fragment", "tipc.msg.fragment",
				FT_FRAMENUM, BASE_NONE, NULL, 0x00, NULL, HFILL }
		},
		{ &hf_tipc_msg_fragment_overlap,
			{ "Message fragment overlap", "tipc.msg.fragment.overlap",
				FT_BOOLEAN, BASE_NONE, NULL, 0x0, NULL, HFILL }
		},
		{ &hf_tipc_msg_fragment_overlap_conflicts,
			{ "Message fragment overlapping with conflicting data", "tipc.msg.fragment.overlap.conflicts",
				FT_BOOLEAN, BASE_NONE, NULL, 0x0, NULL, HFILL }
		},
		{ &hf_tipc_msg_fragment_multiple_tails,
			{ "Message has multiple tail fragments", "tipc.msg.fragment.multiple_tails",
				FT_BOOLEAN, BASE_NONE, NULL, 0x0, NULL, HFILL }
		},
		{ &hf_tipc_msg_fragment_too_long_fragment,
			{ "Message fragment too long", "tipc.msg.fragment.too_long_fragment",
				FT_BOOLEAN, BASE_NONE, NULL, 0x0, NULL, HFILL }
		},
		{ &hf_tipc_msg_fragment_error,
			{ "Message defragmentation error", "tipc.msg.fragment.error",
				FT_FRAMENUM, BASE_NONE, NULL, 0x00, NULL, HFILL }
		},
		{ &hf_tipc_msg_fragment_count,
			{ "Message fragment count", "tipc.msg.fragment.count",
				FT_UINT32, BASE_DEC, NULL, 0x00, NULL, HFILL }
		},
		{ &hf_tipc_msg_reassembled_in,
			{ "Reassembled in", "tipc.msg.reassembled.in",
				FT_FRAMENUM, BASE_NONE, NULL, 0x00, NULL, HFILL }
		},
		{ &hf_tipc_msg_reassembled_length,
			{ "Reassembled TIPC length", "tipc.msg.reassembled.length",
				FT_UINT32, BASE_DEC, NULL, 0x00, NULL, HFILL }
		},
		{ &hf_tipc_ver,
			{ "Version", "tipc.ver",
				FT_UINT32, BASE_DEC, NULL, 0xe0000000,
				"TIPC protocol version", HFILL }
		},
		{ &hf_tipc_usr,
			{ "User", "tipc.usr",
				FT_UINT32, BASE_DEC, VALS(tipc_user_values), 0x1e000000,
				"TIPC User", HFILL }
		},
		{ &hf_tipcv2_usr,
			{ "User", "tipc.usr",
				FT_UINT32, BASE_DEC, VALS(tipcv2_user_values), 0x1e000000,
				"TIPC User", HFILL }
		},
		{ &hf_tipc_hdr_size,
			{ "Header size", "tipc.hdr_size",
				FT_UINT32, BASE_DEC, NULL, 0x01e00000,
				"TIPC Header size", HFILL }
		},
		{ &hf_tipc_nonsequenced,
			{ "Non-sequenced", "tipc.non_sequenced",
				FT_UINT32, BASE_DEC, NULL, 0x00100000,
				"Non-sequenced Bit", HFILL }
		},
		{ &hf_tipc_destdrop,
			{ "Destination Droppable", "tipc.destdrop",
				FT_UINT32, BASE_DEC, NULL, 0x00080000,
				"Destination Droppable Bit", HFILL }
		},
		{ &hf_tipc_unused,
			{ "Unused", "tipc.hdr_unused",
				FT_UINT32, BASE_DEC, NULL, 0x000e0000,
				"TIPC Unused", HFILL }
		},
		{ &hf_tipc_msg_size,
			{ "Message size", "tipc.msg_size",
				FT_UINT32, BASE_DEC, NULL, 0x0001ffff,
				"TIPC Message size", HFILL }
		},
		{ &hf_tipc_ack_link_lev_seq,
			{ "Acknowledged link level sequence number", "tipc.ack_link_lev_seq",
				FT_UINT32, BASE_DEC, NULL, 0xffff0000,
				"TIPC Acknowledged link level sequence number", HFILL }
		},
		{ &hf_tipc_link_lev_seq,
			{ "Link level sequence number", "tipc.link_lev_seq",
				FT_UINT32, BASE_DEC, NULL, 0x0000ffff,
				"TIPC Link level sequence number", HFILL }
		},
		{ &hf_tipc_prev_proc,
			{ "Previous processor", "tipc.prev_proc",
				FT_STRING, BASE_NONE, NULL, 0x0,
				"TIPC Previous processor", HFILL }
		},
		{ &hf_tipc_org_port,
			{ "Originating port", "tipc.org_port",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				"TIPC Originating port", HFILL }
		},
		{ &hf_tipc_dst_port,
			{ "Destination port", "tipc.dst_port",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				"TIPC Destination port", HFILL }
		},
		{ &hf_tipc_data_msg_type,
			{ "Message type", "tipc.msg_type",
				FT_UINT32, BASE_DEC, VALS(tipc_data_msg_type_values), 0xf0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipc_err_code,
			{ "Error code", "tipc.err_code",
				FT_UINT32, BASE_DEC, VALS(tipc_error_code_values), 0x0f000000,
				"TIPC Error code", HFILL }
		},
		{ &hf_tipc_reroute_cnt,
			{ "Reroute counter", "tipc.route_cnt",
				FT_UINT32, BASE_DEC, NULL, 0x00f00000,
				"TIPC Reroute counter", HFILL }
		},
		{ &hf_tipc_act_id,
			{ "Activity identity", "tipc.act_id",
				FT_UINT32, BASE_DEC, NULL, 0x000fffff,
				"TIPC Activity identity", HFILL }
		},
		{ &hf_tipc_org_proc,
			{ "Originating processor", "tipc.org_proc",
				FT_STRING, BASE_NONE, NULL, 0x0,
				"TIPC Originating processor", HFILL }
		},
		{ &hf_tipc_dst_proc,
			{ "Destination processor", "tipc.dst_proc",
				FT_STRING, BASE_NONE, NULL, 0x0,
				"TIPC Destination processor", HFILL }
		},
		{ &hf_tipc_unused2,
			{ "Unused", "tipc.unused2",
				FT_UINT32, BASE_DEC, NULL, 0xe0000000,
				"TIPC Unused", HFILL }
		},
		{ &hf_tipc_importance,
			{ "Importance", "tipc.importance",
				FT_UINT32, BASE_DEC, NULL, 0x18000000,
				"TIPC Importance", HFILL }
		},
		{ &hf_tipc_link_selector,
			{ "Link selector", "tipc.link_selector",
				FT_UINT32, BASE_DEC, NULL, 0x07000000,
				"TIPC Link selector", HFILL }
		},
		{ &hf_tipc_msg_cnt,
			{ "Message count", "tipc.imsg_cnt",
				FT_UINT32, BASE_DEC, NULL, 0x00ffff00,
				"TIPC Message count", HFILL }
		},
		{ &hf_tipc_probe,
			{ "Probe", "tipc.probe",
				FT_UINT32, BASE_DEC, NULL, 0x00000040,
				"TIPC Probe", HFILL }
		},
		{ &hf_tipc_bearer_id,
			{ "Bearer identity", "tipc.bearer_id",
				FT_UINT32, BASE_DEC, NULL, 0x00000038,
				"TIPC Bearer identity", HFILL }
		},
		{ &hf_tipc_link_selector2,
			{ "Link selector", "tipc.link_selector",
				FT_UINT32, BASE_DEC, NULL, 0x00000007,
				"TIPC Link selector", HFILL }
		},
		{ &hf_tipc_remote_addr,
			{ "Remote address", "tipc.remote_addr",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				"TIPC Remote address", HFILL }
		},
		{ &hf_tipc_rm_msg_type,
			{ "Message type", "tipc.rm_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipc_routing_mgr_msg_type_values), 0xf0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipc_nd_msg_type,
			{ "Message type", "tipc.nd_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipc_name_dist_msg_type_values), 0xf0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipc_cm_msg_type,
			{ "Message type", "tipc.nd_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipc_cm_msg_type_values), 0xf0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipc_lp_msg_type,
			{ "Message type", "tipc.lp_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipc_link_prot_msg_type_values), 0xf0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipc_cng_prot_msg_type,
			{ "Message type", "tipc.cng_prot_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipc_cng_prot_msg_type_values), 0xf0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipc_sm_msg_type,
			{ "Message type", "tipc.sm_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipc_sm_msg_type_values), 0xf0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipc_unknown_msg_type,
			{ "Message type", "tipc.unknown_msg_type",
				FT_UINT32, BASE_DEC, NULL, 0xf0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipc_seq_gap,
			{ "Sequence gap", "tipc.seq_gap",
				FT_UINT32, BASE_DEC, NULL, 0x1fff0000,
				"TIPC Sequence gap", HFILL }
		},
		{ &hf_tipc_nxt_snt_pkg,
			{ "Next sent packet", "tipc.nxt_snt_pkg",
				FT_UINT32, BASE_DEC, NULL, 0x0000ffff,
				"TIPC Next sent packet", HFILL }
		},
		{ &hf_tipc_unused_word,
			{ "Word Unused", "tipc.unused_word",
				FT_NONE, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_tipc_bearer_name,
			{ "Bearer name", "tipc.bearer_name",
				FT_STRINGZ, BASE_NONE, NULL, 0x0,
				"TIPC Bearer name", HFILL }
		},
		{ &hf_tipc_data,
			{ "Data", "tipc.data",
				FT_BYTES, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_tipc_msg_no_bundle,
			{ "Message no. in bundle", "tipc.msg_no_bundle",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_tipc_changeover_protocol,
			{ "TIPC_CHANGEOVER_PROTOCOL", "tipc.changeover_protocol",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_tipc_name_dist_type,
			{ "Published port name type", "tipc.name_dist_type",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				"TIPC Published port name type", HFILL }
		},
		{ &hf_tipc_name_dist_lower,
			{ "Lower bound of published sequence", "tipc.name_dist_lower",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				"TIPC Lower bound of published sequence", HFILL }
		},
		{ &hf_tipc_name_dist_upper,
			{ "Upper bound of published sequence", "tipc.name_dist_upper",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				"TIPC Upper bound of published sequence", HFILL }
		},
		{ &hf_tipc_name_dist_port,
			{ "Random number part of port identity", "tipc.dist_port",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				"TIPC Random number part of port identity", HFILL }
		},
		{ &hf_tipc_name_dist_key,
			{ "Key (Use for verification at withdrawal)", "tipc.dist_key",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				"TIPC key", HFILL }
		},
		{ &hf_tipcv2_srcdrop,
			{ "Source Droppable", "tipc.srcdrop",
				FT_UINT32, BASE_DEC, NULL, 0x00040000,
				"Destination Droppable Bit", HFILL }
		},
		{ &hf_tipcv2_syn,
			{ "Connection request (SYN)", "tipc.syn",
				FT_UINT32, BASE_DEC, NULL, 0x00020000,
				"Destination Droppable Bit", HFILL }
		},
		{ &hf_tipcv2_data_msg_type,
			{ "Message type", "tipc.data_type",
				FT_UINT32, BASE_DEC, VALS(tipc_data_msg_type_values), 0xe0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipcv2_bcast_mtype,
			{ "Message type", "tipcv2.bcast_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipcv2_bcast_mtype_strings), 0xe0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipcv2_bundler_mtype,
			{ "Message type", "tipcv2.bundler_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipcv2_bundler_mtype_strings), 0xe0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipcv2_link_mtype,
			{ "Message type", "tipcv2.link_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipcv2_link_mtype_strings), 0xe0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipcv2_connmgr_mtype,
			{ "Message type", "tipcv2.connmgr_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipcv2_connmgr_mtype_strings), 0xe0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipcv2_route_mtype_1_6,
			{ "Message type", "tipcv2.route_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipcv2_route_mtype_strings_1_6), 0xe0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipcv2_route_mtype_1_7,
			{ "Message type", "tipcv2.route_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipcv2_route_mtype_strings_1_7), 0xe0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipcv2_changeover_mtype,
			{ "Message type", "tipcv2.changeover_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipcv2_changeover_mtype_strings), 0xe0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipcv2_naming_mtype,
			{ "Message type", "tipcv2.naming_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipcv2_naming_mtype_strings), 0xe0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipcv2_fragmenter_mtype,
			{ "Message type", "tipcv2.fragmenter_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipcv2_fragmenter_mtype_strings), 0xe0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipcv2_neighbour_mtype,
			{ "Message type", "tipcv2.data_msg_type",
				FT_UINT32, BASE_DEC, VALS(tipcv2_neighbour_mtype_strings), 0xe0000000,
				"TIPC Message type", HFILL }
		},
		{ &hf_tipcv2_errorcode,
			{ "Error code", "tipcv2.errorcode",
				FT_UINT32, BASE_DEC, VALS(tipcv2_error_code_strings), 0x1e000000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_rer_cnt,
			{ "Reroute Counter", "tipcv2.rer_cnt",
				FT_UINT32, BASE_DEC, NULL, 0x01e00000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_lookup_scope,
			{ "Lookup Scope", "tipcv2.lookup_scope",
				FT_UINT32, BASE_DEC, VALS(tipcv2_lookup_scope_strings), 0x00180000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_opt_p,
			{ "Options Position", "tipcv2.opt_p",
				FT_UINT32, BASE_DEC, NULL, 0x00070000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_broadcast_ack_no,
			{ "Broadcast Acknowledge Number", "tipcv2.broadcast_ack_no",
				FT_UINT32, BASE_DEC, NULL, 0x0000FFFF,
				NULL, HFILL }
		},
		{ &hf_tipcv2_link_level_ack_no,
			{ "Link Level Acknowledge Number", "tipcv2.link_level_ack_no",
				FT_UINT32, BASE_DEC, NULL, 0xFFFF0000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_link_level_seq_no,
			{ "Link Level Sequence Number", "tipcv2.link_level_seq_no",
				FT_UINT32, BASE_DEC, NULL, 0x0000FFFF,
				NULL, HFILL }
		},
#if 0
		{ &hf_tipcv2_bcast_seq_no,
			{ "Broadcast Sequence Number", "tipcv2.bcast_seq_no",
				FT_UINT32, BASE_DEC, NULL, 0x0000FFFF,
				NULL, HFILL }
		},
#endif
		{ &hf_tipcv2_prev_node,
			{ "Previous Node", "tipcv2.prev_node",
				FT_STRING, BASE_NONE, NULL, 0x0,
				"TIPC Previous Node", HFILL }
		},
		{ &hf_tipcv2_orig_node,
			{ "Originating Node", "tipcv2.orig_node",
				FT_STRING, BASE_NONE, NULL, 0x0,
				"TIPC Originating Node", HFILL }
		},
		{ &hf_tipcv2_dest_node,
			{ "Destination Node", "tipcv2.dest_node",
				FT_STRING, BASE_NONE, NULL, 0x0,
				"TIPC Destination Node", HFILL }
		},
		{ &hf_tipcv2_port_name_type,
			{ "Port name type", "tipcv2.port_name_type",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_tipcv2_port_name_instance,
			{ "Port name instance", "tipcv2.port_name_instance",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_tipcv2_multicast_lower,
			{ "Multicast lower bound", "tipcv2.multicast_lower",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				"Multicast port name instance lower bound", HFILL }
		},
		{ &hf_tipcv2_multicast_upper,
			{ "Multicast upper bound", "tipcv2.multicast_upper",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				"Multicast port name instance upper bound", HFILL }
		},
		{ &hf_tipcv2_sequence_gap,
			{ "Sequence Gap", "tipcv2.seq_gap",
				FT_UINT32, BASE_DEC, NULL, 0x1FFF0000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_next_sent_broadcast,
			{ "Next Sent Broadcast", "tipcv2.next_sent_broadcast",
				FT_UINT32, BASE_DEC, NULL, 0xFFFF0000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_fragment_number,
			{ "Fragment Number", "tipcv2.fragment_number",
				FT_UINT32, BASE_DEC, NULL, 0xFFFF0000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_fragment_msg_number,
			{ "Fragment Message Number", "tipcv2.fragment_msg_number",
				FT_UINT32, BASE_DEC, NULL, 0x0000FFFF,
				NULL, HFILL }
		},
		{ &hf_tipcv2_next_sent_packet,
			{ "Next Sent Packet", "tipcv2.next_sent_packet",
				FT_UINT32, BASE_DEC, NULL, 0x0000FFFF,
				NULL, HFILL }
		},
		{ &hf_tipcv2_session_no,
			{ "Session Number", "tipcv2.session_no",
				FT_UINT32, BASE_DEC, NULL, 0xFFFF0000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_link_prio,
			{ "Link Priority", "tipcv2.link_prio",
				FT_UINT32, BASE_DEC, NULL, 0x000001F0,
				NULL, HFILL }
		},
		{ &hf_tipcv2_network_plane,
			{ "Network Plane", "tipcv2.network_plane",
				FT_UINT32, BASE_DEC, VALS(tipcv2_networkplane_strings), 0x0000000E,
				NULL, HFILL }
		},
		{ &hf_tipcv2_probe,
			{ "Probe", "tipcv2.probe",
				FT_UINT32, BASE_DEC, NULL, 0x00000001,
				NULL, HFILL }
		},
		{ &hf_tipcv2_link_tolerance,
			{ "Link Tolerance (ms)", "tipcv2.link_tolerance",
				FT_UINT32, BASE_DEC, NULL, 0x0000FFFF,
				"Link Tolerance in ms", HFILL }
		},
		{ &hf_tipcv2_bearer_instance,
			{ "Bearer Instance", "tipcv2.bearer_instance",
				FT_STRINGZ, BASE_NONE, NULL, 0,
				"Bearer instance used by the sender node for this link", HFILL }
		},
		{ &hf_tipcv2_padding,
			{ "Padding", "tipcv2.padding",
				FT_BYTES, BASE_NONE, NULL, 0,
				NULL, HFILL }
		},
		{ &hf_tipcv2_bearer_level_orig_addr,
			{ "Bearer Level Originating Address", "tipcv2.bearer_level_orig_addr",
				FT_BYTES, BASE_NONE, NULL, 0,
				NULL, HFILL }
		},
		{ &hf_tipcv2_cluster_address,
			{ "Cluster Address", "tipcv2.cluster_address",
				FT_STRING, BASE_NONE, NULL, 0x0,
				"The remote cluster concerned by the table", HFILL }
		},
		{ &hf_tipcv2_bitmap,
			{ "Bitmap", "tipcv2.bitmap",
				FT_BYTES, BASE_NONE, NULL, 0,
				"Bitmap, indicating to which nodes within that cluster the sending node has direct links", HFILL }
		},
		{ &hf_tipcv2_node_address,
			{ "Node Address", "tipcv2.node_address",
				FT_STRING, BASE_NONE, NULL, 0x0,
				"Which node the route addition/loss concern", HFILL }
		},
		{ &hf_tipcv2_destination_domain,
			{ "Destination Domain", "tipcv2.destination_domain",
				FT_STRING, BASE_NONE, NULL, 0x0,
				"The domain to which the link request is directed", HFILL }
		},
		{ &hf_tipcv2_network_id,
			{ "Network Identity", "tipcv2.network_id",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				"The sender node's network identity", HFILL }
		},
		{ &hf_tipcv2_bcast_tag,
			{ "Broadcast Tag", "tipcv2.bcast_tag",
				FT_UINT32, BASE_DEC, NULL, 0xFFFF0000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_msg_count,
			{ "Message Count", "tipcv2.msg_count",
				FT_UINT32, BASE_DEC, NULL, 0xFFFF0000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_max_packet,
			{ "Max Packet", "tipcv2.max_packet",
				FT_UINT32, BASE_DEC, NULL, 0xFFFF0000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_transport_seq_no,
			{ "Transport Sequence No", "tipcv2.tseq_no",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				"Transport Level Sequence Number", HFILL }
		},
		{ &hf_tipcv2_redundant_link,
			{ "Redundant Link", "tipcv2.redundant_link",
				FT_UINT32, BASE_DEC, NULL, 0x00001000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_bearer_id,
			{ "Bearer identity", "tipcv2.bearer_id",
				FT_UINT32, BASE_DEC, NULL, 0x00000e00,
				NULL, HFILL }
		},
		{ &hf_tipcv2_conn_mgr_msg_ack, /* special CONN_MANAGER payload */
			{ "Number of Messages Acknowledged", "tipcv2.conn_mgr_msg_ack",
				FT_UINT32, BASE_DEC, NULL, 0xffff0000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_minor_pv,
			{ "Minor protocol version", "tipcv2.minor_pv",
				FT_UINT32, BASE_DEC, NULL, 0x00ff0000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_node_sig,
			{ "Node signature", "tipcv2.node_sig",
				FT_UINT32, BASE_DEC, NULL, 0x0000FFFF,
				NULL, HFILL }
		},
		{ &hf_tipcv2_filler_mtu_discovery,
			{ "Filler for MTU discovery", "tipcv2.filler_mtu_discovery",
				FT_BYTES, BASE_NONE, NULL, 0,
				NULL, HFILL }
		},
		{ &hf_tipcv2_vendor_specific_data,
			{ "Vendor specific data", "tipcv2.vendor_specific_data",
				FT_BYTES, BASE_NONE, NULL, 0,
				NULL, HFILL }
		},
		{ &hf_tipcv2_options,
			{ "Options", "tipcv2.options",
				FT_BYTES, BASE_NONE, NULL, 0,
				NULL, HFILL }
		},
		{ &hf_tipc_named_msg_hdr,
			{ "TIPC_NAMED_MSG Hdr", "tipc.named_msg_hdr",
				FT_BYTES, BASE_NONE, NULL, 0,
				NULL, HFILL }
		},
		{ &hf_tipc_port_name_type,
			{ "Port name type / Connection level sequence number", "tipc.port_name_type",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_tipc_port_name_instance,
			{ "Port name instance", "tipc.port_name_instance",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_tipc_data_fragment,
			{ "Data fragment", "tipc.data_fragment",
				FT_BYTES, BASE_NONE, NULL, 0,
				NULL, HFILL }
		},
		{ &hf_tipc_message_bundle,
			{ "Message Bundle", "tipc.message_bundle",
				FT_NONE, BASE_NONE, NULL, 0,
				NULL, HFILL }
		},
		{ &hf_tipcv2_timestamp,
			{ "Timestamp", "tipcv2.timestamp",
				FT_UINT32, BASE_DEC, NULL, 0x0,
				"OS-dependent Timestamp", HFILL }
		},
		{ &hf_tipcv2_item_size,
			{ "Item Size", "tipcv2.item_size",
				FT_UINT32, BASE_DEC, NULL, 0xFF000000,
				NULL, HFILL }
		},
		{ &hf_tipcv2_network_region,
			{ "Network Region", "tipcv2.network_region",
				FT_STRING, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_tipcv2_local_router,
			{ "Local Router", "tipcv2.local_router",
				FT_STRING, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_tipcv2_remote_router,
			{ "Remote Router", "tipcv2.remote_router",
				FT_STRING, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_tipcv2_dist_dist,
			{ "Route Distributor Dist", "tipcv2.dist_dist",
				FT_UINT32, BASE_DEC, VALS(tipcv2_dist_dist_strings), 0x000000f0,
				NULL, HFILL }
		},
		{ &hf_tipcv2_dist_scope,
			{ "Route Distributor Scope", "tipcv2.dist_scope",
				FT_UINT32, BASE_DEC, VALS(tipcv2_dist_scope_strings), 0x0000000f,
				NULL, HFILL }
		},
		{ &hf_tipcv2_name_dist_port_id_node,
			{ "Port Id Node", "tipcv2.port_id_node",
				FT_STRING, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_tipcv2_media_id,
			{ "Media Id", "tipcv2.media_id",
				FT_UINT32, BASE_DEC, NULL, 0x000000ff,
				NULL, HFILL }
		}
	};

	/* Setup protocol subtree array */
	static int *ett[] = {
		&ett_tipc,
		&ett_tipc_data,
		&ett_tipc_msg_fragment,
		&ett_tipc_msg_fragments
	};

	static ei_register_info ei[] = {
		{ &ei_tipc_field_not_specified, { "tipc.field_not_specified", PI_PROTOCOL, PI_WARN, "This field is not specified in TIPC v7", EXPFILL }},
		{ &ei_tipc_invalid_bundle_size, { "tipc.invalid_bundle_size", PI_PROTOCOL, PI_WARN, "Invalid message bundle size", EXPFILL }},
		{ &ei_tipc_max_recursion_depth_reached, { "tipc.max_recursion_depth_reached", PI_PROTOCOL, PI_WARN, "Maximum allowed recursion depth reached. Dissection stopped.", EXPFILL }},
	};

	module_t *tipc_module;
	expert_module_t* expert_tipc;

	/* options for the enum in the protocol preferences */
	static const enum_val_t handle_v2_as_options[] = {
		{ "all",     "ALL",          V2_AS_ALL },
		{ "1.5_1.6", "TIPC 1.5/1.6", V2_AS_1_6 },
		{ "1.7",     "TIPC 1.7",     V2_AS_1_7 },
		{ NULL,      NULL,           0 }
	};

	/* Register the protocol name and description */
	proto_tipc = proto_register_protocol("Transparent Inter Process Communication(TIPC)", "TIPC", "tipc");

	/* Required function calls to register the header fields and subtrees used */
	proto_register_field_array(proto_tipc, hf, array_length(hf));
	proto_register_subtree_array(ett, array_length(ett));
	expert_tipc = expert_register_protocol(proto_tipc);
	expert_register_field_array(expert_tipc, ei, array_length(ei));

	/* allow other protocols to be called according to specific values in order to
	 * dissect the protocols sent by TIPC */

	/* this allows e.g. to dissect everything which is TIPC Data */
	tipc_user_dissector = register_dissector_table("tipc.usr",
			"TIPC user", proto_tipc, FT_UINT8, BASE_DEC);
	/* this allows to dissect everything which is TIPC Data and uses a specific
	 * port name type it actually does not really work because the type is not
	 * necessarily set in every data message */
	tipc_type_dissector = register_dissector_table("tipcv2.port_name_type",
			"TIPC port name type", proto_tipc, FT_UINT32, BASE_DEC);

	/* make heuristic dissectors possible */
	tipc_heur_subdissector_list = register_heur_dissector_list_with_description("tipc", "TIPC v2 data", proto_tipc);

	/* Register by name */
	tipc_handle = register_dissector("tipc", dissect_tipc, proto_tipc);
	tipc_tcp_handle = register_dissector("tipc.tcp", dissect_tipc_tcp, proto_tipc);

	reassembly_table_register(&tipc_msg_reassembly_table,
	    &addresses_reassembly_table_functions);

	/* Register configuration options */
	tipc_module = prefs_register_protocol(proto_tipc, NULL);

	tipc_address_type = address_type_dissector_register("AT_TIPC", "TIPC Address Zone,Subnetwork,Processor",
														tipc_addr_to_str_buf, tipc_addr_str_len, NULL, NULL, NULL, NULL, NULL);

	prefs_register_bool_preference(tipc_module, "defragment",
			"Reassemble TIPCv1 SEGMENTATION_MANAGER datagrams",
			"Whether TIPCv1 SEGMENTATION_MANAGER datagrams should be reassembled",
			&tipc_defragment);

	prefs_register_bool_preference(tipc_module, "dissect_tipc_data",
			"Dissect TIPC data",
			"Whether to try to dissect TIPC data or not",
			&dissect_tipc_data);

	prefs_register_bool_preference(tipc_module, "try_heuristic_first",
			"Try heuristic sub-dissectors first",
			"Try to decode a TIPCv2 packet using an heuristic sub-dissector before using a registered sub-dissector",
			&try_heuristic_first);

	prefs_register_enum_preference(tipc_module, "handle_v2_as",
			"Handle version 2 as",
			"TIPC 1.7 removes/adds fields (not) available in TIPC 1.5/1.6 while keeping the version number 2 in the packages. \"ALL\" shows all fields that were ever used in both versions.",
			&handle_v2_as,
			handle_v2_as_options,
			true);

	prefs_register_bool_preference(tipc_module, "desegment",
			"Reassemble TIPC-over-TCP messages spanning multiple TCP segments",
			"Whether the TIPC-over-TCP dissector should reassemble messages spanning multiple TCP segments. "
			"To use this option, you must also enable \"Allow subdissectors to reassemble TCP streams\" in the TCP protocol settings.",
			&tipc_tcp_desegment);
}

void
proto_reg_handoff_tipc(void)
{
	dissector_add_uint("ethertype", ETHERTYPE_TIPC, tipc_handle);
	dissector_add_for_decode_as_with_preference("tcp.port", tipc_tcp_handle);
	dissector_add_uint_range_with_preference("udp.port", DEFAULT_TIPC_PORT_RANGE, tipc_handle);
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 noexpandtab:
 * :indentSize=8:tabSize=8:noTabs=false:
 */
