/* Copyright (C)
*  2016 Steve Wilson <wevets@gmail.com>
* 2025 - Christoph van Wüllen, DL1YCF
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/

/* TS-2000 emulation via TCP */
/*
 * PiHPSDR RigCtl by Steve KA6S Oct 16 2016
 * With a kindly assist from Jae, K5JAE who has helped
 * greatly with hamlib integration!
 */

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <netinet/tcp.h>

#include "actions.h"
#include "agc.h"
#include "andromeda.h"
#include "band.h"
#include "band_menu.h"
#include "bandstack.h"
#include "channel.h"
#include "ext.h"
#include "filter.h"
#include "filter_menu.h"
#include "g2panel.h"
#include "g2panel_menu.h"
#include "iambic.h"
#include "main.h"
#include "message.h"
#include "mode.h"
#include "new_menu.h"
#include "new_protocol.h"
#include "noise_menu.h"
#include "old_protocol.h"
#include "property.h"
#include "radio.h"
#include "receiver.h"
#include "rigctl.h"
#include "rigctl_menu.h"
#include "sliders.h"
#include "store.h"
#include "toolbar.h"
#include "transmitter.h"
#include "vfo.h"
#include "zoompan.h"


unsigned int rigctl_tcp_port = 19090;
int rigctl_tcp_enable = 0;
int rigctl_tcp_andromeda = 0;
int rigctl_tcp_autoreporting = 0;

// max number of bytes we can get at once
#define MAXDATASIZE 2000

gboolean rigctl_debug = FALSE;

static int parse_cmd (void *data);

int cat_control = 0;

static GMutex mutex_numcat;   // only needed to make in/de-crements of "cat_control"  atomic

#define MAX_TCP_CLIENTS 3
#define MAX_ANDROMEDA_LEDS 16

static GThread *rigctl_server_thread_id = NULL;
static GThread *rigctl_cw_thread_id = NULL;
static int tcp_running = 0;

static int server_socket = -1;
static struct sockaddr_in server_address;

typedef struct _client {
  int fd;
  int fifo;                         // serial only: this is a FIFO and not a true serial line
  int busy;                         // serial only (for FIFO handling)
  int done;                         // serial only (for FIFO handling)
  int running;                      // set this to zero to terminate client
  socklen_t address_length;         // TCP only: initialised by accept(), never used
  struct sockaddr_in address;       // TCP only: initialised by accept(), never used
  GThread *thread_id;               // ID of thread that serves the client
  guint andromeda_timer;            // for reporting ANDROMEDA LED states
  guint auto_timer;                 // for auto-reporting FA/FB
  int auto_reporting;               // auto-reporting (AI, ZZAI) 0...3
  int andromeda_type;               // 1:Andromeda, 4:G2Mk1 with CM5 upgrade, 5:G2 ultra
  int last_v;                       // Last push-button state received
  int last_fa, last_fb, last_md;    // last VFO-A/B frequency and VFO-A mode reported
  int last_led[MAX_ANDROMEDA_LEDS]; // last status of ANDROMEDA LEDs
  int shift;                        // shift state for original ANDROMEDA console
  int *buttonvec;                   // For G2 ANDROMEDA: button action map
  int *encodervec;                  // For G2 ANDROMEDA: encoder action map
} CLIENT;

//
// A G2V2 VFO encoder has 480 ticks per revolution and reports the number of ticks
// accumulated in 20 msec.
// Thus, slow/fast/veryfast tuning with 1/2/3 revolutions per second generates
// 9/19/29 ticks in one ZZZU/ZZZD command.
// Up to 1 rev/sec, no modification should occur. At 2 rev/sec, the tuning speed should
// be doubled, at 3 rev/sec and beyond it should be quadrupled.
//
// The following table maps input_ticks to output_ticks. If there are more than 30 input
// ticks in one ZZZU/ZZZD message, the number is quadrupled, and this is defined
// through the last entry in the table.
//
static uint8_t andromeda_vfo_speedup[32] = {  0,   1,   2,   3,   4,   5,   6,   7,
                                              8,   9,  11,  12,  14,  17,  19,  22,
                                              25,  29,  33,  38,  43,  48,  54,  61,
                                              69,  77,  85,  95, 105, 116, 128,   4
                                           };
typedef struct _command {
  CLIENT *client;
  char *command;
} COMMAND;

static CLIENT tcp_client[MAX_TCP_CLIENTS]; // TCP clients
static CLIENT serial_client[MAX_SERIAL];   // serial clienta
SERIALPORT SerialPorts[MAX_SERIAL];

static gpointer rigctl_client (gpointer data);

//
// This macro handles cases where RX2 is referred to but might not
// exist. These macros lead to an action only  if the RX exists.
// RXCHECK_ERR sets an error flag if RX is non-exisiting.
// RXCHECK     just silently ignores the command
//
#define RXCHECK_ERR(id, what) if (id >= 0 && id < receivers) { what; } else { implemented = FALSE; }
#define RXCHECK(id, what)     if (id >= 0 && id < receivers) { what; }

int rigctl_tcp_running() {
  return (server_socket >= 0);
}

void shutdown_tcp_rigctl() {
  struct linger linger = { 0 };
  linger.l_onoff = 1;
  linger.l_linger = 0;
  t_print("%s: server_socket=%d\n", __FUNCTION__, server_socket);
  tcp_running = 0;

  //
  // Gracefully terminate all active TCP connections
  //
  for (int id = 0; id < MAX_TCP_CLIENTS; id++) {
    if (tcp_client[id].andromeda_timer != 0) {
      g_source_remove(tcp_client[id].andromeda_timer);
      tcp_client[id].andromeda_timer = 0;
    }

    if (tcp_client[id].auto_timer != 0) {
      g_source_remove(tcp_client[id].auto_timer);
      tcp_client[id].auto_timer = 0;
    }

    tcp_client[id].running = 0;

    if (tcp_client[id].fd != -1) {
      if (setsockopt(tcp_client[id].fd, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger)) == -1) {
        t_perror("setsockopt(...,SO_LINGER,...) failed for client:");
      }

      t_print("%s: closing client socket: %d\n", __FUNCTION__, tcp_client[id].fd);
      close(tcp_client[id].fd);
      tcp_client[id].fd = -1;
    }

    if (tcp_client[id].thread_id) {
      g_thread_join(tcp_client[id].thread_id);
      tcp_client[id].thread_id = NULL;
    }
  }

  //
  // Close server socket
  //
  if (server_socket >= 0) {
    if (setsockopt(server_socket, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger)) == -1) {
      t_perror("setsockopt(...,SO_LINGER,...) failed for server:");
    }

    t_print("%s: closing server_socket: %d\n", __FUNCTION__, server_socket);
    close(server_socket);
    server_socket = -1;
  }

  // TODO: join with the server thread, but this requires to make the accept() there
  //       non-blocking (use select())
}

//
//  CW ring buffer
//

#define CW_BUF_SIZE 80
#define NSEC_PER_SEC 1000000000L
static char cw_buf[CW_BUF_SIZE];
static int  cw_buf_in = 0, cw_buf_out = 0;

static int dotsamples;
static int dashsamples;

//
// send_dash()         send a "key-down" of a dashlen, followed by a "key-up" of a dotlen
// send_dot()          send a "key-down" of a dotlen,  followed by a "key-up" of a dotlen
// send_space(int len) send a "key_down" of zero,      followed by a "key-up" of len*dotlen
//
//
static void send_dash() {
  struct timespec ts;

  if (cw_key_hit) { return; }

  clock_gettime(CLOCK_MONOTONIC, &ts);
  tx_queue_cw_event(1, 0);
  tx_queue_cw_event(0, dashsamples);
  tx_queue_cw_event(0, dotsamples);
  ts.tv_nsec += (dashsamples + dotsamples) * 20833;

  while (ts.tv_nsec > NSEC_PER_SEC) {
    ts.tv_nsec -= NSEC_PER_SEC;
    ts.tv_sec++;
  }

  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
}

static void send_dot() {
  struct timespec ts;

  if (cw_key_hit) { return; }

  clock_gettime(CLOCK_MONOTONIC, &ts);
  tx_queue_cw_event(1, 0);
  tx_queue_cw_event(0, dotsamples);
  tx_queue_cw_event(0, dotsamples);
  ts.tv_nsec += (2 * dotsamples) * 20833;

  while (ts.tv_nsec > NSEC_PER_SEC) {
    ts.tv_nsec -= NSEC_PER_SEC;
    ts.tv_sec++;
  }

  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
}

static void send_space(int len) {
  struct timespec ts;

  if (cw_key_hit) { return; }

  clock_gettime(CLOCK_MONOTONIC, &ts);
  tx_queue_cw_event(0, len * dotsamples);
  ts.tv_nsec += (len * dotsamples) * 20833;

  while (ts.tv_nsec > NSEC_PER_SEC) {
    ts.tv_nsec -= NSEC_PER_SEC;
    ts.tv_sec++;
  }

  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
}

//
// This stores the "buffered join character" status
//
static int join_cw_characters = 0;

static void rigctl_send_cw_char(char cw_char) {
  const char *pattern;

  switch (cw_char) {
  case 'a':
  case 'A':
    pattern =  ".-";
    break;

  case 'b':
  case 'B':
    pattern =  "-...";
    break;

  case 'c':
  case 'C':
    pattern = "-.-.";
    break;

  case 'd':
  case 'D':
    pattern = "-..";
    break;

  case 'e':
  case 'E':
    pattern = ".";
    break;

  case 'f':
  case 'F':
    pattern = "..-.";
    break;

  case 'g':
  case 'G':
    pattern = "--.";
    break;

  case 'h':
  case 'H':
    pattern = "....";
    break;

  case 'i':
  case 'I':
    pattern = "..";
    break;

  case 'j':
  case 'J':
    pattern = ".---";
    break;

  case 'k':
  case 'K':
    pattern = "-.-";
    break;

  case 'l':
  case 'L':
    pattern = ".-..";
    break;

  case 'm':
  case 'M':
    pattern = "--";
    break;

  case 'n':
  case 'N':
    pattern = "-.";
    break;

  case 'o':
  case 'O':
    pattern = "---";
    break;

  case 'p':
  case 'P':
    pattern = ".--.";
    break;

  case 'q':
  case 'Q':
    pattern = "--.-";
    break;

  case 'r':
  case 'R':
    pattern = ".-.";
    break;

  case 's':
  case 'S':
    pattern = "...";
    break;

  case 't':
  case 'T':
    pattern = "-";
    break;

  case 'u':
  case 'U':
    pattern = "..-";
    break;

  case 'v':
  case 'V':
    pattern = "...-";
    break;

  case 'w':
  case 'W':
    pattern = ".--";
    break;

  case 'x':
  case 'X':
    pattern = "-..-";
    break;

  case 'y':
  case 'Y':
    pattern = "-.--";
    break;

  case 'z':
  case 'Z':
    pattern = "--..";
    break;

  case '0':
    pattern = "-----";
    break;

  case '1':
    pattern = ".----";
    break;

  case '2':
    pattern = "..---";
    break;

  case '3':
    pattern = "...--";
    break;

  case '4':
    pattern = "....-";
    break;

  case '5':
    pattern = ".....";
    break;

  case '6':
    pattern = "-....";
    break;

  case '7':
    pattern = "--...";
    break;

  case '8':
    pattern = "---..";
    break;

  case '9':
    pattern = "----.";
    break;

  //
  //     DL1YCF:
  //     added some signs from ITU Recommendation M.1677-1 (2009)
  //     in the order given there.
  //
  case '.':
    pattern = ".-.-.-";
    break;

  case ',':
    pattern = "--..--";
    break;

  case ':':
    pattern = "---..";
    break;

  case '?':
    pattern = "..--..";
    break;

  case '\'':
    pattern = ".----.";
    break;

  case '-':
    pattern = "-....-";
    break;

  case '/':
    pattern = "-..-.";
    break;

  case '(':
    pattern = "-.--.";
    break;

  case ')':
    pattern = "-.--.-";
    break;

  case '"':
    pattern = ".-..-.";
    break;

  case '=':
    pattern = "-...-";
    break;

  case '+':
    pattern = ".-.-.";
    break;

  case '@':
    pattern = ".--.-.";
    break;

  //
  //     Often used, but not ITU: Ampersand for "wait"
  //
  case '&':
    pattern = ".-...";
    break;

  default:
    pattern = "";
  }

  while (*pattern != '\0') {
    if (*pattern == '-') {
      send_dash();
    }

    if (*pattern == '.') {
      send_dot();
    }

    pattern++;
  }

  // The last element (dash or dot) sent already has one dotlen space appended.
  // If the current character is another "printable" sign, we need an additional
  // pause of 2 dotlens to form the inter-character spacing of 3 dotlens.
  // However if the current character is a "space" we must produce an inter-word
  // spacing (7 dotlens) and therefore need 6 additional dotlens
  // We need no longer take care of a sequence of spaces since adjacent spaces
  // are now filtered out while filling the CW character (ring-) buffer.
  if (cw_char == ' ') {
    send_space(6);  // produce inter-word space of 7 dotlens
  } else {
    if (!join_cw_characters) { send_space(2); }  // produce inter-character space of 3 dotlens
  }
}

//
// rigctl_cw_thread is started once and runs forever,
// checking for data in the CW ring buffer and sending it.
//
static gpointer rigctl_cw_thread(gpointer data) {
  int i;
  char cwchar;
  int  buffered_speed = 0;
  int  bracket_command = 0;

  for (;;) {
    // wait for CW data (periodically look every 100 msec)
    if (cw_buf_in == cw_buf_out) {
      cw_key_hit = 0;
      usleep(100000L);
      continue;
    }

    //
    // if TX mode is not CW, drain ring buffer
    //
    int txmode = vfo_get_tx_mode();

    if (txmode != modeCWU && txmode != modeCWL) {
      cw_buf_out = cw_buf_in;
      continue;
    }

    //
    // Take one character from the ring buffer
    //
    cwchar = cw_buf[cw_buf_out];
    t_print("CWCHAR=%c\n", cwchar);
    i = cw_buf_out + 1;

    if (i >= CW_BUF_SIZE) { i = 0; }

    cw_buf_out = i;

    //
    // Special character sequences or characters:
    //
    //  [+         Increase speed by 25 %
    //  [-         Decrease speed by 25 %
    //  [          Join Characters
    //  ]          End speed change or joining
    //
    if (bracket_command)  {
      switch (cwchar) {
      case '+':
        buffered_speed = (5 * cw_keyer_speed) / 4;
        cwchar = 0;
        break;

      case '-':
        buffered_speed = (3 * cw_keyer_speed) / 4;
        cwchar = 0;
        break;

      case '.':
        join_cw_characters = 1;
        cwchar = 0;
        break;
      }

      bracket_command = 0;
    }

    if (cwchar == '[') {
      bracket_command = 1;
      cwchar = 0;
    }

    if (cwchar == ']') {
      buffered_speed = 0;
      join_cw_characters = 0;
      cwchar = 0;
    }

    // The dot and dash length may have changed, so recompute them here
    // This means that we can change the speed (KS command) while
    // the buffer is being sent
    if (buffered_speed > 0) {
      dotsamples = 57600 / buffered_speed;
      dashsamples = (3456 * cw_keyer_weight) / buffered_speed;
    } else {
      dotsamples = 57600 / cw_keyer_speed;
      dashsamples = (3456 * cw_keyer_weight) / cw_keyer_speed;
    }

    CAT_cw_is_active = 1;

    if (!radio_is_remote) { schedule_transmit_specific(); }

    if (!mox) {
      // activate PTT
      g_idle_add(ext_set_mox, GINT_TO_POINTER(1));
      // have to wait until it is really there
      // Note that if out-of-band, we would wait
      // forever here, so allow at most 200 msec
      i = 200;

      while (!mox && i-- > 0) { usleep(1000L); }

      // still no MOX? --> silently discard CW character and give up
      if (!mox) {
        CAT_cw_is_active = 0;

        if (!radio_is_remote) { schedule_transmit_specific(); }

        continue;
      }
    }

    // At this point, mox == 1 and CAT_cw_active == 1
    if (cw_key_hit) {
      //
      // CW transmission has been aborted, either due to manually
      // removing MOX, changing the mode to non-CW, or because a CW key has been hit.
      // Do not remove PTT in the latter case
      buffered_speed = 0;
      CAT_cw_is_active = 0;

      if (!radio_is_remote) { schedule_transmit_specific(); }

      //
      // keep draining ring buffer until it stays empty for 0.5 seconds
      // This is necessary: after aborting a very long CW
      // text such as a CQ call by hitting a Morse key,
      // CW characters may flow in for quite a while.
      //
      do {
        cw_buf_out = cw_buf_in;
        usleep(500000L);
      } while (cw_buf_out != cw_buf_in);
    } else {
      if (cwchar) { rigctl_send_cw_char(cwchar); }

      //
      // Character has been sent, so continue.
      // Since the second character possibly comes 250 msec after
      // the first one, we have to wait if the buffer stays
      // empty. Only then, stop CAT CW.
      //
      for (i = 0; i < 5; i++ ) {
        if (cw_buf_in != cw_buf_out) { break; }

        usleep(50000);
      }

      if (cw_buf_in != cw_buf_out) { continue; }

      CAT_cw_is_active = 0;

      if (!radio_is_remote) { schedule_transmit_specific(); }

      if (!cw_key_hit && !radio_ptt) {
        g_idle_add(ext_set_mox, GINT_TO_POINTER(0));
        // wait up to 500 msec for MOX having gone
        // otherwise there might be a race condition when sending
        // the next character really soon
        i = 10;

        while (mox && (i--) > 0) { usleep(50000L); }

        buffered_speed = 0;
      }
    }
  }

  // NOTREACHED (now this thread is started once-and-for-all)

  // We arrive here if the rigctl server shuts down.
  // This very rarely happens. But we should shut down the
  // local CW system gracefully, in case we were in the mid
  // of a transmission
  if (CAT_cw_is_active) {
    CAT_cw_is_active = 0;

    if (!radio_is_remote) { schedule_transmit_specific(); }

    g_idle_add(ext_set_mox, GINT_TO_POINTER(0));
  }

  rigctl_cw_thread_id = NULL;
  return NULL;
}

