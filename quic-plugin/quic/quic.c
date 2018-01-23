/*
 * Copyright (c) 2015 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * @file
 * @brief Plus plugin, plugin API / trace / CLI handling.
 */

#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>
#include <quic/quic.h>

#include <vlibapi/api.h>
#include <vlibmemory/api.h>
#include <vlibsocket/api.h>

/* define message IDs */
#include <quic/quic_msg_enum.h>

/* define message structures */
#define vl_typedefs
#include <quic/quic_all_api_h.h> 
#undef vl_typedefs

/* define generated endian-swappers */
#define vl_endianfun
#include <quic/quic_all_api_h.h> 
#undef vl_endianfun

/* instantiate all the print functions we know about */
#define vl_print(handle, ...) vlib_cli_output (handle, __VA_ARGS__)
#define vl_printfun
#include <quic/quic_all_api_h.h> 
#undef vl_printfun

/* Get the API version number */
#define vl_api_version(n,v) static u32 api_version=(v);
#include <quic/quic_all_api_h.h>
#undef vl_api_version

#define REPLY_MSG_ID_BASE pm->msg_id_base
#include <vlibapi/api_helper_macros.h>

/* List of message types that this plugin understands */
#define foreach_quic_plugin_api_msg                           \
_(QUIC_ENABLE_DISABLE, quic_enable_disable)

/* *INDENT-OFF* */
VLIB_PLUGIN_REGISTER () = {
    .version = QUIC_PLUGIN_BUILD_VER,
    .description = "QUIC middlebox VPP Plugin",
};
/* *INDENT-ON* */

/**
 * @brief Enable/disable the plugin. 
 *
 * Action function shared between message handler and debug CLI.
 */

int quic_enable_disable (quic_main_t * pm, u32 sw_if_index,
                                   int enable_disable)
{
  vnet_sw_interface_t * sw;
  int rv = 0;

  /* Utterly wrong? */
  if (pool_is_free_index (pm->vnet_main->interface_main.sw_interfaces, 
                          sw_if_index))
    return VNET_API_ERROR_INVALID_SW_IF_INDEX;

  /* Not a physical port? */
  sw = vnet_get_sw_interface (pm->vnet_main, sw_if_index);
  if (sw->type != VNET_SW_INTERFACE_TYPE_HARDWARE)
    return VNET_API_ERROR_INVALID_SW_IF_INDEX;
  
  /* TODO: change if either pcap file is adapted
   * or new traces are generated */
  vnet_feature_enable_disable ("device-input", "quic",
                               sw_if_index, enable_disable, 0, 0);
  return rv;
}

static clib_error_t *
quic_enable_disable_command_fn (vlib_main_t * vm,
                                   unformat_input_t * input,
                                   vlib_cli_command_t * cmd)
{
  quic_main_t * pm = &quic_main;
  u32 sw_if_index = ~0;
  int enable_disable = 1;
    
  int rv;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT) {
    if (unformat (input, "disable"))
      enable_disable = 0;
    else if (unformat (input, "%U", unformat_vnet_sw_interface,
                       pm->vnet_main, &sw_if_index))
      ;
    else
      break;
  }

  if (sw_if_index == ~0)
    return clib_error_return (0, "Please specify an interface...");
    
  rv = quic_enable_disable (pm, sw_if_index, enable_disable);

  switch(rv) {
  case 0:
    break;

  case VNET_API_ERROR_INVALID_SW_IF_INDEX:
    return clib_error_return 
      (0, "Invalid interface, only works on physical ports");
    break;

  case VNET_API_ERROR_UNIMPLEMENTED:
    return clib_error_return (0, "Device driver doesn't support redirection");
    break;

  default:
    return clib_error_return (0, "quic_enable_disable returned %d",
                              rv);
  }
  return 0;
}

/**
 * @brief format function (print each active flow)
 */