static void send_resp (int fd, char * msg) {
  //
  // send_resp is ONLY called from within the GTK event queue
  // ==> no multi-thread problems can occur.
  //
  if (fd == -1) {
    //
    // This means the client fd has been explicitly closed
    // in the mean time. Silently give up and do not
    // emit an error message.
    //
    return;
  }

  if (rigctl_debug) { t_print("RIGCTL: RESP=%s\n", msg); }

  int length = strlen(msg);
  int count = 0;

  while (length > 0) {
    //
    // Since this is in the GTK event queue, we cannot try
    // for a long time. In case of an error (rc < 0) we give
    // up immediately, for rc == 0 we try at most 10 times.
    //
    int rc = write(fd, msg, length);

    if (rc < 0) { return; }

    if (rc == 0) {
      count++;

      if (count > 10) { return; }
    }

    length -= rc;
    msg += rc;
  }
}

static int wdspmode(int kenwoodmode) {
  int wdspmode;

  switch (kenwoodmode) {
  case 1:                // Kenwood LSB
    wdspmode = modeLSB;
    break;

  case 2:                // Kenwood USB
    wdspmode = modeUSB;
    break;

  case 3:                // Kenwood CW (upper side band)
    wdspmode = modeCWU;
    break;

  case 4:                // Kenwood FM
    wdspmode = modeFMN;
    break;

  case 5:                // Kenwood AM
    wdspmode = modeAM;
    break;

  case 6:                // Kenwood FSK (lower side band)
    wdspmode = modeDIGL;
    break;

  case 7:                // Kenwood CW-R (lower side band)
    wdspmode = modeCWL;
    break;

  case 9:                // Kenwood FSK-R (upper side band)
    wdspmode = modeDIGU;
    break;

  default:
    // NOTREACHED?
    wdspmode = modeLSB;
    break;
  }

  return wdspmode;
}

static int ts2000_mode(int wdspmode) {
  int kenwoodmode;

  switch (wdspmode) {
  case modeLSB:
    kenwoodmode = 1;  // Kenwood LDB
    break;

  case modeUSB:
    kenwoodmode = 2;  // Kenwood USB
    break;

  case modeCWL:
    kenwoodmode = 7;  // Kenwood CW-R
    break;

  case modeCWU:
    kenwoodmode = 3;  // Kenwood CW
    break;

  case modeFMN:
    kenwoodmode = 4;  // Kenwood FM
    break;

  case modeAM:
  case modeSAM:
    kenwoodmode = 5;  // Kenwood AM
    break;

  case modeDIGL:
    kenwoodmode = 6;  // Kenwood FSK
    break;

  case modeDIGU:
    kenwoodmode = 9;  // Kenwood FSK-R
    break;

  default:
    // NOTREACHED?
    kenwoodmode = 1;  // LSB
    break;
  }

  return kenwoodmode;
}

static gboolean autoreport_handler(gpointer data) {
  CLIENT *client = (CLIENT *) data;
  //
  // This function is repeatedly called as long as the CAT
  // connection is active. It reports VFOA and VFOB frequency changes
  // to the client, provided it has auto-reporting enabled and is running.
  //
  // Note this runs in the GTK event queue so it cannot interfere
  // with another CAT command.
  // Auto-reporting to a FIFO is suppressed because all data sent there will
  // be echoed back and then be read again.
  //

  if (client->fifo || !client->running) {
    //
    // return and remove timer
    //
    client->auto_timer = 0;
    return G_SOURCE_REMOVE;
  }

  if (client->auto_reporting > 0) {
    long long fa = vfo[VFO_A].ctun ? vfo[VFO_A].ctun_frequency : vfo[VFO_A].frequency;
    long long fb = vfo[VFO_B].ctun ? vfo[VFO_B].ctun_frequency : vfo[VFO_B].frequency;

    if (fa != client->last_fa) {
      char reply[256];
      snprintf(reply,  sizeof(reply), "FA%011lld;", fa);
      send_resp(client->fd, reply);
      client->last_fa = fa;
    }

    if (fb != client->last_fb) {
      char reply[256];
      snprintf(reply,  sizeof(reply), "FB%011lld;", fb);
      send_resp(client->fd, reply);
      client->last_fb = fb;
    }
  }

  if (client->auto_reporting > 1) {
    int md = vfo[VFO_A].mode;

    if (md != client->last_md) {
      char reply[256];
      snprintf(reply,  sizeof(reply), "MD%1d;", ts2000_mode(md));
      send_resp(client->fd, reply);
      client->last_md = md;
    }
  }

  return TRUE;
}

static gboolean andromeda_handler(gpointer data) {
  //
  // This function is repeatedly called as long as the client runs
  //
  //
  CLIENT *client = (CLIENT *)data;
  char reply[256];

  if (!client->running || client->andromeda_type == 4) {
    //
    // If the client is no longer running, remove source.
    // The same applies for ANDROMEDA type-4 clients since there
    // are no LEDs on a G2MkI panel
    //
    client->andromeda_timer = 0;
    return G_SOURCE_REMOVE;
  }

  //
  // Do not proceed until Andromeda version is known
  // Send a ZZZS command and re-trigger the handler
  //
  if (client->andromeda_type < 1) {
    snprintf(reply,  sizeof(reply), "ZZZS;");
    send_resp(client->fd, reply);
    return TRUE;
  }

  for (int led = 0; led < MAX_ANDROMEDA_LEDS; led++) {
    int new = client->last_led[led];

    if (client->andromeda_type == 1) {
      //
      // Original ANDROMEDA console
      //
      switch (led) {
      case 1:
        new = mox;
        break;

      case 2:
        // ATU has TUNE solution
        break;

      case 3:
        new = tune;
        break;

      case 4:

        // According to the ANAN document this is LED #5
        if (can_transmit) {
          new = transmitter->puresignal;
        } else {
          new = 0;
        }

        break;

      case 5:
        // According to the ANAN document this is LED #5
        new = diversity_enabled;
        break;

      case 6:
        new = client->shift;
        break;

      case 7:
        new = vfo[active_receiver->id].ctun;
        break;

      case 8:
        new = vfo[active_receiver->id].rit_enabled;
        break;

      case 9:
        new = vfo[vfo_get_tx_vfo()].xit_enabled;
        break;

      case 10:
        new = (active_receiver->id  == 0);
        break;

      case 11:
        new = locked;
        break;
      }
    }

    if (client->andromeda_type == 5) {
      //
      // G2Mk2 (a.k.a. G2 Ultra) console
      //
      switch (led) {
      case 1:
        new = mox;
        break;

      case 2:
        new = tune;
        break;

      case 3:
        if (can_transmit) {
          new = transmitter->puresignal;
        } else {
          new = 0;
        }

        break;

      case 4:
        new = auto_tune_flag;
        break;

      case 6:
        new = vfo[active_receiver->id].rit_enabled;
        break;

      case 7:
        new = vfo[vfo_get_tx_vfo()].xit_enabled;
        break;

      case 8:
        new = (active_receiver->id  == 0);
        break;

      case 9:
        new = locked;
        break;
      }
    }

    //
    // if LED status changed, send it via ZZZI command
    //
    if (client->last_led[led] != new) {
      snprintf(reply,  sizeof(reply), "ZZZI%02d%d;", led, new);
      send_resp(client->fd, reply);
      client->last_led[led] = new;
    }
  }

  return TRUE;
}

static gboolean andromeda_oneshot_handler(gpointer data) {
  //
  // This is the handler, called once, so it has to return
  // G_SOURCE_REMOVE. It is intended to be exectuted via
  // g_idle_add() at the end of a ZZZP handling when
  // "immediate" LED update is desired.
  //
  (void) andromeda_handler(data);
  return G_SOURCE_REMOVE;
}

static gpointer rigctl_server(gpointer data) {
  int port = GPOINTER_TO_INT(data);
  int on = 1;
  t_print("%s: starting TCP server on port %d\n", __FUNCTION__, port);
  server_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (server_socket < 0) {
    t_perror("rigctl_server: listen socket failed");
    return NULL;
  }

  SETSOCKOPT(server_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  SETSOCKOPT(server_socket, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
  // bind to listening port
  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(port);

  if (bind(server_socket, (struct sockaddr * )&server_address, sizeof(server_address)) < 0) {
    t_perror("rigctl_server: listen socket bind failed");
    close(server_socket);
    return NULL;
  }

  for (int id = 0; id < MAX_TCP_CLIENTS; id++) {
    tcp_client[id].fd = -1;
    tcp_client[id].fifo = 0;
    tcp_client[id].auto_reporting = 0;
  }

  // listen with a max queue of 3
  if (listen(server_socket, 3) < 0) {
    t_perror("rigctl_server: listen failed");
    close(server_socket);
    return NULL;
  }

  // must start the thread here in order NOT to inherit a lock
  cw_buf_in = 0;
  cw_buf_out = 0;

  if (!rigctl_cw_thread_id) { rigctl_cw_thread_id = g_thread_new("RIGCTL cw", rigctl_cw_thread, NULL); }

  while (tcp_running) {
    int spare;
    //
    // find a spare slot
    //
    spare = -1;

    for (int id = 0; id < MAX_TCP_CLIENTS; id++) {
      if (tcp_client[id].fd == -1) {
        spare = id;
        break;
      }
    }

    // if all slots are in use, wait and continue
    if (spare < 0) {
      usleep(100000L);
      continue;
    }

    //
    // A slot is available, try to get connection via accept()
    // (this initialises fd, address, address_length)
    //
    t_print("%s: slot= %d waiting for connection\n", __FUNCTION__, spare);
    tcp_client[spare].fd = accept(server_socket, (struct sockaddr*)&tcp_client[spare].address,
                                  &tcp_client[spare].address_length);

    if (tcp_client[spare].fd < 0) {
      t_perror("rigctl_server: client accept failed");
      tcp_client[spare].fd = -1;
      continue;
    }

    t_print("%s: slot= %d connected with fd=%d\n", __FUNCTION__, spare, tcp_client[spare].fd);
    //
    // Setting TCP_NODELAY may (or may not) improve responsiveness
    // by *disabling* Nagle's algorithm for clustering small packets
    //
#ifdef __APPLE__

    if (setsockopt(tcp_client[spare].fd, IPPROTO_TCP, TCP_NODELAY, (void *)&on, sizeof(on)) < 0) {
#else

    if (SETSOCKOPT(tcp_client[spare].fd, SOL_TCP, TCP_NODELAY, &on, sizeof(on)) < 0) {
#endif
      t_perror("TCP_NODELAY");
    }

    //
    // Initialise client data structure
    //
    tcp_client[spare].fifo            = 0;
    tcp_client[spare].busy            = 0;
    tcp_client[spare].done            = 0;
    tcp_client[spare].running         = 1;
    tcp_client[spare].andromeda_timer = 0;
    tcp_client[spare].auto_reporting  = SET(rigctl_tcp_autoreporting);
    tcp_client[spare].andromeda_type  = 0;
    tcp_client[spare].last_fa         = -1;
    tcp_client[spare].last_fb         = -1;
    tcp_client[spare].last_md         = -1;
    tcp_client[spare].last_v          = 0;
    tcp_client[spare].shift           = 0;
    tcp_client[spare].buttonvec       = NULL;
    tcp_client[spare].encodervec      = NULL;

    for (int i = 0; i < MAX_ANDROMEDA_LEDS; i++) {
      tcp_client[spare].last_led[i] = -1;
    }

    //
    // Spawn off thread that "does" the connection
    //
    tcp_client[spare].thread_id       = g_thread_new("rigctl client", rigctl_client, (gpointer)&tcp_client[spare]);
    //
    // Launch auto-reporter task
    //
    tcp_client[spare].auto_timer = g_timeout_add(750, autoreport_handler, &tcp_client[spare]);

    //
    // If ANDROMEDA is enabled for TCP, lauch periodic ANDROMEDA task
    //
    if (rigctl_tcp_andromeda) {
      // Note this will send a ZZZS; command upon first invocation
      tcp_client[spare].andromeda_timer = g_timeout_add(500, andromeda_handler, &tcp_client[spare]);
    }
  }

  close(server_socket);
  return NULL;
}

static gpointer rigctl_client (gpointer data) {
  CLIENT *client = (CLIENT *)data;
  t_print("%s: starting rigctl_client: socket=%d\n", __FUNCTION__, client->fd);
  g_mutex_lock(&mutex_numcat);
  cat_control++;

  if (rigctl_debug) { t_print("RIGCTL: CTLA INC cat_control=%d\n", cat_control); }

  g_mutex_unlock(&mutex_numcat);
  g_idle_add(ext_vfo_update, NULL);
  int i;
  int numbytes;
  char  cmd_input[MAXDATASIZE] ;
  char *command = g_new(char, MAXDATASIZE);
  int command_index = 0;

  while (client->running && (numbytes = recv(client->fd, cmd_input, MAXDATASIZE - 2, 0)) > 0 ) {
    for (i = 0; i < numbytes; i++) {
      //
      // Filter out newlines and other non-printable characters
      // These may occur when doing CAT manually with a terminal program
      //
      if (cmd_input[i] < 32) {
        continue;
      }

      command[command_index] = cmd_input[i];
      command_index++;

      if (cmd_input[i] == ';') {
        command[command_index] = '\0';

        if (rigctl_debug) { t_print("RIGCTL: command=%s\n", command); }

        COMMAND *info = g_new(COMMAND, 1);
        info->client = client;
        info->command = command;
        g_idle_add(parse_cmd, info);
        command = g_new(char, MAXDATASIZE);
        command_index = 0;
      }
    }
  }

  // Release the last "command" buffer (that has not yet been used)
  g_free(command);
  t_print("%s: Leaving rigctl_client thread\n", __FUNCTION__);

  //
  // If rigctl is disabled via the GUI, the connections are closed by shutdown_rigctl_ports()
  // but even the we should decrement cat_control
  //
  if (client->fd != -1) {
    struct linger linger = { 0 };
    linger.l_onoff = 1;
    linger.l_linger = 0;

    if (setsockopt(client->fd, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger)) == -1) {
      t_perror("setsockopt(...,SO_LINGER,...) failed for client:");
    }

    if (client->andromeda_timer != 0) {
      g_source_remove(client->andromeda_timer);
      client->andromeda_timer = 0;
    }

    if (client->auto_timer != 0) {
      g_source_remove(client->auto_timer);
      client->auto_timer = 0;
    }

    client->running = 0;
    close(client->fd);
    client->fd = -1;
  }

  // Decrement CAT_CONTROL
  g_mutex_lock(&mutex_numcat);
  cat_control--;
  g_mutex_unlock(&mutex_numcat);
  g_idle_add(ext_vfo_update, NULL);
  return NULL;
}

static gboolean parse_extended_cmd (const char *command, CLIENT *client) {
  gboolean implemented = TRUE;
  char reply[256];
  reply[0] = '\0';

  switch (command[2]) {
  case 'A': //ZZAx
    switch (command[3]) {
    case 'C': //ZZAC

      //CATDEF    ZZAC
      //DESCR     Set/read VFO-A step size
      //SET       ZZACxx;
      //READ      ZZAC;
      //RESP      ZZACxx;
      //NOTE      x 0...16 encodes the step size:
      //NOTE      1 Hz (x=0), 10 Hz (x=1), 25 Hz (x=2), 50 Hz (x=3)
      //CONT      100 Hz (x=4), 250 Hz (x=5), 500 Hz (x=6)
      //CONT      1000 Hz (x=7), 5000 Hz (x=8), 6250 Hz (x=9)
      //CONT      9 kHz (x=10), 10 kHz (x=11), 12.5 kHz (x=12)
      //CONT      100 kHz (x=13), 250 kHz (x=14)
      //CONT      500 kHz (x=15), 1 MHz (x=16).
      //ENDDEF
      if (command[4] == ';') {
        // read the step size
        snprintf(reply,  sizeof(reply), "ZZAC%02d;", vfo_id_get_stepindex(VFO_A));
        send_resp(client->fd, reply) ;
      } else if (command[6] == ';') {
        // set the step size
        int i = atoi(&command[4]) ;
        vfo_id_set_step_from_index(VFO_A, i);
        g_idle_add(ext_vfo_update, NULL);
      } else {
      }

      break;

    case 'D': //ZZAD

      //CATDEF    ZZAD
      //DESCR     Move down VFO-A frequency by a selected step
      //SET       ZZACxx;
      //NOTE      x encodes the step size, see ZZAC command.
      //ENDDEF
      if (command[6] == ';') {
        int step_index = atoi(&command[4]);
        long long hz = (long long) vfo_get_step_from_index(step_index);
        vfo_id_move(VFO_A, -hz, FALSE);
      } else {
      }

      break;

    case 'E': //ZZAE

      //CATDEF    ZZAE
      //DESCR     Move down VFO-A frequency by several steps
      //SET       ZZAExx;
      //NOTE      VFO-A frequency moved down by x (0...99) times the current step size
      //ENDDEF
      if (command[6] == ';') {
        int steps = atoi(&command[4]);
        vfo_id_step(VFO_A, -steps);
      }

      break;

    case 'F': //ZZAF

      //CATDEF    ZZAF
      //DESCR     Move up VFO-A frequency by several steps
      //SET       ZZAFxx;
      //NOTE      VFO-A frequency moved up by x (0...99) times the current step size
      //ENDDEF
      if (command[6] == ';') {
        int steps = atoi(&command[4]);
        vfo_id_step(VFO_A, steps);
      }

      break;

    case 'G': //ZZAG

      //CATDEF    ZZAG
      //DESCR     Set/Read RX1 volume (AF slider)
      //SET       ZZAGxxx;
      //READ      ZZAG;
      //RESP      ZZAGxxx;
      //NOTE      x = 0...100, mapped logarithmically to -40 ... 0 dB.
      //ENDDEF
      if (command[4] == ';') {
        // send reply back
        snprintf(reply,  sizeof(reply), "ZZAG%03d;", (int)(100.0 * pow(10.0, 0.05 * receiver[0]->volume)));
        send_resp(client->fd, reply) ;
      } else {
        int gain = atoi(&command[4]);

        if (gain < 2) {
          receiver[0]->volume = -40.0;
        } else {
          receiver[0]->volume = 20.0 * log10(0.01 * (double) gain);
        }

        set_af_gain(0, receiver[0]->volume);
      }

      break;

    case 'I': //ZZAI

      //CATDEF    ZZAI
      //DESCR     Set/Read auto-reporting
      //SET       ZZAIx;
      //READ      ZZAI;
      //RESP      ZZAIx;
      //NOTE      x=0: auto-reporting disabled, x>0: enabled.
      //NOTE      Auto-reporting is affected for the client that sends this command.
      //CONT      For x=1, only frequency changes are sent via FA/FB commands.
      //CONT      For x>1, mode changes are also sent via MD commands.
      //ENDDEF
      if (command[4] == ';') {
        // Query status
        snprintf(reply,  sizeof(reply), "ZZAI%d;", client->auto_reporting);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        client->auto_reporting = command[4] - '0';

        if (client->auto_reporting < 0) { client->auto_reporting = 0; }

        if (client->auto_reporting > 3) { client->auto_reporting = 3; }
      } else {
        implemented = FALSE;
      }

      break;

    case 'R': //ZZAR

      //CATDEF    ZZAR
      //DESCR     Set/Read RX1 AGC gain
      //SET       ZZARxxxx;
      //READ      ZZAR;
      //RESP      ZZARxxxx;
      //NOTE      x -20...120, must contain + or - sign.
      //ENDDEF
      if (command[4] == ';') {
        // send reply back
        snprintf(reply,  sizeof(reply), "ZZAR%+04d;", (int)(receiver[0]->agc_gain));
        send_resp(client->fd, reply) ;
      } else {
        int threshold = atoi(&command[4]);
        set_agc_gain(VFO_A, (double)threshold);
      }

      break;

    case 'S': //ZZAS

      //CATDEF    ZZAS
      //DESCR     Set/Read RX2 AGC gain
      //SET       ZZASxxxx;
      //READ      ZZAS;
      //RESP      ZZASxxxx;
      //NOTE      x -20...120, must contain + or - sign.
      //ENDDEF
      if (receivers == 2) {
        if (command[4] == ';') {
          // send reply back
          snprintf(reply,  sizeof(reply), "ZZAS%+04d;", (int)(receiver[1]->agc_gain));
          send_resp(client->fd, reply) ;
        } else {
          int threshold = atoi(&command[4]);
          set_agc_gain(VFO_B, (double)threshold);
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'U': //ZZAU

      //CATDEF    ZZAU
      //DESCR     Move up VFO-A frequency by selected step
      //SET       ZZAUxx;
      //NOTE      x 0...16 selects the size of the step, see ZZAC command.
      //ENDDEF
      if (command[6] == ';') {
        int step_index = atoi(&command[4]);
        long long hz = (long long) vfo_get_step_from_index(step_index);
        vfo_id_move(VFO_A, hz, FALSE);
      } else {
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'B': //ZZBx
    switch (command[3]) {
    case 'A': //ZZBA

      //CATDEF    ZZBA
      //DESCR     Move VFO-B one band down
      //SET       ZZBA;
      //NOTE      Wraps from lowest to highest band.
      //ENDDEF
      if (command[4] == ';') {
        if (receivers == 2) {
          band_minus(receiver[1]->id);
        } else {
          implemented = FALSE;
        }
      }

      break;

    case 'B': //ZZBB

      //CATDEF    ZZBB
      //DESCR     Move VFO-B one band up
      //SET       ZZBB;
      //NOTE      Wraps from highest to lowest band.
      //ENDDEF
      if (command[4] == ';') {
        if (receivers == 2) {
          band_plus(receiver[1]->id);
        } else {
          implemented = FALSE;
        }
      }

      break;

    case 'D': //ZZBD

      //CATDEF    ZZBD
      //DESCR     Move VFO-A one band down
      //SET       ZZBD;
      //NOTE      Wraps from lowest to highest band.
      //ENDDEF
      if (command[4] == ';') {
        band_minus(receiver[0]->id);
      }

      break;

    case 'E': //ZZBE

      //CATDEF    ZZBE
      //DESCR     Move down VFO-B frequency by multiple steps
      //SET       ZZBExx;
      //NOTE      VFO-B frequency moves down by x (0..99) times the current step size
      //ENDDEF
      if (command[6] == ';') {
        int steps = atoi(&command[4]);
        vfo_id_step(VFO_B, -steps);
      }

      break;

    case 'F': //ZZBF

      //CATDEF    ZZBF
      //DESCR     Move up VFO-B frequency by multiple steps
      //SET       ZZBFxx;
      //NOTE      VFO-B frequency moves up by x (0...99) times the current step size
      //ENDDEF
      if (command[6] == ';') {
        int steps = atoi(&command[4]);
        vfo_id_step(VFO_B, +steps);
      }

      break;

    case 'M': //ZZBM

      //CATDEF    ZZBM
      //DESCR     Move down VFO-B frequency by selected step.
      //SET       ZZBMxx;
      //NOTE      x 0...16 selects the size of the step, see ZZAC command.
      //ENDDEF
      if (command[6] == ';') {
        int step_index = atoi(&command[4]);
        long long hz = (long long) vfo_get_step_from_index(step_index);
        vfo_id_move(VFO_B, -hz, FALSE);
      } else {
      }

      break;

    case 'P': //ZZBP

      //CATDEF    ZZBP
      //DESCR     Move up VFO-B frequency by selected step.
      //SET       ZZBPxx;
      //NOTE      x 0...16 selects the size of the step, see ZZAC command.
      //ENDDEF
      if (command[6] == ';') {
        int step_index = atoi(&command[4]);
        long long hz = (long long) vfo_get_step_from_index(step_index);
        vfo_id_move(VFO_B, hz, FALSE);
      }

      break;

    case 'S':
    case 'T': { //ZZBS and ZZBT
      int v = VFO_A;

      if (command[3] == 'T') { v = VFO_B; }

      //CATDEF    ZZBS
      //DESCR     Set/Read VFO-A band
      //SET       ZZBSxxx;
      //NOTE      x 0...999 encodes the band:
      //NOTE      136 kHz (x=136), 472 kHz (x=472), 160M (x=160)
      //CONT      80M (x=80), 60M (x=60), 40M (x=40), 30M (x=30)
      //CONT      20M (x=20), 17M (x=17), 15M (x=15), 12M (x=12)
      //CONT      10M (x=10), 6M (x=6), Gen (x=888), WWV (x=999).
      //ENDDEF
      //CATDEF    ZZBT
      //DESCR     Set/Read VFO-B band
      //SET       ZZBTxxx;
      //NOTE      x 0...999 encodes the band, see ZZBS command.
      //ENDDEF
      if (command[4] == ';') {
        int b;

        switch (vfo[v].band) {
        case band136:
          b = 136;
          break;

        case band472:
          b = 472;
          break;

        case band160:
          b = 160;
          break;

        case band80:
          b = 80;
          break;

        case band60:
          b = 60;
          break;

        case band40:
          b = 40;
          break;

        case band30:
          b = 30;
          break;

        case band20:
          b = 20;
          break;

        case band17:
          b = 17;
          break;

        case band15:
          b = 15;
          break;

        case band12:
          b = 12;
          break;

        case band10:
          b = 10;
          break;

        case band6:
          b = 6;
          break;

        case bandGen:
          b = 888;
          break;

        case bandWWV:
          b = 999;
          break;

        default:
          b = 20;
          break;
        }

        snprintf(reply,  sizeof(reply), "ZZB%c%03d;", 'S' + v, b);
        send_resp(client->fd, reply) ;
      } else if (command[7] == ';') {
        int band = band20;
        int b = atoi(&command[4]);

        switch (b) {
        case 136:
          band = band136;
          break;

        case 472:
          band = band472;
          break;

        case 160:
          band = band160;
          break;

        case 80:
          band = band80;
          break;

        case 60:
          band = band60;
          break;

        case 40:
          band = band40;
          break;

        case 30:
          band = band30;
          break;

        case 20:
          band = band20;
          break;

        case 17:
          band = band17;
          break;

        case 15:
          band = band15;
          break;

        case 12:
          band = band12;
          break;

        case 10:
          band = band10;
          break;

        case 6:
          band = band6;
          break;

        case 888:
          band = bandGen;
          break;

        case 999:
          band = bandWWV;
          break;
        }

        vfo_id_band_changed(v, band);
      }
    }
    break;

    case 'U': //ZZBU

      //CATDEF    ZZBU
      //DESCR     Move VFO-A one band up
      //SET       ZZBU;
      //NOTE      Wraps from highest to lowest band.
      //ENDDEF
      if (command[4] == ';') {
        band_plus(receiver[0]->id);
      }

      break;

    case 'Y': //ZZBY
      // closes console (ignored)
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'C': //ZZCx
    switch (command[3]) {
    case 'N': //ZZCN

      //CATDEF    ZZCN
      //DESCR     Set/Read VFO-A CTUN status
      //SET       ZZCNx;
      //READ      ZZCN;
      //RESP      ZZCNx;
      //NOTE      x=0: CTUN disabled, x=1: enabled
      //ENDDEF
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZCN%d;", vfo[VFO_A].ctun);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        int state = atoi(&command[4]);
        vfo_id_ctun_update(VFO_A, state);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'O': //ZZCO

      //CATDEF    ZZCO
      //DESCR     Set/Read VFO-B CTUN status
      //SET       ZZCOx;
      //READ      ZZCO;
      //RESP      ZZCOx;
      //NOTE      x=0: CTUN disabled, x=1: enabled
      //ENDDEF
      if (command[4] == ';') {
        // return the CTUN status
        snprintf(reply,  sizeof(reply), "ZZCO%d;", vfo[VFO_B].ctun);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        int state = atoi(&command[4]);
        vfo_id_ctun_update(VFO_B, state);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'P': //ZZCP

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      // set/read compander
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZCP%d;", 0);
        send_resp(client->fd, reply) ;
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'D': //ZZDx
    switch (command[3]) {
    case 'B': //ZZDB

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      // set/read RX Reference
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZDB%d;", 0); // currently always 0
        send_resp(client->fd, reply) ;
      }

      break;

    case 'C': //ZZDC

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      // set/get diversity gain
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZDC%04d;", (int)div_gain);
        send_resp(client->fd, reply) ;
      }

      break;

    case 'D': //ZZDD

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      // set/get diversity phase
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZDD%04d;", (int)div_phase);
        send_resp(client->fd, reply) ;
      }

      break;

    case 'M': //ZZDM

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      // set/read Display Mode
      if (command[4] == ';') {
        int v = 0;

        if (receiver[0]->display_waterfall) {
          v = 8;
        } else {
          v = 2;
        }

        snprintf(reply,  sizeof(reply), "ZZDM%d;", v);
        send_resp(client->fd, reply) ;
      }

      break;

    case 'N': //ZZDN

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      // set/read waterfall low
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZDN%+4d;", receiver[0]->waterfall_low);
        send_resp(client->fd, reply) ;
      }

      break;

    case 'O': //ZZDO

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      // set/read waterfall high
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZDO%+4d;", receiver[0]->waterfall_high);
        send_resp(client->fd, reply) ;
      }

      break;

    case 'P': //ZZDP

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      // set/read panadapter high
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZDP%+4d;", receiver[0]->panadapter_high);
        send_resp(client->fd, reply) ;
      }

      break;

    case 'Q': //ZZDQ

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      // set/read panadapter low
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZDQ%+4d;", receiver[0]->panadapter_low);
        send_resp(client->fd, reply) ;
      }

      break;

    case 'R': //ZZDR

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      // set/read panadapter step
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZDR%2d;", receiver[0]->panadapter_step);
        send_resp(client->fd, reply) ;
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'E': //ZZEx
    switch (command[3]) {
    case 'R': //ZZER

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      // set/read rx equaliser
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZER%d;", receiver[0]->eq_enable);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        receiver[0]->eq_enable = SET(atoi(&command[4]));
      }

      break;

    case 'T': //ZZET

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      // set/read tx equaliser
      if (can_transmit) {
        if (command[4] == ';') {
          snprintf(reply,  sizeof(reply), "ZZET%d;", transmitter->eq_enable);
          send_resp(client->fd, reply) ;
        } else if (command[5] == ';') {
          transmitter->eq_enable = SET(atoi(&command[4]));
        }
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'F': //ZZFx
    switch (command[3]) {
    case 'A': //ZZFA

      //CATDEF    ZZFA
      //DESCR     Set/Read VFO-A frequency
      //SET       ZZFAxxxxxxxxxxx;
      //READ      ZZFA;
      //RESP      ZZFAxxxxxxxxxxx;
      //NOTE      x in Hz, left-padded with zeroes
      //ENDDEF
      if (command[4] == ';') {
        if (vfo[VFO_A].ctun) {
          snprintf(reply,  sizeof(reply), "ZZFA%011lld;", vfo[VFO_A].ctun_frequency);
        } else {
          snprintf(reply,  sizeof(reply), "ZZFA%011lld;", vfo[VFO_A].frequency);
        }

        send_resp(client->fd, reply) ;
      } else if (command[15] == ';') {
        long long f = atoll(&command[4]);
        vfo_id_set_frequency(VFO_A, f);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'B': //ZZFB

      //CATDEF    ZZFB
      //DESCR     Set/Read VFO-B frequency
      //SET       ZZFBxxxxxxxxxxx;
      //READ      ZZFB;
      //RESP      ZZFBxxxxxxxxxxx;
      //NOTE      x in Hz, left-padded with zeroes
      //ENDDEF
      if (command[4] == ';') {
        if (vfo[VFO_B].ctun) {
          snprintf(reply,  sizeof(reply), "ZZFB%011lld;", vfo[VFO_B].ctun_frequency);
        } else {
          snprintf(reply,  sizeof(reply), "ZZFB%011lld;", vfo[VFO_B].frequency);
        }

        send_resp(client->fd, reply) ;
      } else if (command[15] == ';') {
        long long f = atoll(&command[4]);
        vfo_id_set_frequency(VFO_B, f);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'D': //ZZFD

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZFD%d;", vfo[VFO_A].deviation == 2500 ? 0 : 1);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        int d = atoi(&command[4]);
        vfo[VFO_A].deviation = d ? 5000 : 2500;
        rx_set_filter(receiver[0]);

        if (can_transmit) {
          tx_set_filter(transmitter);
        }

        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'H': //ZZFH

      //CATDEF    ZZFH
      //DESCR     Set/Read RX1 filter high water
      //SET       ZZFHxxxxx;
      //READ      ZZFH;
      //RESP      ZZFHxxxxxx;
      //NOTE      x must be in the range -9999 ... 9999 and start with a minus sign if negative.
      //CONT      If setting, this switches to the Var1 filter first.
      //CONT      The convention is such that LSB, the filter high cut is negative and affects the low audio frequencies.
      //ENDDEF
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZFH%05d;", receiver[0]->filter_high);
        send_resp(client->fd, reply) ;
      } else if (command[9] == ';') {
        int fh = atoi(&command[4]);
        fh = fmin(9999, fh);
        fh = fmax(-9999, fh);

        // make sure filter is filterVar1
        if (vfo[VFO_A].filter != filterVar1) {
          vfo_id_filter_changed(VFO_A, filterVar1);
        }

        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        FILTER *filter = &mode_filters[filterVar1];
        filter->high = fh;
        vfo_id_filter_changed(VFO_A, filterVar1);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'I': //ZZFI

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZFI%02d;", vfo[VFO_A].filter);
        send_resp(client->fd, reply) ;
      } else if (command[6] == ';') {
        int filter = atoi(&command[4]);
        vfo_id_filter_changed(VFO_A, filter);
      }

      break;

    case 'J': //ZZFJ

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZFJ%02d;", vfo[VFO_B].filter);
        send_resp(client->fd, reply) ;
      } else if (command[6] == ';') {
        int filter = atoi(&command[4]);
        vfo_id_filter_changed(VFO_B, filter);
      }

      break;

    case 'L': //ZZFL

      //CATDEF    ZZFL
      //DESCR     Set/Read RX1 filter low water
      //SET       ZZFLxxxxx;
      //READ      ZZFL;
      //RESP      ZZFLxxxxxx;
      //NOTE      x must be in the range -9999 ... 9999 and start with a minus sign if negative.
      //CONT      If setting, this switches to the Var1 filter first.
      //CONT      The convention is such that LSB, the filter low cut is negative and affects the high audio frequencies.
      //ENDDEF
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZFL%05d;", receiver[0]->filter_low);
        send_resp(client->fd, reply) ;
      } else if (command[9] == ';') {
        int fl = atoi(&command[4]);
        fl = fmin(9999, fl);
        fl = fmax(-9999, fl);

        // make sure filter is filterVar1
        if (vfo[VFO_A].filter != filterVar1) {
          vfo_id_filter_changed(VFO_A, filterVar1);
        }

        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        FILTER *filter = &mode_filters[filterVar1];
        filter->low = fl;
        vfo_id_filter_changed(VFO_A, filterVar1);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'G': //ZZGx
    switch (command[3]) {
    case 'T': //ZZGT

      //CATDEF    ZZGT
      //DESCR     Set/Read RX1 AGC
      //SET       ZZGTx;
      //READ      ZZGT;
      //RESP      ZZGTx;
      //NOTE      x=0: AGC OFF, x=1: LONG, x=2: SLOW, x=3: MEDIUM, x=4: FAST
      //ENDDEF
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZGT%d;", receiver[0]->agc);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        int agc = atoi(&command[4]);
        // update RX1 AGC
        receiver[0]->agc = agc;
        rx_set_agc(receiver[0]);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'U': //ZZGU
      //CATDEF    ZZGU
      //DESCR     Set/Read RX2 AGC
      //SET       ZZGUx;
      //READ      ZZGU;
      //RESP      ZZGUx;
      //NOTE      x=0: AGC OFF, x=1: LONG, x=2: SLOW, x=3: MEDIUM, x=4: FAST
      //ENDDEF
      RXCHECK(1,
      if (command[4] == ';') {
      snprintf(reply,  sizeof(reply), "ZZGU%d;", receiver[1]->agc);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
      int agc = atoi(&command[4]);
        // update RX2 AGC
        RXCHECK(1,
                receiver[1]->agc = agc;
                rx_set_agc(receiver[1]);
                g_idle_add(ext_vfo_update, NULL);
               )
      }
             )
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'H': //ZZHx
  case 'I': //ZZIx
  case 'K': //ZZKx
    implemented = FALSE;
    break;

  case 'L': //ZZLx
    switch (command[3]) {
    case 'A': //ZZLA

      //CATDEF    ZZLA
      //DESCR     Set/Read RX1 volume (AF slider)
      //SET       ZZLAxxx;
      //READ      ZZLA;
      //RESP      ZZLAxxx;
      //NOTE      x = 0...100, mapped logarithmically to -40 ... 0 dB.
      //ENDDEF
      if (command[4] == ';') {
        // send reply back
        snprintf(reply,  sizeof(reply), "ZZLA%03d;", (int)(receiver[0]->volume * 100.0));
        send_resp(client->fd, reply) ;
      } else {
        int gain = atoi(&command[4]);

        // gain is 0..100
        if (gain < 2) {
          receiver[0]->volume = -40.0;
        } else {
          receiver[0]->volume = 20.0 * log10(0.01 * (double) gain);
        }

        set_af_gain(0, receiver[0]->volume);
      }

      break;

    case 'C': //ZZLC
      //CATDEF    ZZLC
      //DESCR     Set/Read RX2 volume (AF slider)
      //SET       ZZLCxxx;
      //READ      ZZLC;
      //RESP      ZZLCxxx;
      //NOTE      x = 0...100, mapped logarithmically to -40 ... 0 dB.
      //ENDDEF
      RXCHECK(1,
      if (command[4] == ';') {
      // send reply back
      snprintf(reply,  sizeof(reply), "ZZLC%03d;", (int)(255.0 * pow(10.0, 0.05 * receiver[1]->volume)));
        send_resp(client->fd, reply) ;
      } else {
        int gain = atoi(&command[4]);

        // gain is 0..100
        if (gain < 2) {
          receiver[1]->volume = -40.0;
        } else {
          receiver[1]->volume = 20.0 * log10(0.01 * (double) gain);
        }

        set_af_gain(1, receiver[1]->volume);
      }
             )
      break;

    case 'I': //ZZLI

      //CATDEF    ZZLI
      //DESCR     Set/Read PURESIGNAL status
      //SET       ZZLIx;
      //READ      ZZLI;
      //RESP      ZZLIx;
      //NOTE      x=0: PURESIGNAL disabled, x=1: enabled.
      //ENDDEF
      if (can_transmit) {
        if (command[4] == ';') {
          // send reply back
          snprintf(reply,  sizeof(reply), "ZZLI%d;", transmitter->puresignal);
          send_resp(client->fd, reply) ;
        } else {
          int ps = atoi(&command[4]);
          tx_ps_onoff(transmitter, ps);
        }

        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'M': //ZZMx
    switch (command[3]) {
    case 'A':  //ZZMA

      //CATDEF    ZZMA
      //DESCR     Mute/Unmute RX1
      //SET       ZZMAx;
      //READ      ZZMA;
      //RESP      ZZMAx;
      //NOTE      x=0: RX1 not muted, x=1: muted.
      //ENDDEF
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZMA%d;", receiver[0]->mute_radio);
        send_resp(client->fd, reply) ;
      } else {
        int mute = atoi(&command[4]);
        receiver[0]->mute_radio = mute;
      }

      break;

    case 'B': //ZZMB
      //CATDEF    ZZMB
      //DESCR     Mute/Unmute RX2
      //SET       ZZMBx;
      //READ      ZZMB;
      //RESP      ZZMBx;
      //NOTE      x=0: RX2 not muted, x=1: muted.
      //ENDDEF
      RXCHECK(1,
      if (command[4] == ';') {
      snprintf(reply,  sizeof(reply), "ZZMA%d;", receiver[1]->mute_radio);
        send_resp(client->fd, reply) ;
      } else {
        int mute = atoi(&command[4]);
        receiver[1]->mute_radio = mute;
      }
             )
      break;

    case 'D': //ZZMD

      //CATDEF    ZZMD
      //DESCR     Set/Read VFO-A modes
      //SET       ZZMDxx;
      //READ      ZZMD;
      //RESP      ZZMDxx;
      //NOTE      Modes: LSB (x=0), USB (x=1), DSB (x=3), CWL (x=4)
      //CONT      CWU (x=5), FMN (x=6), AM (x=7), DIGU (x=7)
      //CONT      SPEC (x=8), DIGL (x=9), SAM (x=10), DRM (x=11)
      //ENDDEF
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZMD%02d;", vfo[VFO_A].mode);
        send_resp(client->fd, reply);
      } else if (command[6] == ';') {
        vfo_id_mode_changed(VFO_A, atoi(&command[4]));
      }

      break;

    case 'E': //ZZME

      //CATDEF    ZZME
      //DESCR     Set/Read VFO-B modes
      //SET       ZZMEx;
      //READ      ZZME;
      //RESP      ZZMEx;
      //NOTE      x encodes the mode (see ZZMD command)
      //ENDDEF
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZMD%02d;", vfo[VFO_B].mode);
        send_resp(client->fd, reply);
      } else if (command[6] == ';') {
        vfo_id_mode_changed(VFO_A, atoi(&command[4]));
      }

      break;

    case 'G': //ZZMG

      //CATDEF    ZZMG
      //DESCR     Set/Read Mic gain (Mic gain slider)
      //SET       ZZMGxxx;
      //READ      ZZMG;
      //RESP      ZZMGxxx;
      //NOTE      x 0-70 mapped to -12 ... +50 dB
      //ENDDEF
      if (can_transmit) {
        if (command[4] == ';') {
          snprintf(reply,  sizeof(reply), "ZZMG%03d;", (int)((transmitter->mic_gain + 12.0) * 1.129));
          send_resp(client->fd, reply);
        } else if (command[7] == ';') {
          int val = atoi(&command[4]);
          set_mic_gain(((double) val * 0.8857) - 12.0);
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'L': //ZZML

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply),
                 "ZZML LSB00: USB01: DSB02: CWL03: CWU04: FMN05:  AM06:DIGU07:SPEC08:DIGL09: SAM10: DRM11;");
        send_resp(client->fd, reply);
      }

      break;

    case 'O': //ZZMO

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      // set/read MON status
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZMO%d;", 0);
        send_resp(client->fd, reply);
      }

      break;

    case 'R': //ZZMR

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZMR%d;", active_receiver->smetermode + 1);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        int val = atoi(&command[4]) - 1;

        switch (val) {
        case 0:
          active_receiver->smetermode = SMETER_PEAK;
          break;

        case 1:
          active_receiver->smetermode = SMETER_AVERAGE;
          break;
        }
      }

      break;

    case 'T': //ZZMT

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZMT%02d;", 1); // forward power
        send_resp(client->fd, reply);
      } else {
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'N': //ZZNx
    switch (command[3]) {
    case 'A': //ZZNA

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZNA%d;", (receiver[0]->nb == 1));
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        if (atoi(&command[4])) { receiver[0]->nb = 1; }

        rx_set_noise(receiver[0]);
      }

      break;

    case 'B': //ZZNB

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZNB%d;", (receiver[0]->nb == 2));
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        if (atoi(&command[4])) { receiver[0]->nb = 2; }

        rx_set_noise(receiver[0]);
      }

      break;

    case 'C': //ZZNC

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (receivers == 2) {
        if (command[4] == ';') {
          snprintf(reply,  sizeof(reply), "ZZNC%d;", (receiver[1]->nb == 1));
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          if (atoi(&command[4])) { receiver[1]->nb = 1; }

          rx_set_noise(receiver[1]);
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'D': //ZZND

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (receivers == 2) {
        if (command[4] == ';') {
          snprintf(reply,  sizeof(reply), "ZZND%d;", (receiver[1]->nb == 2));
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          if (atoi(&command[4])) { receiver[1]->nb = 2; }

          rx_set_noise(receiver[1]);
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'N': //ZZNN

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZNN%d;", receiver[0]->snb);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        receiver[0]->snb = atoi(&command[4]);
        rx_set_noise(receiver[0]);
      }

      break;

    case 'O': //ZZNO

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (receivers == 2) {
        if (command[4] == ';') {
          snprintf(reply,  sizeof(reply), "ZZNO%d;", receiver[1]->snb);
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          receiver[1]->snb = atoi(&command[4]);
          rx_set_noise(receiver[1]);
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'R': //ZZNR

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (receivers == 2) {
        if (command[4] == ';') {
          snprintf(reply,  sizeof(reply), "ZZNR%d;", (receiver[0]->nr == 1));
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          if (atoi(&command[4])) { receiver[0]->nr = 1; }

          rx_set_noise(receiver[0]);
        }
      }

      break;

    case 'S': //ZZNS

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZNS%d;", (receiver[0]->nr == 2));
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        if (atoi(&command[4])) { receiver[0]->nr = 2; }

        rx_set_noise(receiver[0]);
      }

      break;

    case 'T': //ZZNT

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZNT%d;", receiver[0]->anf);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        if (atoi(&command[4])) { receiver[0]->anf = 1; }

        rx_set_noise(receiver[0]);
      }

      break;

    case 'U': //ZZNU

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (receivers == 2) {
        if (command[4] == ';') {
          snprintf(reply,  sizeof(reply), "ZZNU%d;", receiver[1]->anf);
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          if (atoi(&command[4])) { receiver[1]->anf = 1; }

          rx_set_noise(receiver[1]);
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'V': //ZZNV

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (receivers == 2) {
        if (command[4] == ';') {
          snprintf(reply,  sizeof(reply), "ZZNV%d;", (receiver[1]->nr == 1));
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          if (atoi(&command[4])) { receiver[1]->nr = 1; }

          rx_set_noise(receiver[1]);
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'W': //ZZNW

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (receivers == 2) {
        if (command[4] == ';') {
          snprintf(reply,  sizeof(reply), "ZZNW%d;", (receiver[1]->nr == 2));
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          if (atoi(&command[4])) { receiver[1]->nr = 2; }

          rx_set_noise(receiver[1]);
        }
      } else {
        implemented = FALSE;
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'O': //ZZOx
    implemented = FALSE;
    break;

  case 'P': //ZZPx
    switch (command[3]) {
    case 'A': //ZZPA

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        int a = adc[receiver[0]->adc].attenuation;

        if (a == 0) {
          a = 1;
        } else if (a <= -30) {
          a = 4;
        } else if (a <= -20) {
          a = 0;
        } else if (a <= -10) {
          a = 2;
        } else {
          a = 3;
        }

        snprintf(reply,  sizeof(reply), "ZZPA%d;", a);
        send_resp(client->fd, reply);
      } else if (command[5] == ';' && have_rx_att) {
        int a = atoi(&command[4]);

        switch (a) {
        case 0:
          adc[receiver[0]->adc].attenuation = -20;
          break;

        case 1:
          adc[receiver[0]->adc].attenuation = 0;
          break;

        case 2:
          adc[receiver[0]->adc].attenuation = -10;
          break;

        case 3:
          adc[receiver[0]->adc].attenuation = -20;
          break;

        case 4:
          adc[receiver[0]->adc].attenuation = -30;
          break;

        default:
          adc[receiver[0]->adc].attenuation = 0;
          break;
        }
      }

      break;

    case 'Y': // ZZPY

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZPY%d;", receiver[0]->zoom);
        send_resp(client->fd, reply);
      } else if (command[7] == ';') {
        int zoom = atoi(&command[4]);
        set_zoom(0, zoom);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'Q': //ZZQx
    implemented = FALSE;
    break;

  case 'R': //ZZRx
    switch (command[3]) {
    case 'C': //ZZRC

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        schedule_action(RIT_CLEAR, ACTION_PRESSED, 0);
      }

      break;

    case 'D': //ZZRD

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        vfo_id_rit_incr(VFO_A, -vfo[VFO_A].rit_step);
      } else if (command[9] == ';') {
        // set RIT frequency
        vfo_id_rit_value(VFO_A, atoi(&command[4]));
      }

      break;

    case 'F': //ZZRF

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZRF%+5lld;", vfo[VFO_A].rit);
        send_resp(client->fd, reply);
      } else if (command[9] == ';') {
        vfo_id_rit_value(VFO_A, atoi(&command[4]));
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'M': //ZZRM

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[5] == ';') {
        snprintf(reply,  sizeof(reply), "ZZRM%d%20d;", active_receiver->smetermode, (int)receiver[0]->meter);
        send_resp(client->fd, reply);
      }

      break;

    case 'S': //ZZRS

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZRS%d;", receivers == 2);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        int state = atoi(&command[4]);

        if (state) {
          radio_change_receivers(2);
        } else {
          radio_change_receivers(1);
        }
      }

      break;

    case 'T': //ZZRT

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZRT%d;", vfo[VFO_A].rit_enabled);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        vfo_id_rit_onoff(VFO_A, SET(atoi(&command[4])));
      }

      break;

    case 'U': //ZZRU

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        vfo_id_rit_incr(VFO_A, vfo[VFO_A].rit_step);
      } else if (command[9] == ';') {
        vfo_id_rit_value(VFO_A,  atoi(&command[4]));
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'S': //ZZSx
    switch (command[3]) {
    case 'A': //ZZSA

      //CATDEF    ZZSA
      //DESCR     Move down VFO-A frequency one step
      //SET       ZZSA;
      //NOTE      VFO-A frequency moved down by the current step size
      //ENDDEF
      if (command[4] == ';') {
        vfo_id_step(VFO_A, -1);
      }

      break;

    case 'B': //ZZSB

      //CATDEF    ZZSB
      //DESCR     Move up VFO-A frequency one step
      //SET       ZZSB;
      //NOTE      VFO-A frequency moved up by the current step size
      //ENDDEF
      if (command[4] == ';') {
        vfo_id_step(VFO_A, 1);
      }

      break;

    case 'G': //ZZSG

      //CATDEF    ZZSG
      //DESCR     Move down VFO-B frequency one step
      //SET       ZZSG;
      //NOTE      VFO-B frequency moved down by the current step size
      //ENDDEF
      if (command[4] == ';') {
        vfo_id_step(VFO_B, -1);
      }

      break;

    case 'H': //ZZSH

      //CATDEF    ZZSH
      //DESCR     Move up VFO-B frequency one step
      //SET       ZZSG;
      //NOTE      VFO-B frequency moved up by the current step size
      //ENDDEF
      if (command[4] == ';') {
        vfo_id_step(VFO_B, 1);
      }

      break;

    case 'M': //ZZSM

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[5] == ';') {
        int v = atoi(&command[4]);

        if (v >= 0 && v < receivers) {
          double m = receiver[v]->meter;
          m = fmax(-140.0, m);
          m = fmin(-10.0, m);
          snprintf(reply,  sizeof(reply), "ZZSM%d%03d;", v, (int)((m + 140.0) * 2));
          send_resp(client->fd, reply);
        } else {
          implemented = FALSE;
        }
      }

      break;

    case 'P': //ZZSP

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZSP%d;", split);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        int val = atoi(&command[4]);
        radio_set_split(val);
      }

      break;

    case 'W': //ZZSW

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZSW%d;", split);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        int val = atoi(&command[4]);
        radio_set_split(val);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'T': //ZZTx
    switch (command[3]) {
    case 'U': //ZZTU

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZTU%d;", tune);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        radio_set_tune(atoi(&command[4]));
      }

      break;

    case 'X': //ZZTX

      //CATDEF    ZZTX
      //DESCR     Get/Set MOX status
      //SET       ZZTXx;
      //READ      ZZTX;
      //RESP      ZZTXx;
      //NOTE      x=1: MOX on, x=0: off.
      //ENDDEF
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZTX%d;", mox);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        radio_set_mox(atoi(&command[4]));
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'U': //ZZUx
    switch (command[3]) {
    case 'U': //ZZUT

      //CATDEF    ZZUT
      //DESCR     Get/Set TwoTone status
      //SET       ZZUTx;
      //READ      ZZUT;
      //RESP      ZZTXx;
      //NOTE      x=1: TwoTone on, x=0: TwoTone off.
      //ENDDEF
      if (can_transmit) {
        if (command[4] == ';') {
          snprintf(reply,  sizeof(reply), "ZZUT%d;", transmitter->twotone);
          send_resp(client->fd, reply) ;
        } else if (command[5] == ';') {
          radio_set_twotone(transmitter, atoi(&command[4]));
        }
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'V': //ZZVx
    switch (command[3]) {
    case 'L': //ZZVL
      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      locked = command[4] == '1';
      g_idle_add(ext_vfo_update, NULL);
      break;

    case 'S': { //ZZVS
      //CATDEF    ZZVS
      //DESCR     Swap VFO A and B
      //SET       ZZVS;
      //NOTE      The contents (frequencies, CTUN mode, filters, etc.) of VFO A and B are exchanged.
      //ENDDEF
      int i = atoi(&command[4]);

      if (i == 0) {
        vfo_a_to_b();
      } else if (i == 1) {
        vfo_b_to_a();
      } else {
        vfo_a_swap_b();
      }
    }
    break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'W': //ZZWx
    implemented = FALSE;
    break;

  case 'X': //ZZXx
    switch (command[3]) {
    case 'C': //ZZXC
      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      schedule_action(XIT_CLEAR, ACTION_PRESSED, 0);
      break;

    case 'F': //ZZXF

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZXT%+05lld;", vfo[vfo_get_tx_vfo()].xit);
        send_resp(client->fd, reply) ;
      } else if (command[9] == ';') {
        vfo_xit_value(atoi(&command[4]));
      }

      break;

    case 'N': //ZZXN

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        int status = ((receiver[0]->agc) & 0x03);
        int a = adc[receiver[0]->adc].attenuation;

        if (a == 0) {
          a = 1;
        } else if (a <= -30) {
          a = 4;
        } else if (a <= -20) {
          a = 0;
        } else if (a <= -10) {
          a = 2;
        } else {
          a = 3;
        }

        status = status | ((a & 0x03) << 3);

        if (receiver[0]->squelch_enable) { status |=  0x0040; }

        if (receiver[0]->nb == 1) { status |=  0x0080; }

        if (receiver[0]->nb == 2) { status |=  0x0100; }

        if (receiver[0]->nr == 1) { status |=  0x0200; }

        if (receiver[0]->nr == 2) { status |=  0x0400; }

        if (receiver[0]->snb) { status |=  0x0800; }

        if (receiver[0]->anf) { status |=  0x1000; }

        snprintf(reply,  sizeof(reply), "ZZXN%04d;", status);
        send_resp(client->fd, reply);
      }

      break;

    case 'O': //ZZXO

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (receivers == 2) {
        if (command[4] == ';') {
          int status = ((receiver[1]->agc) & 0x03);
          int a = adc[receiver[1]->adc].attenuation;

          if (a == 0) {
            a = 1;
          } else if (a <= -30) {
            a = 4;
          } else if (a <= -20) {
            a = 0;
          } else if (a <= -10) {
            a = 2;
          } else {
            a = 3;
          }

          status = status | ((a & 0x03) << 3);

          if (receiver[1]->squelch_enable) { status |=  0x0040; }

          if (receiver[1]->nb == 1) { status |=  0x0080; }

          if (receiver[1]->nb == 2) { status |=  0x0100; }

          if (receiver[1]->nr == 1) { status |=  0x0200; }

          if (receiver[1]->nr == 2) { status |=  0x0400; }

          if (receiver[1]->snb) { status |=  0x0800; }

          if (receiver[1]->anf) { status |=  0x1000; }

          snprintf(reply,  sizeof(reply), "ZZXO%04d;", status);
          send_resp(client->fd, reply);
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'S': //ZZXS

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZXS%d;", vfo[vfo_get_tx_vfo()].xit_enabled);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        vfo[vfo_get_tx_vfo()].xit_enabled = atoi(&command[4]);
        schedule_high_priority();
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'V': //ZZXV

      //CATDEF    ZZXV
      //DESCR     Get extended status information
      //READ      ZZVS;
      //RESP      ZZVSxxxx;
      //NOTE      Status is reported bit-wise in the status word x=0-1023.
      //NOTE      Bit 0: RIT; Bit 1: Lock, Bit2: Lock, Bit3: Split,
      //NOTE      Bit 4: VFO-A CTUN, Bit 5: VFO-B CTUN, Bit 6: MOX,
      //NOTE      Bit 7: TUNE, Bit 8: XIT, Bit 9: always cleared.
      //ENDDEF
      if (command[4] == ';') {
        int status = 0;

        if (vfo[VFO_A].rit_enabled) {
          // cppcheck-suppress badBitmaskCheck
          status = status | 0x01;
        }

        if (locked) {
          status = status | 0x02;
          status = status | 0x04;
        }

        if (split) {
          status = status | 0x08;
        }

        if (vfo[VFO_A].ctun) {
          status = status | 0x10;
        }

        if (vfo[VFO_B].ctun) {
          status = status | 0x20;
        }

        if (mox) {
          status = status | 0x40;
        }

        if (tune) {
          status = status | 0x80;
        }

        if (vfo[vfo_get_tx_vfo()].xit_enabled) {
          status = status | 0x100;
        }

        snprintf(reply,  sizeof(reply), "ZZXV%03d;", status);
        send_resp(client->fd, reply);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'Y': //ZZYx
    switch (command[3]) {
    case 'R': //ZZYR

      //CATDEF    ZZYR
      //DESCR     Get/Set active receiver
      //SET       ZZYRx;
      //READ      ZZYR;
      //RESP      ZZYRx;
      //NOTE      The active receiver is either RX1 (x=0) or RX2 (x=1).
      //ENDDEF
      if (command[4] == ';') {
        snprintf(reply,  sizeof(reply), "ZZYR%01d;", active_receiver->id);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        int v = atoi(&command[4]);

        if (v >= 0 && v < receivers) {
          schedule_action(v == 0 ? RX1 : RX2, ACTION_PRESSED, 0);
        } else {
          implemented = FALSE;
        }

        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'Z': //ZZZx
    switch (command[3]) {
    case 'D': //ZZZD

      //CATDEF    ZZZD
      //DESCR     Move down frequency of active receiver
      //SET       ZZZDxx;
      //NOTE      ANDROMEDA extension. x = number of VFO steps.
      //NOTE      For x>10, the number of VFO steps is multiplied with
      //CONT      a speed-up factor that increases up to 4 at x=30
      //CONT      (corresponds to 3 turns of the VFO dial per second).
      //CONT      This implements an over-proportional tuning speed if
      //CONT      turning the VFO knob faster and faster.
      //ENDDEF
      if (command[6] == ';') {
        int steps = 10 * (command[4] - '0') + (command[5] - '0');

        if (steps <= 30) {
          steps = andromeda_vfo_speedup[steps];
        } else {
          steps *= andromeda_vfo_speedup[31];
        }

        schedule_action(VFO, ACTION_RELATIVE, -steps);
      } else {
        // unexpected command format
        implemented = FALSE;
      }

      break;

    case 'E': //ZZZE ANDROMEDA command

      //CATDEF    ZZZE
      //DESCR     Handle ANDROMEDA encoders
      //SET       ZZZExxy;
      //NOTE      ANDROMEDA extension.
      //NOTE      x encodes the encoder and the direction.
      //NOTE      x= 1-20 maps to encoder 1-20, clockwise
      //NOTE      x=51-70 maps to encoder 1-20, counter clockwise
      //NOTE      y=0-9 is the number of ticks
      //NOTE
      //ENDDEF
      if (command[7] == ';') {
        int v, p;
        p = 10 * (command[4] - '0') + (command[5] - '0');
        v = command[6] - '0';

        if (p > 50) {
          p -= 50;
          v = -v;
        }

        if (v == 0) {
          // This should not happen, but if, just do nothing
          break;
        }

        //
        // At this place, p is the encoder number (1...20) and
        // v the number of ticks (-9 ... 9)
        //

        if (client->andromeda_type == 1) {
          andromeda_execute_encoder(p, v);
        } else {
          if (g2panel_menu_is_open) {
            g2panel_change_command(client->andromeda_type, CONTROLLER_ENCODER,
                                   client->buttonvec, client->encodervec, p);
          } else {
            g2panel_execute_encoder(client->andromeda_type, client->encodervec, p, v);
          }
        }
      } else {
        // unexpected command format
        implemented = FALSE;
      }

      break;

    case 'I': //ZZZI ANDROMEDA info
      //CATDEF    ZZZI
      //DESCR     ANDROMEDA reports
      //RESP      ZZZIxxy;
      //NOTE      Automatic generated response for ANDROMEDA controller.
      //NOTE      The LED with number x shall be switched on (y=1)
      //NOTE      or off (y=0).
      //ENDDEF
      implemented = FALSE;  // this command should never ARRIVE from the console
      break;

    case 'P': //ZZZP ANDROMEDA command

      //CATDEF    ZZZP
      //DESCR     Handle ANDROMEDA push-buttons
      //SET       ZZZPxxy;
      //NOTE      ANDROMEDA extension.
      //NOTE      x encodes the button and y means released (y=0),
      //NOTE      pressed(y=1) or pressed for a longer time (y=2).
      //ENDDEF
      if (command[7] == ';') {
        int p = 10 * (command[4] - '0') + (command[5] - '0');
        int v = (command[6] - '0');

        //
        // The Andromeda console will send a v=0 --> v=1 --> v=0 sequence for a short press,
        // so we have characteristic transitions tr01 (0-->1, upon pressing), tr12 (1-->2, after waiting),
        // tr10 (1-->0, upon release) and tr20 (2-->0, upon release). For any button, either the sequence
        // {tr01,tr01} is generated (short press) or the sequence {tr01,tr12,tr20} (long press).
        //
        // We have to distinguish "normal" buttons from "long" buttons. "normal" buttons make no difference
        // between a short and a long press but "long" buttons may generate different actions for short
        // and long presses.
        //
        // "Normal" buttons should generate a "ACTION_PRESSED" upon tr01 and (if required) a "ACTION_RELEASED" upon (tr10 || tr20).
        // "Long" buttons generate a "ACTION_PRESSED" for the short-press event upon tr10,
        // and a "ACTION_PRESSED" for the long-press event upon tr12.
        //
        // ATTENTION: no RELEASE event is ever triggered for a "long" button. Such events are currently required
        //            only for PTT, RIT_PLUS, RIT_MINS, XIT_PLUS, XIT_MINUS, and CW keyer actions, which may be associated
        //            to the "function keys" F1-F8 on the original ANDROMEDA console.
        //            In all other cases, there is no need to bother the system with RELEASE events.
        //
        // NOTE: Rick's code for the original ANDROMEDA console went to andromeda.c
        //
        //

        if (client->andromeda_type == 1) {
          client->shift = andromeda_execute_button(v, p);
        } else {
          //
          // "generic" ANDROMDA push-button section
          //
          int tr01, tr10, tr12, tr20;
          tr01 = 0;  // indicates a v=0 --> v=1 transision
          tr12 = 0;  // indicates a v=1 --> v=2 transision
          tr10 = 0;  // indicates a v=1 --> v=0 transision
          tr20 = 0;  // indicates a v=2 --> v=0 transision

          if (client->last_v == 0 && v == 1) { tr01 = 1; }

          if (client->last_v == 1 && v == 2) { tr12 = 1; }

          if (client->last_v == 1 && v == 0) { tr10 = 1; }

          if (client->last_v == 2 && v == 0) { tr20 = 1; }

          client->last_v = v;

          if (g2panel_menu_is_open) {
            //
            // It is enough to "fire" this upon initial press
            //
            if (tr01) {
              g2panel_change_command(client->andromeda_type, CONTROLLER_SWITCH,
                                     client->buttonvec, client->encodervec, p);
            }
          } else {
            g2panel_execute_button(client->andromeda_type, client->buttonvec, p, tr01, tr10, tr12, tr20);
          }
        }

        //
        // Schedule LED update, in case the state has changed
        //
        g_idle_add(andromeda_oneshot_handler, (gpointer) client);
      } else {
        // all ANDROMEDA types, unexpected command format
        implemented = FALSE;
      }

      break;

    case 'S': //ZZZS ANDROMEDA command

      //CATDEF    ZZZS
      //DESCR     Log ANDROMEDA version
      //SET       ZZZSxxyyzzz;
      //NOTE      ANDROMEDA extension.
      //NOTE      The ANDROMEDA type (x), hardware (y) and
      //CONT      software (z) version is printed in the log file.
      //CONT      The type (x) sent by a client does affect the
      //CONT      processing of ZZZE and ZZZP commands from that client.
      //CONT      Only the cases x=1 (original ANDROMEDA console)
      //CONT      and x=5 (G2 Ultra console) are implemented.
      //ENDDEF
      if (command[11] == ';') {
        //
        // Besides logging, store the ANDROMEDA type in the client data structure
        //
        client->andromeda_type = 10 * (command[4] - '0') + (command[5] - '0');
        t_print("RIGCTL:INFO: Andromeda Client: Type:%c%c h/w:%c%c s/w:%c%c%c\n",
                command[4], command[5],
                command[6], command[7], command[8], command[9], command[10]);

        if (client->andromeda_type == 4 || client->andromeda_type == 5) {
          //
          // Initialise commands.
          //
          if (client->buttonvec) { g_free(client->buttonvec); }

          if (client->encodervec) { g_free(client->encodervec); }

          client->buttonvec  = g2panel_default_buttons(client->andromeda_type);
          client->encodervec = g2panel_default_encoders(client->andromeda_type);
          g2panelRestoreState(client->andromeda_type, client->buttonvec, client->encodervec);

          //
          // This takes care the G2panel menu is shown in the main menu
          //
          if (controller == NO_CONTROLLER) { controller = G2_V2; }
        }
      }

      break;

    case 'U': //ZZZU ANDROMEDA command operating on VFO of active receiver

      //CATDEF    ZZZU
      //DESCR     Move up frequency of active receiver
      //SET       ZZZUxx;
      //NOTE      ANDROMEDA extension. x = number of steps.
      //NOTE      For x>10, the number of VFO steps is multiplied with
      //CONT      a speed-up factor that increases up to 4 at x=30
      //CONT      (corresponds to 3 turns of the VFO dial per second).
      //CONT      This implements an over-proportional tuning speed if
      //CONT      turning the VFO knob faster and faster.
      //ENDDEF
      if (command[6] == ';') {
        int steps = 10 * (command[4] - '0') + (command[5] - '0');

        if (steps <= 30) {
          steps = andromeda_vfo_speedup[steps];
        } else {
          steps *= andromeda_vfo_speedup[31];
        }

        schedule_action(VFO, ACTION_RELATIVE, steps);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  default:
    implemented = FALSE;
    break;
  }

  return implemented;
}

// called with g_idle_add so that the processing is running on the main thread
static int parse_cmd(void *data) {
  COMMAND *info = (COMMAND *)data;
  CLIENT *client = info->client;
  char *command = info->command;
  char reply[256];
  reply[0] = '\0';
  gboolean implemented = TRUE;

  switch (command[0]) {
  case '#':

    //CATDEF    \#S
    //DESCR     Shutdown Console
    //SET       \#S;
    //ENDDEF
    if (command[1] == 'S' && command[2] == ';') {
      schedule_action(SHUTDOWN, ACTION_PRESSED, 0);
    } else {
      implemented = FALSE;
    }

    break;

  case 'A':
    switch (command[1]) {
    case 'C': //AC
      // set/read internal atu status
      implemented = FALSE;
      break;

    case 'G': //AG

      //CATDEF    AG
      //DESCR     Sets/Reads audio volume (AF slider)
      //SET       AGxyyy;
      //READ      AGx;
      //RESP      AGxyyy;
      //NOTE      x=0 sets RX1 audio volume, x=1 sets RX2 audio volume.
      //NOTE      y is 0...255 and mapped logarithmically to the volume -40...0 dB
      //ENDDEF
      if (command[3] == ';') {
        int id = SET(command[2] == '1');
        RXCHECK(id,
                snprintf(reply,  sizeof(reply), "AG%1d%03d;", id, (int)(255.0 * pow(10.0, 0.05 * receiver[id]->volume)));
                send_resp(client->fd, reply);
               )
      } else if (command[6] == ';') {
        int id = SET(command[2] == '1');
        int gain = atoi(&command[3]);
        double vol = (gain < 3) ? -40.0 : 20.0 * log10((double) gain / 255.0);
        RXCHECK(id, receiver[id]->volume = vol; set_af_gain(0, receiver[id]->volume));
      }

      break;

    case 'I': //AI

      //CATDEF    AI
      //DESCR     Sets/Reads auto reporting status
      //SET       AIx;
      //READ      AI;
      //RESP      AIx;
      //NOTE      x=0: auto-reporting disabled, x>0: enabled.
      //NOTE      Auto-reporting is affected for the client that sends this command.
      //CONT      For x=1, only frequency changes are sent via FA/FB commands.
      //CONT      For x>1, mode changes are also sent via MD commands.
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "AI%d;", client->auto_reporting);
        send_resp(client->fd, reply) ;
      } else if (command[3] == ';') {
        client->auto_reporting = command[2] - '0';

        if (client->auto_reporting < 0) { client->auto_reporting = 0; }

        if (client->auto_reporting > 3) { client->auto_reporting = 3; }
      }

      break;

    case 'L': // AL
      // set/read Auto Notch level
      implemented = FALSE;
      break;

    case 'M': // AM
      // set/read Auto Mode
      implemented = FALSE;
      break;

    case 'N': // AN
      // set/read Antenna Connection
      implemented = FALSE;
      break;

    case 'S': // AS
      // set/read Auto Mode Function Parameters
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'B':
    switch (command[1]) {
    case 'C': //BC
      // set/read Beat Canceller
      implemented = FALSE;
      break;

    case 'D': //BD
      //CATDEF    BD
      //DESCR     VFO-A Band down
      //SET       BD;
      //NOTE      Wraps from the lowest to the highest band.
      //ENDDEF
      band_minus(receiver[0]->id);
      break;

    case 'P': //BP
      // set/read Manual Beat Canceller frequency
      implemented = FALSE;
      break;

    case 'U': //BU
      //CATDEF    BU
      //DESCR     VFO-A Band up
      //SET       BU;
      //NOTE      Wraps from the highest to the lowest band.
      //ENDDEF
      band_plus(receiver[0]->id);
      break;

    case 'Y': //BY
      // read busy signal
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'C':
    switch (command[1]) {
    case 'A': //CA
      // set/read CW Auto Tune
      implemented = FALSE;
      break;

    case 'G': //CG
      // set/read Carrier Gain
      implemented = FALSE;
      break;

    case 'I': //CI
      // sets the current frequency to the CALL Channel
      implemented = FALSE;
      break;

    case 'M': //CM
      // sets/reads the Packet Cluster Tune function
      implemented = FALSE;
      break;

    case 'N': //CN

      //CATDEF    CN
      //DESCR     Sets/Reads the CTCSS frequency
      //SET       CNxx;
      //READ      CN;
      //RESP      CNxx;
      //NOTE      x =  1...38. CTCSS frequencies in Hz are:
      //CONT      67.0 (x=1),  71.9 (x=2),  74.4 (x=3),  77.0 (x=4),
      //CONT      79.7 (x=5),  82.5 (x=6),  85.4 (x=7),  88.5 (x=8),
      //CONT      91.5 (x=9),  94.8 (x=10), 97.4 (x=11), 100.0 (x=12)
      //CONT      103.5 (x=13), 107.2 (x=14), 110.9 (x=15), 114.8 (x=16)
      //CONT      118.8 (x=17), 123.0 (x=18), 127.3 (x=19), 131.8 (x=20)
      //CONT      136.5 (x=21), 141.3 (x=22), 146.2 (x=23), 151.4 (x=24)
      //CONT      156.7 (x=25), 162.2 (x=26), 167.9 (x=27), 173.8 (x=28)
      //CONT      179.9 (x=29), 186.2 (x=30), 192.8 (x=31), 203.5 (x=32)
      //CONT      210.7 (x=33), 218.1 (x=34), 225.7 (x=35), 233.6 (x=36)
      //CONT      241.8 (x=37), 250.3 (x=38).
      //ENDDEF
      // sets/reads CTCSS function (frequency)
      if (can_transmit) {
        if (command[2] == ';') {
          snprintf(reply,  sizeof(reply), "CN%02d;", transmitter->ctcss + 1);
          send_resp(client->fd, reply) ;
        } else if (command[4] == ';') {
          transmitter->ctcss = atoi(&command[2]) - 1;
          tx_set_ctcss(transmitter);
          g_idle_add(ext_vfo_update, NULL);
        }
      }

      break;

    case 'T': //CT

      //CATDEF    CT
      //DESCR     Enable/Disable CTCSS
      //SET       CTx;
      //READ      CT;
      //RESP      CTx;
      //NOTE      x = 0: CTCSS off, x=1: on
      //ENDDEF
      if (can_transmit) {
        if (command[2] == ';') {
          snprintf(reply,  sizeof(reply), "CT%d;", transmitter->ctcss_enabled);
          send_resp(client->fd, reply) ;
        } else if (command[3] == ';') {
          transmitter->ctcss_enabled = SET(command[2] == '1');
          tx_set_ctcss(transmitter);
          g_idle_add(ext_vfo_update, NULL);
        }
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'D':
    switch (command[1]) {
    case 'C': //DC
      // set/read TX band status
      implemented = FALSE;
      break;

    case 'N': //DN
      //CATDEF    DN
      //DESCR     VFO-A down  one step
      //SET       DN;
      //NOTE      Parameters may be given, but are ignored.
      //ENDDEF
      vfo_id_step(VFO_A, -1);
      break;

    case 'Q': //DQ
      // set/read DCS function status
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'E':
    switch (command[1]) {
    case 'X': //EX
      // set/read the extension menu
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'F':
    switch (command[1]) {
    case 'A': //FA

      //CATDEF    FA
      //DESCR     Set/Read VFO-A frequency
      //SET       FAxxxxxxxxxxx;
      //READ      FA;
      //RESP      FAxxxxxxxxxxx;
      //NOTE      x in Hz, left-padded with zeroes
      //ENDDEF
      if (command[2] == ';') {
        if (vfo[VFO_A].ctun) {
          snprintf(reply,  sizeof(reply), "FA%011lld;", vfo[VFO_A].ctun_frequency);
        } else {
          snprintf(reply,  sizeof(reply), "FA%011lld;", vfo[VFO_A].frequency);
        }

        send_resp(client->fd, reply) ;
      } else if (command[13] == ';') {
        long long f = atoll(&command[2]);
        vfo_id_set_frequency(VFO_A, f);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'B': //FB

      //CATDEF    FB
      //DESCR     Set/Read VFO-B frequency
      //SET       FBxxxxxxxxxxx;
      //READ      FB;
      //RESP      FBxxxxxxxxxxx;
      //NOTE      x in Hz, left-padded with zeroes
      //ENDDEF
      if (command[2] == ';') {
        if (vfo[VFO_B].ctun) {
          snprintf(reply,  sizeof(reply), "FB%011lld;", vfo[VFO_B].ctun_frequency);
        } else {
          snprintf(reply,  sizeof(reply), "FB%011lld;", vfo[VFO_B].frequency);
        }

        send_resp(client->fd, reply) ;
      } else if (command[13] == ';') {
        long long f = atoll(&command[2]);
        vfo_id_set_frequency(VFO_B, f);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'C': //FC
      // set/read the sub receiver VFO frequency menu
      implemented = FALSE;
      break;

    case 'D': //FD
      // set/read the filter display dot pattern
      implemented = FALSE;
      break;

    case 'R': //FR

      //CATDEF    FR
      //DESCR     Set/Read active receiver
      //SET       FRx;
      //READ      FR;
      //RESP      FRx;
      //NOTE      x = 0 (RX1) or 1 (RX2)
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "FR%d;", active_receiver->id);
        send_resp(client->fd, reply) ;
      } else if (command[3] == ';') {
        int id = SET(command[2] == '1');
        RXCHECK(id, schedule_action(id == 0 ? RX1 : RX2, ACTION_PRESSED, 0));
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'S': //FS
      // set/read the fine tune function status
      implemented = FALSE;
      break;

    case 'T': //FT

      //CATDEF    FT
      //DESCR     Set/Read Split status
      //SET       FTx;
      //READ      FT;
      //RESP      FTx;
      //NOTE      x=0: TX VFO is the VFO controlling the active receiver, x=1: the other VFO.
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "FT%d;", split);
        send_resp(client->fd, reply) ;
      } else if (command[3] == ';') {
        int id = SET(command[2] == '1');
        radio_set_split(id);
      }

      break;

    case 'W': //FW

      //CATDEF    FW
      //DESCR     Set/Read VFO-A filter width (CW, AM, FM)
      //SET       FWxxxx;
      //READ      FW;
      //RESP      FWxxxx;
      //NOTE      When setting, this switches to the Var1 filter and sets its  width to x.
      //CONT      Only valid for CW, FM, AM. Use SH/SL for LSB, USB, DIGL, DIGU.
      //NOTE      For AM, 8kHz filter width (x=0) or  16 kHz (x$\ne$0)
      //NOTE      For FM, 2.5kHz deviation (x=0) or 5 kHz (x$\ne$0)
      //ENDDEF
      if (command[2] == ';') {
        int val = 0;
        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        const FILTER *filter = &mode_filters[vfo[VFO_A].filter];

        switch (vfo[VFO_A].mode) {
        case modeCWL:
        case modeCWU:
          val = filter->low * 2;
          break;

        case modeAM:
        case modeSAM:
          val = filter->low >= -4000;
          break;

        case modeFMN:
          val = vfo[VFO_A].deviation == 5000;
          break;

        default:
          implemented = FALSE;
          break;
        }

        if (implemented) {
          snprintf(reply,  sizeof(reply), "FW%04d;", val);
          send_resp(client->fd, reply) ;
        }
      } else if (command[6] == ';') {
        // make sure filter is filterVar1
        if (vfo[VFO_A].filter != filterVar1) {
          vfo_id_filter_changed(VFO_A, filterVar1);
        }

        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        FILTER *filter = &mode_filters[filterVar1];
        int fw = atoi(&command[2]);
        filter->low = fw;

        switch (vfo[VFO_A].mode) {
        case modeCWL:
        case modeCWU:
          filter->low = fw / 2;
          filter->high = fw / 2;
          break;

        case modeFMN:
          if (fw == 0) {
            filter->low = -5500;
            filter->high = 5500;
            vfo[VFO_A].deviation = 2500;
          } else {
            filter->low = -8000;
            filter->high = 8000;
            vfo[VFO_A].deviation = 5000;
          }

          rx_set_filter(receiver[0]);

          if (can_transmit) {
            tx_set_filter(transmitter);
          }

          g_idle_add(ext_vfo_update, NULL);
          break;

        case modeAM:
        case modeSAM:
          if (fw == 0) {
            filter->low = -4000;
            filter->high = 4000;
          } else {
            filter->low = -8000;
            filter->high = 8000;
          }

          break;

        default:
          implemented = FALSE;
          break;
        }

        if (implemented) {
          vfo_id_filter_changed(VFO_A, filterVar1);
          g_idle_add(ext_vfo_update, NULL);
        }
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'G':
    switch (command[1]) {
    case 'T': //GT

      //CATDEF    GT
      //DESCR     Set/Read RX1 AGC
      //SET       GTxxx;
      //READ      GT;
      //RESP      GTxxx;
      //NOTE      x=0: AGC OFF, x=5: LONG, x=10: SLOW,
      //CONT      x=15: MEDIUM, x=20: FAST.
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "GT%03d;", receiver[0]->agc * 5);
        send_resp(client->fd, reply) ;
      } else if (command[5] == ';') {
        receiver[0]->agc = atoi(&command[2]) / 5;
        rx_set_agc(receiver[0]);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'H':
    switch (command[1]) {
    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'I':
    switch (command[1]) {
    case 'D': //ID
      //CATDEF    ID
      //DESCR     Get radio model ID
      //READ      ID;
      //RESP      IDxxx;
      //NOTE      piHPSDR responds ID019; (so does the Kenwood TS-2000)
      //ENDDEF
      snprintf(reply,  sizeof(reply), "%s", "ID019;");
      send_resp(client->fd, reply);
      break;

    case 'F': { //IF
      //CATDEF    IF
      //DESCR     Get VFO-A Frequency/Mode etc.
      //READ      IF;
      //RESP      IFxxxxxxxxxxxyyyyzzzzzzsbc|ddefghikllm;
      //NOTE      x : VFO-A Frequency (11 digit)
      //NOTE      y : VFO-A step size
      //NOTE      z : VFO-A rit step size
      //NOTE      s : VFO-A rit enabled (0/1)
      //NOTE      b : VFO-A xit enabled (0/1)
      //NOTE      c : always 0
      //NOTE      d : always 0
      //NOTE      e : RX (e=0) or TX (e=1)
      //NOTE      f : mode (TS-2000 encoding, see MD command)
      //NOTE      g : always 0
      //NOTE      h : always 0
      //NOTE      i : Split enabled (i=1) or disabled (i=0)
      //NOTE      k : CTCSS enabled (i=2) or disabled (i=0)
      //NOTE      l : CTCSS frequency (1 - 38), see CN command
      //NOTE      m : always 0
      //ENDDEF
      int mode = ts2000_mode(vfo[VFO_A].mode);
      int tx_xit_en = 0;
      int tx_ctcss_en = 0;
      int tx_ctcss = 0;

      if (can_transmit) {
        tx_xit_en   = vfo[vfo_get_tx_vfo()].xit_enabled;
        tx_ctcss    = transmitter->ctcss + 1;
        tx_ctcss_en = transmitter->ctcss_enabled;
      }

      snprintf(reply,  sizeof(reply), "IF%011lld%04d%+06lld%d%d%d%02d%d%d%d%d%d%d%02d%d;",
               vfo[VFO_A].ctun ? vfo[VFO_A].ctun_frequency : vfo[VFO_A].frequency,
               vfo[VFO_A].step, vfo[VFO_A].rit, vfo[VFO_A].rit_enabled, tx_xit_en,
               0, 0, radio_is_transmitting(), mode, 0, 0, split, tx_ctcss_en ? 2 : 0, tx_ctcss, 0);
      send_resp(client->fd, reply);
    }
    break;

    case 'S': //IS

      //DO NOT DOCUMENT, THIS WILL BE REMOVED
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "%s", "IS 0000;");
        send_resp(client->fd, reply);
      } else {
        implemented = FALSE;
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'J':
    switch (command[1]) {
    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'K':
    switch (command[1]) {
    case 'S': //KS

      //CATDEF    KS
      //DESCR     Set CW speed
      //SET       KSxxx;
      //READ      KS;
      //RESP      KSxxx;
      //NOTE      x (1 - 60) is in wpm
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "KS%03d;", cw_keyer_speed);
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        int speed = atoi(&command[2]);

        if (speed >= 1 && speed <= 60) {
          cw_keyer_speed = speed;
          keyer_update();
          g_idle_add(ext_vfo_update, NULL);
        }
      }

      break;

    case 'Y': //KY

      //CATDEF    KY
      //DESCR     Send Morse/query Morse buffer
      //SET       KYxyyy...yyy;
      //READ      KY;
      //RESP      KYx;
      //NOTE      When setting (sending), x must be a space.
      //CONT      When reading, x=1 indicates buffer space is available, x=0  buffer full
      //NOTE      y: string of up to 24 characters NOT containing ';'.
      //CONT      Trailing blanks are ignored in y, but if it is completely blank it causes an inter-word space.
      //ENDDEF
      if (command[2] == ';') {
        //
        // reply "buffer full" condition if the buffer contains
        // more than (CW_BUF_SIZE-24) characters.
        //
        int avail = cw_buf_in - cw_buf_out;

        if (avail < 0) { avail += CW_BUF_SIZE; }

        if (avail < CW_BUF_SIZE - 24) {
          snprintf(reply,  sizeof(reply), "KY0;");
        } else {
          snprintf(reply,  sizeof(reply), "KY1;");
        }

        send_resp(client->fd, reply);
      } else {
        //
        // Recent versions of Hamlib send CW messages one character at a time.
        // So all trailing blanks have to be removed, and an entirely blank
        // message is interpreted as a inter-word distance.
        // Note we allow variable lengths of incoming messages here, although
        // the standard says they are exactly 24 characters long.
        //
        int j = 3;

        for (unsigned int i = 3; i < strlen(command); i++) {
          if (command[i] == ';') { break; }

          if (command[i] != ' ') { j = i; }
        }

        // j points to the last non-blank character, or to the first blank
        // in an empty string
        for (int i = 3; i <= j; i++) {
          int new = cw_buf_in + 1;

          if (new >= CW_BUF_SIZE) { new = 0; }

          if (new != cw_buf_out) {
            cw_buf[cw_buf_in] = command[i];
            cw_buf_in = new;
          }
        }
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'L':
    switch (command[1]) {
    case 'K': //LK

      //CATDEF    LK
      //DESCR     Set/Read Lock status
      //SET       LKxx;
      //READ      LK;
      //RESP      LKxx;
      //NOTE      When setting, any nonzero x sets lock status.
      //CONT      When reading, x = 00 (not locked) or x = 11 (locked)
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "LK%d%d;", locked, locked);
        send_resp(client->fd, reply);
      } else if (command[4] == ';') {
        locked = atoi(&command[2]);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'M': //LM
      // set/read keyer recording status
      implemented = FALSE;
      break;

    case 'T': //LT
      // set/read ALT fucntion status
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'M':
    switch (command[1]) {
    case 'C': //MC
      // set/read Memory Channel
      implemented = FALSE;
      break;

    case 'D': //MD

      //CATDEF    MD
      //DESCR     Set/Read VFO-A modes
      //SET       MDx;
      //READ      MD;
      //RESP      MDx;
      //NOTE      Kenwood-type  mode  list:
      //CONT      LSB (x=1), USB (x=2), CWU (x=3), FMN (x=4),
      //CONT      AM (x=5), DIGL (x=6), CWL (x=7), DIGU (x=9)
      //ENDDEF
      if (command[2] == ';') {
        int mode = ts2000_mode(vfo[VFO_A].mode);
        snprintf(reply,  sizeof(reply), "MD%d;", mode);
        send_resp(client->fd, reply);
      } else if (command[3] == ';') {
        int mode = wdspmode(atoi(&command[2]));
        vfo_id_mode_changed(VFO_A, mode);
      }

      break;

    case 'F': //MF
      // set/read Menu
      implemented = FALSE;
      break;

    case 'G': //MG

      //CATDEF    MG
      //DESCR     Set/Read Mic gain (Mic gain slider)
      //SET       MGxxx;
      //READ      MG;
      //RESP      MGxxx;
      //NOTE      x 0-100 mapped to -12 ... +50 dB
      //ENDDEF
      if (can_transmit) {
        if (command[2] == ';') {
          snprintf(reply,  sizeof(reply), "MG%03d;", (int)(((transmitter->mic_gain + 12.0) / 62.0) * 100.0));
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          double gain = (double)atoi(&command[2]);
          gain = ((gain / 100.0) * 62.0) - 12.0;

          if (gain < -12.0) { gain = -12.0; }

          if (gain > 50.0) { gain = 50.0; }

          set_mic_gain(gain);
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'L': //ML
      // set/read Monitor Function Level
      implemented = FALSE;
      break;

    case 'O': //MO
      // set/read Monitor Function On/Off
      implemented = FALSE;
      break;

    case 'R': //MR
      // read Memory Channel
      implemented = FALSE;
      break;

    case 'U': //MU
      // set/read Memory Group
      implemented = FALSE;
      break;

    case 'W': //MW
      // Write Memory Channel
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'N':
    switch (command[1]) {
    case 'B': //NB

      //CATDEF    NB
      //DESCR     Set/Read RX1 noise blanker
      //SET       NBx;
      //READ      NB;
      //RESP      NBx;
      //NOTE      x=0: NB off, x=1: NB1 on, x=2: NB2 on
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "NB%d;", receiver[0]->nb);
        send_resp(client->fd, reply);
      } else if (command[3] == ';') {
        receiver[0]->nb = atoi(&command[2]);
        rx_set_noise(receiver[0]);
      }

      break;

    case 'L': //NL
      // set/read noise blanker level
      implemented = FALSE;
      break;

    case 'R': //NR

      //CATDEF    NR
      //DESCR     Set/Read RX1 noise reduction
      //SET       NRx;
      //READ      NR;
      //RESP      NRx;
      //NOTE      x=0: NR off, x=1: NR1 on, x=2: NR2 on
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "NR%d;", receiver[0]->nr);
        send_resp(client->fd, reply);
      } else if (command[3] == ';')  {
        receiver[0]->nr = atoi(&command[2]);
        rx_set_noise(receiver[0]);
      }

      break;

    case 'T': //NT

      //CATDEF    NT
      //DESCR     Set/Read RX1 auto notch filter
      //SET       NTx;
      //READ      NT;
      //RESP      NTx;
      //NOTE      x=0: Automatic Notch Filter off, x=1: on
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "NT%d;", receiver[0]->anf);
        send_resp(client->fd, reply);
      } else if (command[3] == ';') {
        receiver[0]->anf = atoi(&command[2]);
        rx_set_noise(receiver[0]);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'O':
    switch (command[1]) {
    case 'F': //OF
      // set/read offset frequency
      implemented = FALSE;
      break;

    case 'I': //OI
      // set/read offset frequency
      implemented = FALSE;
      break;

    case 'S': //OS
      // set/read offset function status
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'P':
    switch (command[1]) {
    case 'A': //PA

      //CATDEF    PA
      //DESCR     Set/Read RX1 preamp status
      //SET       PAx;
      //READ      PA;
      //RESP      PAx;
      //NOTE      Applies to RX1
      //NOTE      x=0: RX1 preamp off, x=1: on
      //NOTE      newer HPSDR radios do not have a switchable preamp
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "PA%d0;", receiver[0]->preamp);
        send_resp(client->fd, reply);
      } else if (command[4] == ';') {
        receiver[0]->preamp = command[2] == '1';
      }

      break;

    case 'B': //PB
      // set/read FRU-3A playback status
      implemented = FALSE;
      break;

    case 'C': //PC

      //CATDEF    PC
      //DESCR     Set/Read TX power (Drive slider)
      //SET       PCxxx;
      //READ      PC;
      //RESP      PCxxx;
      //NOTE      x = 0...100
      //ENDDEF
      if (can_transmit) {
        if (command[2] == ';') {
          snprintf(reply,  sizeof(reply), "PC%03d;", (int)transmitter->drive);
          send_resp(client->fd, reply);
        } else if (command[5] == ';') {
          set_drive((double)atoi(&command[2]));
        }
      }

      break;

    case 'I': //PI
      // store in program memory channel
      implemented = FALSE;
      break;

    case 'K': //PK
      // read packet cluster data
      implemented = FALSE;
      break;

    case 'L': //PL

      //CATDEF    PL
      //DESCR     Set/Read TX compressor level
      //SET       PLxxxyyy;
      //READ      PL;
      //RESP      PLxxxyyy;
      //NOTE      x = 0...100, maps to compression 0...20 dB.
      //NOTE      y ignored when setting, y=0 when reading
      //ENDDEF
      if (can_transmit) {
        if (command[2] == ';') {
          snprintf(reply,  sizeof(reply), "PL%03d000;", (int)(5.0 * transmitter->compressor_level));
          send_resp(client->fd, reply);
        } else if (command[8] == ';') {
          command[5] = '\0';
          double level = (double)atoi(&command[2]);
          level = 0.2 * level;
          transmitter->compressor_level = level;
          tx_set_compressor(transmitter);
          g_idle_add(ext_vfo_update, NULL);
        }
      }

      break;

    case 'M': //PM
      // recall program memory
      implemented = FALSE;
      break;

    case 'R': //PR
      // set/read speech processor function
      implemented = FALSE;
      break;

    case 'S': //PS

      //CATDEF    PS
      //DESCR     Set/Read power status
      //SET       PSx;
      //READ      PS;
      //RESP      PSx;
      //NOTE      x = 1: Power on, x=0: off
      //NOTE      When setting, x=1 is ignored and x=0 leads to shutdown
      //NOTE      Reading always reports x=1
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "PS1;");
        send_resp(client->fd, reply);
      } else if (command[3] == ';') {
        int pwrc = atoi(&command[2]);

        if ( pwrc == 0 ) {
          schedule_action(SHUTDOWN, ACTION_PRESSED, 0);
        } else {
          // power-on command. Should there be a reply?
          // snprintf(reply,  sizeof(reply), "PS1;");
        }
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'Q':
    switch (command[1]) {
    case 'C': //QC
      // set/read DCS code
      implemented = FALSE;
      break;

    case 'I': //QI
      // store in quick memory
      implemented = FALSE;
      break;

    case 'R': //QR
      // set/read quick memory channel data
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'R':
    switch (command[1]) {
    case 'A': //RA

      //CATDEF    RA
      //DESCR     Set/Read RX1 attenuator or RX1 gain
      //SET       RAxx;
      //READ      RA;
      //RESP      RAxxyy;
      //NOTE      x = 0 ... 99 is mapped to the attenuation range available.
      //NOTE      HPSDR radios: attenuator range 0...31 dB,
      //CONT      HermesLite-II etc.: gain range -12...48 dB.
      //CONT      y is always zero.
      //ENDDEF
      // set/read Attenuator function
      if (command[2] == ';') {
        int att = 0;

        if (have_rx_gain) {
          // map gain value -12...48 to 0...99
          att = (int)(adc[receiver[0]->adc].attenuation + 12);
          att = (int)(((double)att / 60.0) * 99.0);
        }

        if (have_rx_att) {
          // map att value -31 ... 0 to 0...99
          att = (int)(adc[receiver[0]->adc].attenuation);
          att = (int)(((double)att / 31.0) * 99.0);
        }

        snprintf(reply,  sizeof(reply), "RA%02d00;", att);
        send_resp(client->fd, reply);
      } else if (command[4] == ';') {
        int att = atoi(&command[2]);

        if (have_rx_gain) {
          // map 0...99 scale to -12...48
          att = (int)((((double)att / 99.0) * 60.0) - 12.0);
          set_rf_gain(VFO_A, (double)att);
        }

        if (have_rx_att) {
          // mapp 0...99 scale to 0...31
          att = (int)(((double)att / 99.0) * 31.0);
          set_attenuation_value((double)att);
        }
      }

      break;

    case 'C': //RC

      //CATDEF    RC
      //DESCR     Clear VFO-A RIT value
      //SET       RC;
      //ENDDEF
      if (command[2] == ';') {
        vfo[VFO_A].rit = 0;
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'D': //RD

      //CATDEF    RD
      //DESCR     Set or Decrement VFO-A RIT value
      //SET       RDxxxxx;
      //NOTE      When x is not given (RD;)  decrement by 10 Hz (CW modes) or 50 Hz (other modes).
      //CONT      When x is given, set VFO-A rit value to the negative of x.
      //ENDDEF
      if (command[2] == ';') {
        if (vfo[VFO_A].mode == modeCWL || vfo[VFO_A].mode == modeCWU) {
          vfo[VFO_A].rit -= 10;
        } else {
          vfo[VFO_A].rit -= 50;
        }

        g_idle_add(ext_vfo_update, NULL);
      } else if (command[7] == ';') {
        vfo[VFO_A].rit = -atoi(&command[2]);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'G': //RG
      // set/read RF gain status
      implemented = FALSE;
      break;

    case 'L': //RL
      // set/read noise reduction level
      implemented = FALSE;
      break;

    case 'M': //RM
      // set/read meter function
      implemented = FALSE;
      break;

    case 'T': //RT

      //CATDEF    RT
      //DESCR     Read/Set VFO-A RIT status
      //SET       RTx;
      //READ      RT;
      //RESP      RTx;
      //NOTE      x=0: VFO-A RIT off, x=1: on
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "RT%d;", vfo[VFO_A].rit_enabled);
        send_resp(client->fd, reply);
      } else if (command[3] == ';') {
        vfo[VFO_A].rit_enabled = atoi(&command[2]);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'U': //RU

      //CATDEF    RU
      //DESCR     Set or Increment VFO-A RIT value
      //SET       RUxxxxx;
      //NOTE      When x is not given (RU;)  increment by 10 Hz (CW modes) or 50 Hz (other modes).
      //CONT      When x is given, set VFO-A rit value to x
      //ENDDEF
      if (command[2] == ';') {
        if (vfo[VFO_A].mode == modeCWL || vfo[VFO_A].mode == modeCWU) {
          vfo[VFO_A].rit += 10;
        } else {
          vfo[VFO_A].rit += 50;
        }

        g_idle_add(ext_vfo_update, NULL);
      } else if (command[7] == ';') {
        vfo[VFO_A].rit = atoi(&command[2]);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'X': //RX

      //CATDEF    RX
      //DESCR     Enter RX mode
      //SET       RX;
      //ENDDEF
      if (command[2] == ';') {
        radio_set_mox(0);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'S':
    switch (command[1]) {
    case 'A': //SA

      //CATDEF    SA
      //DESCR     Set/Read SAT mode
      //SET       SAxyzsbcdeeeeeeee;
      //READ      SA;
      //RESP      SAxyzsbcdeeeeeeee;
      //NOTE      x=0: neither SAT nor RSAT, x=1: SAT or RSAT
      //NOTE      y,z,s always zero
      //NOTE      c = 1 indicates SAT mode (TRACE)
      //NOTE      d = 1 indicates RSAT mode (TRACE REV)
      //NOTE      e = eight-character label, here "SAT     "
      //NOTE      when setting, c=1 and/or d=1 is illegal, and s is ignored.
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "SA%d%d%d%d%d%d%dSAT     ;", (sat_mode == SAT_MODE) || (sat_mode == RSAT_MODE), 0, 0, 0,
                 sat_mode == SAT_MODE, sat_mode == RSAT_MODE, 0);
        send_resp(client->fd, reply);
      } else if (command[9] == ';') {
        if (command[2] == '0') {
          radio_set_satmode(SAT_NONE);
        } else if (command[2] == '1') {
          if (command[6] == '0' && command[7] == '0') {
            radio_set_satmode(SAT_NONE);
          } else if (command[6] == '1' && command[7] == '0') {
            radio_set_satmode(SAT_MODE);
          } else if (command[6] == '0' && command[7] == '1') {
            radio_set_satmode(RSAT_MODE);
          } else {
            implemented = FALSE;
          }
        }
      } else {
        implemented = FALSE;
      }

      break;

    case 'B': //SB
      // set/read SUB,TF-W status
      implemented = FALSE;
      break;

    case 'C': //SC
      // set/read SCAN function status
      implemented = FALSE;
      break;

    case 'D': //SD

      //CATDEF    SD
      //DESCR     Set/Read CW break-in hang time
      //SET       SDxxxx;
      //READ      SD;
      //RESP      SDxxxx;
      //NOTE      x = 0...1000 (in milli seconds).
      //CONT      When setting, x = 0  disables break-in
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "SD%04d;", (int)fmin(cw_keyer_hang_time, 1000));
        send_resp(client->fd, reply);
      } else if (command[6] == ';') {
        int b = fmin(atoi(&command[2]), 1000);
        cw_breakin = (b == 0);
        cw_keyer_hang_time = b;
      } else {
        implemented = FALSE;
      }

      break;

    case 'H': //SH

      //CATDEF    SH
      //DESCR     Set/Read VFO-A filter high-water (LSB, USB, DIGL, DIGU only)
      //SET       SHxx;
      //READ      SH;
      //RESP      SHxx;
      //NOTE      When setting, the Var1 filter is activated
      //NOTE      x = 0...11 encodes filter high water mark in Hz:
      //NOTE      1400 (x=0), 1600 (x=1), 1800 (x=2), 2000 (x=3)
      //CONT      2200 (x=4), 2400 (x=5), 2600 (x=6), 2800 (x=7)
      //CONT      3000 (x=8), 3400 (x=9), 4000 (x=10), 5000 (x=11).
      //ENDDEF
      if (command[2] == ';') {
        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        const FILTER *filter = &mode_filters[vfo[VFO_A].filter];
        int fh, high = 0;

        switch (vfo[VFO_A].mode) {
        case modeLSB:
        case modeDIGL:
          high = abs(filter->low);
          break;

        case modeUSB:
        case modeDIGU:
          high = filter->high;
          break;

        default:
          implemented = FALSE;
          break;
        }

        if (high <= 1400) {
          fh = 0;
        } else if (high <= 1600) {
          fh = 1;
        } else if (high <= 1800) {
          fh = 2;
        } else if (high <= 2000) {
          fh = 3;
        } else if (high <= 2200) {
          fh = 4;
        } else if (high <= 2400) {
          fh = 5;
        } else if (high <= 2600) {
          fh = 6;
        } else if (high <= 2800) {
          fh = 7;
        } else if (high <= 3000) {
          fh = 8;
        } else if (high <= 3400) {
          fh = 9;
        } else if (high <= 4000) {
          fh = 10;
        } else {
          fh = 11;
        }

        if (implemented) {
          snprintf(reply,  sizeof(reply), "SH%02d;", fh);
          send_resp(client->fd, reply) ;
        }
      } else if (command[4] == ';') {
        // make sure filter is filterVar1
        if (vfo[VFO_A].filter != filterVar1) {
          vfo_id_filter_changed(VFO_A, filterVar1);
        }

        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        FILTER *filter = &mode_filters[filterVar1];
        int i = atoi(&command[2]);
        int fh;

        switch (i) {
        case 0:
          fh = 1400;
          break;

        case 1:
          fh = 1600;
          break;

        case 2:
          fh = 1800;
          break;

        case 3:
          fh = 2000;
          break;

        case 4:
          fh = 2200;
          break;

        case 5:
          fh = 2400;
          break;

        case 6:
          fh = 2600;
          break;

        case 7:
          fh = 2800;
          break;

        case 8:
          fh = 3000;
          break;

        case 9:
          fh = 3400;
          break;

        case 10:
          fh = 4000;
          break;

        case 11:
          fh = 5000;
          break;

        default:
          fh = 100;
          break;
        }

        switch (vfo[VFO_A].mode) {
        case modeUSB:
        case modeDIGU:
          filter->high = fh;
          break;

        case modeLSB:
        case modeDIGL:
          filter->low = -fh;
          break;

        default:
          implemented = FALSE;
        }

        vfo_id_filter_changed(VFO_A, filterVar1);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'I': //SI
      // enter satellite memory name
      implemented = FALSE;
      break;

    case 'L': //SL

      //CATDEF    SL
      //DESCR     Set/Read VFO-A filter low-water (LSB, USB, DIGL, DIGU only)
      //SET       SLxx;
      //READ      SL;
      //RESP      SLxx;
      //NOTE      When setting, the Var1 filter is activated
      //NOTE      x = 0...11 encodes filter low water mark in Hz:
      //NOTE      10 (x=0), 50 (x=1), 100 (x=2), 200 (x=3)
      //CONT      300 (x=4), 400 (x=5), 500 (x=6), 600 (x=7)
      //CONT      700 (x=8), 800 (x=9), 900 (x=10), 1000 (x=11).
      //ENDDEF
      if (command[2] == ';') {
        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        const FILTER *filter = &mode_filters[vfo[VFO_A].filter];
        int fl = 2;
        int low = filter->low;

        if (vfo[VFO_A].mode == modeLSB || vfo[VFO_A].mode == modeDIGL) {
          low = abs(filter->high);
        }

        if (low <= 10) {
          fl = 0;
        } else if (low <= 50) {
          fl = 1;
        } else if (low <= 100) {
          fl = 2;
        } else if (low <= 200) {
          fl = 3;
        } else if (low <= 300) {
          fl = 4;
        } else if (low <= 400) {
          fl = 5;
        } else if (low <= 500) {
          fl = 6;
        } else if (low <= 600) {
          fl = 7;
        } else if (low <= 700) {
          fl = 8;
        } else if (low <= 800) {
          fl = 9;
        } else if (low <= 900) {
          fl = 10;
        } else {
          fl = 11;
        }

        snprintf(reply,  sizeof(reply), "SL%02d;", fl);
        send_resp(client->fd, reply) ;
      } else if (command[4] == ';') {
        // make sure filter is filterVar1
        if (vfo[VFO_A].filter != filterVar1) {
          vfo_id_filter_changed(VFO_A, filterVar1);
        }

        FILTER *mode_filters = filters[vfo[VFO_A].mode];
        FILTER *filter = &mode_filters[filterVar1];
        int i = atoi(&command[2]);
        int fl = 100;

        switch (i) {
        case 0:
          fl = 10;
          break;

        case 1:
          fl = 50;
          break;

        case 2:
          fl = 100;
          break;

        case 3:
          fl = 200;
          break;

        case 4:
          fl = 300;
          break;

        case 5:
          fl = 400;
          break;

        case 6:
          fl = 500;
          break;

        case 7:
          fl = 600;
          break;

        case 8:
          fl = 700;
          break;

        case 9:
          fl = 800;
          break;

        case 10:
          fl = 900;
          break;

        case 11:
          fl = 1000;
          break;

        default:
          fl = 100;
          break;
        }

        switch (vfo[VFO_A].mode) {
        case modeLSB:
        case modeDIGL:
          filter->high = -fl;
          break;

        case modeUSB:
        case modeDIGU:
          filter->low = fl;
          break;
        }

        vfo_id_filter_changed(VFO_A, filterVar1);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'M': //SM

      //CATDEF    SM
      //DESCR     Read S-meter
      //READ      SMx;
      //RESP      SMxyyyy;
      //NOTE      x=0: read RX1, x=1: read RX2
      //NOTE      y : 0 ... 30 mapped to -127...-19 dBm
      //ENDDEF
      if (command[3] == ';') {
        int id = atoi(&command[2]);
        RXCHECK (id,
                 int val = (int)((receiver[id]->meter + 127.0) * 0.277778);

        if (val > 30) { val = 30; }
      if (val < 0 ) { val = 0; }
      snprintf(reply,  sizeof(reply), "SM%d%04d;", id, val);
      send_resp(client->fd, reply);
              )
      }

      break;

    case 'Q': //SQ

      //CATDEF    SQ
      //DESCR     Set/Read squelch level (Squelch slider)
      //SET       SQxyyy;
      //READ      SQx;
      //RESP      SQxyyy
      //NOTE      x=0: read/set RX1 squelch, x=1: RX2.
      //NOTE      y : 0-255 mapped to 0-100
      //ENDDEF
      if (command[3] == ';') {
        int id = atoi(&command[2]);
        RXCHECK(id,
                snprintf(reply,  sizeof(reply), "SQ%d%03d;", id, (int)((double)receiver[id]->squelch / 100.0 * 255.0 + 0.5));
                send_resp(client->fd, reply);
               )
      } else if (command[6] == ';') {
        int id = atoi(&command[2]);
        int p2 = atoi(&command[3]);
        RXCHECK(id,
                receiver[id]->squelch = (int)((double)p2 / 255.0 * 100.0 + 0.5);
                set_squelch(receiver[id]);
               )
      }

      break;

    case 'R': //SR
      // reset transceiver
      implemented = FALSE;
      break;

    case 'S': //SS
      // set/read program scan pause frequency
      implemented = FALSE;
      break;

    case 'T': //ST
      // set/read MULTI/CH channel frequency steps
      implemented = FALSE;
      break;

    case 'U': //SU
      // set/read program scan pause frequency
      implemented = FALSE;
      break;

    case 'V': //SV
      // execute memory transfer function
      implemented = FALSE;
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'T':
    switch (command[1]) {
    case 'C': //TC
      // set/read internal TNC mode
      implemented = FALSE;
      break;

    case 'D': //TD
      // send DTMF memory channel data
      implemented = FALSE;
      break;

    case 'I': //TI
      // read TNC LED status
      implemented = FALSE;
      break;

    case 'N': //TN
      // set/read sub-tone frequency
      implemented = FALSE;
      break;

    case 'O': //TO
      // set/read TONE function
      implemented = FALSE;
      break;

    case 'S': //TS
      // set/read TF-SET function
      implemented = FALSE;
      break;

    case 'X': //TX

      //CATDEF    TX
      //DESCR     Enter TX mode
      //SET       TX;
      //ENDDEF
      // set transceiver to TX mode
      if (command[2] == ';') {
        radio_set_mox(1);
      }

      break;

    case 'Y': //TY

      //CATDEF    TY
      //DESCR     Read firmware version
      //READ      TY;
      //RESP      TYxxx;
      //NOTE      x is always zero
      //ENDDEF
      if (command[2] == ';') {
        send_resp(client->fd, "TY000;");
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'U':
    switch (command[1]) {
    case 'L': //UL
      // detects the PLL unlock status
      implemented = FALSE;
      break;

    case 'P': //UP

      //CATDEF    UP
      //DESCR     Move VFO-A one step up
      //SET       UP;
      //NOTE      use current VFO-A step size
      //ENDDEF
      if (command[2] == ';') {
        vfo_id_step(VFO_A, 1);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'V':
    switch (command[1]) {
    case 'D': //VD
      // set/read VOX delay time
      implemented = FALSE;
      break;

    case 'G': //VG

      //CATDEF    VG
      //DESCR     Set/Read VOX threshold
      //SET       VGxxx;
      //READ      VG;
      //RESP      VGxxx;
      //NOTE      x is in the range 0-9, mapped to an amplitude
      //CONT      threshold 0.0-1.0
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "VG%03d;", (int)((vox_threshold * 100.0) * 0.9));
        send_resp(client->fd, reply);
      } else if (command[5] == ';') {
        vox_threshold = atof(&command[2]) / 9.0;
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    case 'R': //VR
      // emulate VOICE1 or VOICE2 key
      implemented = FALSE;
      break;

    case 'X': //VX

      //CATDEF    VX
      //DESCR     Set/Read VOX status
      //SET       VXx;
      //READ      VX;
      //RESP      VXx;
      //NOTE      x=0: VOX disabled, x=1: enabled
      //ENDDEF
      if (command[2] == ';') {
        snprintf(reply,  sizeof(reply), "VX%d;", vox_enabled);
        send_resp(client->fd, reply);
      } else if (command[3] == ';') {
        vox_enabled = atoi(&command[2]);
        g_idle_add(ext_vfo_update, NULL);
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'W':
    switch (command[1]) {
    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'X':
    switch (command[1]) {
    case 'T': //XT

      //CATDEF    XT
      //DESCR     Set/Read XIT status
      //SET       XTx;
      //READ      XT;
      //RESP      XTx;
      //NOTE      x=0: XIT disabled, x=1: enabled
      //ENDDEF
      if (can_transmit) {
        if (command[2] == ';') {
          snprintf(reply,  sizeof(reply), "XT%d;", vfo[vfo_get_tx_vfo()].xit_enabled);
          send_resp(client->fd, reply);
        } else if (command[3] == ';') {
          vfo_xit_onoff(SET(atoi(&command[2])));
        }
      }

      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'Y':
    switch (command[1]) {
    default:
      implemented = FALSE;
      break;
    }

    break;

  case 'Z':
    switch (command[1]) {
    case 'Z':
      implemented = parse_extended_cmd (command, client);
      break;

    default:
      implemented = FALSE;
      break;
    }

    break;

  default:
    implemented = FALSE;
    break;
  }

  if (!implemented) {
    if (rigctl_debug) { t_print("RIGCTL: UNIMPLEMENTED COMMAND: %s\n", info->command); }

    send_resp(client->fd, "?;");
  }

  client->done = 1; // possibly inform server that command is finished
  g_free(info->command);
  g_free(info);
  return 0;
}

// Serial Port Launch
static int set_interface_attribs (int fd, speed_t speed, int parity) {
  struct termios tty;
  memset (&tty, 0, sizeof tty);

  if (tcgetattr (fd, &tty) != 0) {
    t_perror ("RIGCTL (tcgetattr):");
    return -1;
  }

  cfsetospeed (&tty, speed);
  cfsetispeed (&tty, speed);
  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
  // disable IGNBRK for mismatched speed tests; otherwise receive break
  // as \000 chars
  tty.c_iflag &= ~IGNBRK;         // disable break processing
  tty.c_lflag = 0;                // no signaling chars, no echo,
  // no canonical processing
  tty.c_oflag = 0;                // no remapping, no delays
  tty.c_cc[VMIN]  = 0;            // read doesn't block
  tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout
  //tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl
  tty.c_iflag |= (IXON | IXOFF | IXANY); // shut off xon/xoff ctrl
  tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
  // enable reading
  tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
  tty.c_cflag |= parity;
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CRTSCTS;

  if (tcsetattr (fd, TCSANOW, &tty) != 0) {
    t_perror( "RIGCTL (tcsetattr):");
    return -1;
  }

  return 0;
}

static void set_blocking (int fd, int should_block) {
  struct termios tty;
  memset (&tty, 0, sizeof tty);
  int flags = fcntl(fd, F_GETFL, 0);

  if (should_block) {
    flags &= ~O_NONBLOCK;
  } else {
    flags |= O_NONBLOCK;
  }

  fcntl(fd, F_SETFL, flags);

  if (tcgetattr (fd, &tty) != 0) {
    t_perror ("RIGCTL (tggetattr):");
    return;
  }

  tty.c_cc[VMIN]  = SET(should_block);
  tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

  if (tcsetattr (fd, TCSANOW, &tty) != 0) {
    t_perror("RIGCTL (tcsetattr):");
  }
}

static gpointer serial_server(gpointer data) {
  // We're going to Read the Serial port and
  // when we get data we'll send it to parse_cmd
  CLIENT *client = (CLIENT *)data;
  char cmd_input[MAXDATASIZE];
  char *command = g_new(char, MAXDATASIZE);
  int command_index = 0;
  int i;
  fd_set fds;
  struct timeval tv;
  t_print("%s: Entering Thread\n", __FUNCTION__);
  g_mutex_lock(&mutex_numcat);
  cat_control++;
  g_mutex_unlock(&mutex_numcat);
  g_idle_add(ext_vfo_update, NULL);
  client->running = TRUE;

  while (client->running) {
    //
    // If the "serial line" is a FIFO, we must not drain it
    // by reading our own responses (they must go to the other
    // side). Therefore, wait until 50msec after the last
    // CAT command of this client has been processed.
    // If for some reason this does not happen, resume after
    // waiting for about 500 msec.
    // Check client->running after the "pause" and after returning
    // from "read".
    //
    while (client->fifo && client->busy > 0) {
      if (client->done) {
        // command done, possibly response sent:
        // wait 50 msec then resume listening
        usleep(50000L);
        break;
      }

      usleep(50000L);
      client->busy--;
    }

    client->busy = 0;
    client->done = 0;

    if (!client->running) { break; }

    //
    // Blocking I/O with a time-out
    //
    FD_ZERO(&fds);
    FD_SET(client->fd, &fds);
    tv.tv_usec = 250000; // 250 msec
    tv.tv_sec = 0;

    if (select(client->fd + 1, &fds, NULL, NULL, &tv) <= 0) {
      continue;
    }

    int numbytes = read (client->fd, cmd_input, sizeof cmd_input);

    //
    // On my MacOS using a FIFO, I have seen that numbytes can be -1
    // (with errno = EAGAIN) although the select() inidcated that data
    // is available. Therefore the serial thread is not shut down if
    // the read() failed -- it will try again and again until it is
    // shut down by the rigctl menu.
    if (!client->running) { break; }

    if (numbytes > 0) {
      for (i = 0; i < numbytes; i++) {
        //
        // Filter out newlines and other non-printable characters
        // These may occur when doing CAT manually with a terminal program
        //
        if (cmd_input[i] < 32) {
          continue;
        }

        command[command_index] = cmd_input[i];
        command_index++;

        if (cmd_input[i] == ';') {
          command[command_index] = '\0';

          if (rigctl_debug) { t_print("RIGCTL: serial command=%s\n", command); }

          COMMAND *info = g_new(COMMAND, 1);
          info->client = client;
          info->command = command;
          client->busy = 10;
          g_idle_add(parse_cmd, info);
          command = g_new(char, MAXDATASIZE);
          command_index = 0;
        }
      }
    }
  }

  // Release the last "command" buffer (that has not yet been used)
  g_free(command);
  g_mutex_lock(&mutex_numcat);
  cat_control--;
  g_mutex_unlock(&mutex_numcat);
  g_idle_add(ext_vfo_update, NULL);
  t_print("%s: Exiting Thread, running=%d\n", __FUNCTION__, client->running);
  return NULL;
}

int launch_serial_rigctl (int id) {
  int fd;
  speed_t speed;
  t_print("%s:  Port %s\n", __FUNCTION__, SerialPorts[id].port);
  //
  // Use O_NONBLOCK to prevent "hanging" upon open(), set blocking mode
  // later.
  //
  fd = open (SerialPorts[id].port, O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK);

  if (fd < 0) {
    t_perror("RIGCTL (open serial):");
    return 0 ;
  }

  t_print("%s: serial port fd=%d\n", __FUNCTION__, fd);
  serial_client[id].fd = fd;
  // hard-wired parity = NONE
  speed = SerialPorts[id].speed;

  //
  // ANDROMEDA uses a hard-wired baud rate 9600
  //
  if (SerialPorts[id].andromeda && !SerialPorts[id].g2) {
    speed = B9600;
  }

  t_print("%s: Speed (baud rate code)=%d\n", __FUNCTION__, (int) speed);
  serial_client[id].fifo = 0;

  if (set_interface_attribs (fd, speed, 0) == 0) {
    set_blocking (fd, 1);                   // set blocking
  } else {
    //
    // This tells the server that fd is something else
    // than a serial line (most likely a FIFO), but it
    // can still be used.
    //
    t_print("%s: serial port is probably a FIFO\n", __FUNCTION__);
    serial_client[id].fifo = 1;
  }

  //
  // Initialise the rest of the CLIENT data structure
  // (except fd and fifo)
  //
  serial_client[id].busy               = 0;
  serial_client[id].done               = 0;
  serial_client[id].running            = 1;
  serial_client[id].andromeda_timer    = 0;
  serial_client[id].auto_reporting     = SET(SerialPorts[id].autoreporting);
  serial_client[id].andromeda_type     = 0;
  serial_client[id].last_fa            = -1;
  serial_client[id].last_fb            = -1;
  serial_client[id].last_md            = -1;
  serial_client[id].last_v             = 0;
  serial_client[id].shift              = 0;
  serial_client[id].buttonvec          = NULL;
  serial_client[id].encodervec         = NULL;

  for (int i = 0; i < MAX_ANDROMEDA_LEDS; i++) {
    serial_client[id].last_led[i] = -1;
  }

  //
  // Spawn off server thread
  //
  serial_client[id].thread_id = g_thread_new( "Serial server", serial_server, (gpointer)&serial_client[id]);
  //
  // Launch auto-reporter task
  //
  serial_client[id].auto_timer = g_timeout_add(750, autoreport_handler, &serial_client[id]);

  //
  // If this is a serial line to an ANDROMEDA controller, initialise it and start a periodic GTK task
  //
  if (SerialPorts[id].andromeda) {
    //
    // For Arduino UNO and the like, opening the serial line executes a hardware
    // reset and then the device stays in bootloader mode for half a second or so.
    //
    usleep(700000L);
    // Note this will send a ZZZS; command upon first invocation
    serial_client[id].andromeda_timer = g_timeout_add(500, andromeda_handler, &serial_client[id]);
  }

  return 1;
}

// Serial Port close
void disable_serial_rigctl (int id) {
  t_print("%s: Close Serial Port %s\n", __FUNCTION__, SerialPorts[id].port);

  if (serial_client[id].andromeda_timer != 0) {
    g_source_remove(serial_client[id].andromeda_timer);
    serial_client[id].andromeda_timer = 0;
  }

  if (serial_client[id].auto_timer != 0) {
    g_source_remove(serial_client[id].auto_timer);
    serial_client[id].auto_timer = 0;
  }

  serial_client[id].running = FALSE;

  if (serial_client[id].fifo) {
    //
    // If the "serial port" is a fifo then the serial thread
    // may hang in a blocking read().
    // Fortunately, we can set the thread free
    // by sending something to the FIFO
    //
    write (serial_client[id].fd, "ID;", 3);
  }

  // wait for the serial server actually terminating
  if (serial_client[id].thread_id) {
    g_thread_join(serial_client[id].thread_id);
    serial_client[id].thread_id = NULL;
  }

  if (serial_client[id].fd >= 0) {
    close(serial_client[id].fd);
    serial_client[id].fd = -1;
  }
}

void launch_tcp_rigctl () {
  t_print( "---- LAUNCHING RIGCTL SERVER ----\n");
  tcp_running = 1;

  //
  // Start CW thread and auto reporter, if not yet done
  //
  if (!rigctl_cw_thread_id) {
    rigctl_cw_thread_id = g_thread_new("RIGCTL cw", rigctl_cw_thread, NULL);
  }

  //
  // Start TCP thread
  //
  rigctl_server_thread_id = g_thread_new( "rigctl server", rigctl_server, GINT_TO_POINTER(rigctl_tcp_port));
}

void rigctlRestoreState() {
  GetPropI0("rigctl_tcp_enable",                             rigctl_tcp_enable);
  GetPropI0("rigctl_tcp_andromeda",                          rigctl_tcp_andromeda);
  GetPropI0("rigctl_tcp_autoreporting",                      rigctl_tcp_autoreporting);
  GetPropI0("rigctl_port_base",                              rigctl_tcp_port);

  for (int id = 0; id < MAX_SERIAL; id++) {
    //
    // Do not overwrite a "detected" port
    //
    if (!SerialPorts[id].g2) {
      GetPropS1("rigctl_serial_port[%d]", id,                  SerialPorts[id].port);
      GetPropI1("rigctl_serial_enable[%d]", id,                SerialPorts[id].enable);
      GetPropI1("rigctl_serial_andromeda[%d]", id,             SerialPorts[id].andromeda);
      GetPropI1("rigctl_serial_baud_rate[%i]", id,             SerialPorts[id].speed);
      GetPropI1("rigctl_serial_autoreporting[%d]", id,         SerialPorts[id].autoreporting);

      if (SerialPorts[id].andromeda) {
        SerialPorts[id].speed = B9600;
      }
    }
  }

  //
  // G2 panel settings are restored when the panel is detected
  //
}

void rigctlSaveState() {
  SetPropI0("rigctl_tcp_enable",                             rigctl_tcp_enable);
  SetPropI0("rigctl_tcp_andromeda",                          rigctl_tcp_andromeda);
  SetPropI0("rigctl_tcp_autoreporting",                      rigctl_tcp_autoreporting);
  SetPropI0("rigctl_port_base",                              rigctl_tcp_port);

  for (int id = 0; id < MAX_SERIAL; id++) {
    SetPropI1("rigctl_serial_enable[%d]", id,                SerialPorts[id].enable);
    SetPropI1("rigctl_serial_andromeda[%d]", id,             SerialPorts[id].andromeda);
    SetPropI1("rigctl_serial_baud_rate[%i]", id,             SerialPorts[id].speed);
    SetPropS1("rigctl_serial_port[%d]", id,                  SerialPorts[id].port);
    SetPropI1("rigctl_serial_autoreporting[%d]", id,         SerialPorts[id].autoreporting);
  }

  //
  // Save Andromeda controller settings. Only types 4 and 5 are saved.
  // Only the first controller found is saved, and the search order is:
  // a) serial clients from the last to the first
  // b) tcp clients from the first to the last
  //
  for (int id = MAX_SERIAL - 1; id >= 0; id--) {
    if (serial_client[id].running &&
        (serial_client[id].andromeda_type == 4 || serial_client[id].andromeda_type == 5) &&
        serial_client[id].buttonvec != NULL &&
        serial_client[id].encodervec != NULL) {
      g2panelSaveState(serial_client[id].andromeda_type,
                       serial_client[id].buttonvec,
                       serial_client[id].encodervec);
      return;
    }
  }

  for (int id = 0; id < MAX_TCP_CLIENTS; id++) {
    if (tcp_client[id].running &&
        (tcp_client[id].andromeda_type == 4 || tcp_client[id].andromeda_type == 5) &&
        tcp_client[id].buttonvec != NULL &&
        tcp_client[id].encodervec != NULL) {
      g2panelSaveState(tcp_client[id].andromeda_type,
                       tcp_client[id].buttonvec,
                       tcp_client[id].encodervec);
      return;
    }
  }
}