u8 * format_sessions(u8 *s, va_list *args) {
  quic_main_t * pm = &quic_main;
  const char * stateNames[] = {"ACTIVE", "ERROR"};
  s = format(s, "Total flows: %u, total active flows: %u\n",
                  pm->total_flows, pm->active_flows);
  quic_session_t * session;
  s = format(s, "=======================================================\n");
  /* Iterate through all pool entries */
  pool_foreach (session, pm->session_pool, ({
    s = format(s, "Flow id: %lu, observed packets: %u\n",
                    session->id, session->pkt_count);
    s = format(s, "Current state: %s, estimated RTT (client, server): %.*lfs %.*lfs\n",
                    stateNames[session->state], session->basic_spinbit_observer.rtt_client, 9,
                    session->basic_spinbit_observer.rtt_server, 9);
    s = format(s, "=======================================================\n");
  }));
  return s;
}

static clib_error_t * quic_show_stats_fn(vlib_main_t * vm,
                unformat_input_t * input, vlib_cli_command_t * cmd)
{
  vl_print(vm, "%U", format_sessions);
  return 0;
}

/**
 * @brief CLI command to enable/disable the quic plugin.
 */
VLIB_CLI_COMMAND (sr_content_command, static) = {
  .path = "quic",
  .short_help = 
  "quic <interface-name> [disable]",
  .function = quic_enable_disable_command_fn,
};

/**
 * @brief CLI command to show all active flows
 */
VLIB_CLI_COMMAND (sr_content_command_stats, static) = {
  .path = "quic stats",
  .short_help = "Show QUIC middlebox stats",
  .function = quic_show_stats_fn,
};

/**
 * @brief QUIC API message handler.
 */
static void vl_api_quic_enable_disable_t_handler
(vl_api_quic_enable_disable_t * mp)
{
  vl_api_quic_enable_disable_reply_t * rmp;
  quic_main_t * pm = &quic_main;
  int rv;

  rv = quic_enable_disable (pm, ntohl(mp->sw_if_index), 
                                      (int) (mp->enable_disable));
  
  REPLY_MACRO(VL_API_QUIC_ENABLE_DISABLE_REPLY);
}

/**
 * @brief Set up the API message handling tables.
 */
static clib_error_t *
quic_plugin_api_hookup (vlib_main_t *vm)
{
  quic_main_t * pm = &quic_main;
#define _(N,n)                                                  \
    vl_msg_api_set_handlers((VL_API_##N + pm->msg_id_base),     \
                           #n,					\
                           vl_api_##n##_t_handler,              \
                           vl_noop_handler,                     \
                           vl_api_##n##_t_endian,               \
                           vl_api_##n##_t_print,                \
                           sizeof(vl_api_##n##_t), 1); 
    foreach_quic_plugin_api_msg;
#undef _

    return 0;
}

#define vl_msg_name_crc_list
#include <quic/quic_all_api_h.h>
#undef vl_msg_name_crc_list

static void 
setup_message_id_table (quic_main_t * pm, api_main_t *am)
{
#define _(id,n,crc) \
  vl_msg_api_add_msg_name_crc (am, #n "_" #crc, id + pm->msg_id_base);
  foreach_vl_msg_name_crc_quic;
#undef _
}

/**
 *  @brief create the hash key
 */
void make_key(quic_key_t * kv, ip4_address_t * src_ip, ip4_address_t * dst_ip,
                u16 src_p, u16 dst_p, u8 protocol)
{
  kv->s_x_d_ip = src_ip->as_u32 ^ dst_ip->as_u32;
  kv->s_x_d_port = src_p ^ dst_p;
  kv->protocol = protocol;
}

/**
 *  @brief get session pointer if corresponding key is known
 */
quic_session_t * get_session_from_key(quic_key_t * kv_in)
{
  BVT(clib_bihash_kv) kv, kv_return;
  quic_main_t *pm = &quic_main;
  BVT(clib_bihash) *bi_table;
  bi_table = &pm->quic_table;
  kv.key = kv_in->as_u64;
  int rv = BV(clib_bihash_search) (bi_table, &kv, &kv_return);
  if (rv != 0) {
    /* Key does not exist */
    return 0;
  } else {
    return get_quic_session(kv_return.value);
  }
}

/**
 * @brief update RTT estimations.
 * Currently, only 1-bit spin and valid bit implemented
 */
void update_rtt_estimate(vlib_main_t * vm, quic_session_t * session, f64 now,
                u16 src_port, u8 measurement, u32 packet_number) {

  /*
  * FIRST we run the basic observer
  */
  {
    basic_spin_observer_t *observer = &(session->basic_spinbit_observer);
    bool spin = measurement & ONE_BIT_SPIN;

    /* if this is a packet from the SERVER */
    if (src_port == QUIC_PORT) {
      if (observer->spin_server != spin){
        observer->spin_server = spin;
        observer->rtt_server = now - observer->time_last_spin_server;
        observer->time_last_spin_server = now;
        vlib_cli_output(vm, "[TIME:] %.*lf [BASIC-RTT-SERVER:] %.*lf, [SPIN:] %u, [PN:] %u\n",
                          now, 9, observer->rtt_server, 9, spin ? 1 : 0, packet_number);
      }
    /* if this is a packet from the CLIENT */
    } else {
      if (observer->spin_client != spin){
        observer->spin_client = spin;
        observer->rtt_client = now - observer->time_last_spin_client;
        observer->time_last_spin_client = now;
        vlib_cli_output(vm, "[TIME:] %.*lf [BASIC-RTT-CLIENT:] %.*lf, [SPIN:] %u, [PN:] %u\n",
                          now, 9, observer->rtt_client, 9, spin ? 1 : 0, packet_number);
      }
    }
  }

 /*
  * SECOND we run the packet number (PN) observer
  */

  //TODO this does not handle PN wrap arrounds yet
  {
    pn_spin_observer_t *observer = &(session->pn_spin_observer);
    bool spin = measurement & ONE_BIT_SPIN;

    /* if this is a packet from the SERVER */
    if (src_port == QUIC_PORT) {
      /* check if arrived in order and has different spin */
      if (packet_number > observer->pn_server && observer->spin_server != spin) {
        observer->spin_server = spin;
        observer->pn_server = packet_number;
        observer->rtt_server = now - observer->time_last_spin_server;
        observer->time_last_spin_server = now;
        vlib_cli_output(vm, "[TIME:] %.*lf [PN-RTT-SERVER:] %.*lf, [SPIN:] %u, [PN:] %u\n",
                          now, 9, observer->rtt_server, 9, spin ? 1 : 0, packet_number);
      }
    /* if this is a packet from the CLIENT */
    } else {
      /* check if arrived in order and has different spin */
      if (packet_number > observer->pn_client && observer->spin_client != spin) {
        observer->spin_client = spin;
        observer->pn_client = packet_number;
        observer->rtt_client = now - observer->time_last_spin_client;
        observer->time_last_spin_client = now;
        vlib_cli_output(vm, "[TIME:] %.*lf [PN-RTT-CLIENT:] %.*lf, [SPIN:] %u, [PN:] %u\n",
                          now, 9, observer->rtt_client, 9, spin ? 1 : 0, packet_number);
        }
      }
  }

  /*
  * THIRD we run the packet number (PN) observer with VALID bit
  */

  //TODO this does not handle PN wrap arrounds yet
  {
    pn_valid_spin_observer_t *observer = &(session->pn_valid_spin_observer);
    bool spin = measurement & ONE_BIT_SPIN;
    bool valid = measurement & VALID_BIT;

    /* if this is a packet from the SERVER */
    if (src_port == QUIC_PORT) {
      /* check if arrived in order and has different spin */
      if (packet_number > observer->pn_server && observer->spin_server != spin) {
        observer->spin_server = spin;
        observer->pn_server = packet_number;
        observer->valid_server = valid;
        observer->rtt_server = now - observer->time_last_spin_server;
        observer->time_last_spin_server = now;
        /* only report RTT if it was valid over the entire roundtrip */
        if (observer->valid_server && observer->valid_client){
          vlib_cli_output(vm, "[TIME:] %.*lf [PN-VALID-RTT-SERVER:] %.*lf, [SPIN:] %u, [PN:] %u\n",
                          now, 9, observer->rtt_server, 9, spin ? 1 : 0, packet_number);
        }
      }
    /* if this is a packet from the CLIENT */
    } else {
      /* check if arrived in order and has different spin */
      if (packet_number > observer->pn_client && observer->spin_client != spin) {
        observer->spin_client = spin;
        observer->pn_client = packet_number;
        observer->valid_client = valid;
        observer->rtt_client = now - observer->time_last_spin_client;
        observer->time_last_spin_client = now;
        /* only report RTT if it was valid over the entire roundtrip */
        if (observer->valid_server && observer->valid_client){
          vlib_cli_output(vm, "[TIME:] %.*lf [PN-VALID-RTT-CLIENT:] %.*lf, [SPIN:] %u, [PN:] %u\n",
                          now, 9, observer->rtt_client, 9, spin ? 1 : 0, packet_number);
        }
      }
    }
  }

  /*
  * FOURTH we run the dual spin bit observer
  */

  //TODO this does not handle PN wrap arrounds yet
  {
    two_bit_spin_observer_t *observer = &(session->two_bit_spin_observer);
    u8 spin = measurement >> TWO_BIT_SPIN_OFFSET;

    /* if this is a packet from the SERVER */
    if (src_port == QUIC_PORT) {
      /* check if arrived in order and has different spin */
      if (spin == ((observer->spin_server + 1) % 4)) {
        observer->spin_server = spin;
        observer->rtt_server = now - observer->time_last_spin_server;
        observer->time_last_spin_server = now;
        /* only report RTT if it was valid over the entire roundtrip */
        vlib_cli_output(vm, "[TIME:] %.*lf [TWO-BIT-RTT-SERVER:] %.*lf, [SPIN:] %u, [PN:] %u\n",
                        now, 9, observer->rtt_server, 9, spin, packet_number);
      }
    /* if this is a packet from the CLIENT */
    } else {
      if (spin == ((observer->spin_client + 1) % 4)) {
        observer->spin_client = spin;
        observer->rtt_client = now - observer->time_last_spin_client;
        observer->time_last_spin_client = now;
        /* only report RTT if it was valid over the entire roundtrip */
        vlib_cli_output(vm, "[TIME:] %.*lf [TWO-BIT-RTT-CLIENT:] %.*lf, [SPIN:] %u, [PN:] %u\n",
                        now, 9, observer->rtt_client, 9, spin, packet_number);
      }
    }
  }
}

  /* bellow here: old stuff */
//   /* TODO: implement blocking bit */
//   /* TODO: implement logic for 2-bit spin */
//   /* TODO: add support for PN observer */
//   if (measurement & VALID_BIT) {
//     bool spin = measurement & ONE_BIT_SPIN;
//     if (src_port == QUIC_PORT) {
//       if (session->spin_server != spin) {
//         session->spin_server = spin;
//         session->rtt_server = now - session->time_last_spin_server;
//         session->time_last_spin_server = now;
//         vlib_cli_output(vm, "[%.*lf] RTT server: %.*lf, spin -> %u, packet number: %u\n",
//                         now, 9, session->rtt_server, 9, spin ? 1 : 0, packet_number);
//       }
//     } else {
//       if (session->spin_client != spin) {
//         session->spin_client = spin;
//         session->rtt_client = now - session->time_last_spin_client;
//         session->time_last_spin_client = now;
//         vlib_cli_output(vm, "[%.*lf] RTT client: %.*lf, spin -> %u, packet number: %u\n",
//                         now, 9, session->rtt_client, 9, spin ? 1 : 0, packet_number);
//       }
//     }
//   }
// }

/**
 * @brief update the state of the session with the given key
 */
void update_state(quic_key_t * kv_in, uword new_state)
{
  BVT(clib_bihash_kv) kv;
  quic_main_t *pm = &quic_main;
  BVT(clib_bihash) *bi_table;
  bi_table = &pm->quic_table;
  kv.key = kv_in->as_u64;
  kv.value = new_state;
  BV(clib_bihash_add_del) (bi_table, &kv, 1 /* is_add */);
}

/**
 * @brief create a new session for a new flow
 */
u32 create_session() {
  quic_session_t * session;
  quic_main_t * pm = &quic_main;
  pm->active_flows ++;
  pm->total_flows ++;
  pool_get (pm->session_pool, session);
  memset (session, 0, sizeof (*session));
  /* Correct session index */
  session->index = session - pm->session_pool;
  session->state = 0;
  return session->index;
}

/**
 * @brief clean session after timeout
 */
void clean_session(u32 index)
{
  quic_main_t * pm = &quic_main;
  quic_session_t * session = get_quic_session(index);
  
  /* If main loop (in node.c) is executed sparsely, it can happen that
   * the timer wheel triggers multiple times for the same session.
   * We remove/clean the session only the first time. */
  if (session == 0) {
    return;
  }
  pm->active_flows --;
  
  BVT(clib_bihash_kv) kv;
  BVT(clib_bihash) * bi_table;
  bi_table = &pm->quic_table;
  kv.key = session->key;
  
  /* clear hash and pool entry */
  BV(clib_bihash_add_del) (bi_table, &kv, 0 /* is_add */);
  pool_put (pm->session_pool, session);
}

/**
 * @brief callback function for expired timer
 */
static void timer_expired_callback(u32 * expired_timers)
{
  int i;
  u32 index, timer_id;
  
  /* Iterate over all expired timers */
  for (i = 0; i < vec_len(expired_timers); i = i+1)
  {
    /* Extract index and timer wheel id */
    index = expired_timers[i] & 0x7FFFFFFF;
    timer_id = expired_timers[i] >> 31;
    
    /* Only use timer with ID 0 at the moment */
    ASSERT (timer_id == 0);

    clean_session(index);
  }
}

/**
 * @brief Initialize the quic plugin.
 */
static clib_error_t * quic_init (vlib_main_t * vm)
{
  quic_main_t * pm = &quic_main;
  clib_error_t * error = 0;
  u8 * name;

  pm->vnet_main =  vnet_get_main ();
  name = format (0, "quic_%08x%c", api_version, 0);
  
  /* Ask for a correctly-sized block of API message decode slots */
  pm->msg_id_base = vl_msg_api_get_msg_ids 
      ((char *) name, VL_MSG_FIRST_AVAILABLE);
  
  error = quic_plugin_api_hookup (vm);
  
  /* Add our API messages to the global name_crc hash table */
  setup_message_id_table (pm, &api_main);
  
  /* Init bihash */
  BV (clib_bihash_init) (&pm->quic_table, "quic", 2048, 512<<20);

  /* Timer wheel has 2048 slots, so we predefine pool with 2048 entries as well */ 
  pool_init_fixed(pm->session_pool, 2048);

  /* Init timer wheel with 100ms resolution */
  tw_timer_wheel_init_2t_1w_2048sl (&pm->tw, timer_expired_callback, 100e-3, ~0);
  pm->tw.last_run_time = vlib_time_now (vm);
  
  /* Set counters to zero*/
  pm->total_flows = 0;
  pm->active_flows = 0;

  vec_free(name);

  return error;
}

VLIB_INIT_FUNCTION (quic_init);

/**
 * @brief Hook the QUIC plugin into the VPP graph hierarchy.
 */
VNET_FEATURE_INIT (quic, static) = 
{
  /* It runs in the device-input arc before the ethernet-input */  
  /* TODO: change if either pcap file is adapted
   * or new traces are generated */
  .arc_name = "device-input",
  .node_name = "quic",
  .runs_before = VNET_FEATURES ("ethernet-input"),
};