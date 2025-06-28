/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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

#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>

#include "discovered.h"
#include "discovery.h"
#include "message.h"
#include "old_discovery.h"
#include "stemlab_discovery.h"

static char interface_name[64];
static struct sockaddr_in interface_addr = {0};
static struct sockaddr_in interface_netmask = {0};

#define DISCOVERY_PORT 1024
static int discovery_socket;

static GThread *discover_thread_id;
static gpointer discover_receive_thread(gpointer data);

//
// discflag = 1:   discover by sending UDP broadcast packet
// discflag = 2:   discover by sending UDP backet to Radio IP address
// discflag = 3:   discover by connecting via TCP
//
static void discover(struct ifaddrs* iface, int discflag) {
  int rc;
  struct sockaddr_in *sa;
  struct sockaddr_in *mask;
  struct sockaddr_in to_addr = {0};
  int flags;
  struct timeval tv;
  int optval;
  socklen_t optlen;
  fd_set fds;
  unsigned char buffer[1032];
  int i, len;

  switch (discflag) {
  case 1:
    //
    // Send METIS discovery packet to broadcast address on interface iface
    //
    snprintf(interface_name, sizeof(interface_name), "%s", iface->ifa_name);
    t_print("discover: looking for HPSDR devices on %s\n", interface_name);
    // send a broadcast to locate hpsdr boards on the network
    discovery_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (discovery_socket < 0) {
      t_perror("discover: create socket failed for discovery_socket:");
      return;
    }

    sa = (struct sockaddr_in *) iface->ifa_addr;
    mask = (struct sockaddr_in *) iface->ifa_netmask;
    interface_netmask.sin_addr.s_addr = mask->sin_addr.s_addr;
    // bind to this interface and the discovery port
    interface_addr.sin_family = AF_INET;
    interface_addr.sin_addr.s_addr = sa->sin_addr.s_addr;
    interface_addr.sin_port = htons(0); // system assigned port

    if (bind(discovery_socket, (struct sockaddr * )&interface_addr, sizeof(interface_addr)) < 0) {
      t_perror("discover: bind socket failed for discovery_socket:");
      close (discovery_socket);
      return;
    }

    t_print("discover: bound to %s\n", interface_name);
    // allow broadcast on the socket
    int on = 1;
    rc = SETSOCKOPT(discovery_socket, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));

    if (rc != 0) {
      t_print("discover: cannot set SO_BROADCAST: rc=%d\n", rc);
      close (discovery_socket);
      return;
    }

    // setup to address
    to_addr.sin_family = AF_INET;
    to_addr.sin_port = htons(DISCOVERY_PORT);
    to_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    break;

  case 2:
    //
    // Send METIS detection packet via UDP to ipaddr_radio
    // To be able to connect later, we have to specify INADDR_ANY
    //
    interface_addr.sin_family = AF_INET;
    interface_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&to_addr, 0, sizeof(to_addr));
    to_addr.sin_family = AF_INET;
    to_addr.sin_port = htons(DISCOVERY_PORT);

    if (inet_aton(ipaddr_radio, &to_addr.sin_addr) == 0) {
      return;
    }

    t_print("discover: looking for HPSDR device with IP %s\n", ipaddr_radio);
    discovery_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (discovery_socket < 0) {
      t_perror("discover: create socket failed for discovery_socket:");
      return;
    }

    break;

  case 3:
    //
    // Send METIS detection packet via TCP to ipaddr_radio
    // This is rather tricky, one must avoid "hanging" when the
    // connection does not succeed.
    //
    memset(&to_addr, 0, sizeof(to_addr));
    to_addr.sin_family = AF_INET;
    to_addr.sin_port = htons(DISCOVERY_PORT);

    if (inet_aton(ipaddr_radio, &to_addr.sin_addr) == 0) {
      return;
    }

    t_print("Trying to detect via TCP with IP %s\n", ipaddr_radio);
    discovery_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (discovery_socket < 0) {
      t_perror("discover: create socket failed for TCP discovery_socket\n");
      return;
    }

    //
    // Here I tried a bullet-proof approach to connect() such that the program
    // does not "hang" under any circumstances.
    // - First, one makes the socket non-blocking. Then, the connect() will
    //   immediately return with error EINPROGRESS.
    // - Then, one uses select() to look for *writeability* and check
    //   the socket error if everything went right. Since one calls select()
    //   with a time-out, one either succeed within this time or gives up.
    // - Do not forget to make the socket blocking again.
    //
    // Step 1. Make socket non-blocking and connect()
    flags = fcntl(discovery_socket, F_GETFL, 0);
    fcntl(discovery_socket, F_SETFL, flags | O_NONBLOCK);
    rc = connect(discovery_socket, (const struct sockaddr *)&to_addr, sizeof(to_addr));

    if ((errno != EINPROGRESS) && (rc < 0)) {
      t_perror("discover: connect() failed for TCP discovery_socket:");
      close(discovery_socket);
      return;
    }

    // Step 2. Use select to wait for the connection
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(discovery_socket, &fds);
    rc = select(discovery_socket + 1, NULL, &fds, NULL, &tv);

    if (rc < 0) {
      t_perror("discover: select() failed on TCP discovery_socket:");
      close(discovery_socket);
      return;
    }

    // If no connection occured, return
    if (rc == 0) {
      // select timed out
      t_print("discover: select() timed out on TCP discovery socket\n");
      close(discovery_socket);
      return;
    }

    // Step 3. select() succeeded. Check success of connect()
    optlen = sizeof(int);
    rc = GETSOCKOPT(discovery_socket, SOL_SOCKET, SO_ERROR, &optval, &optlen);

    if (rc < 0) {
      // this should very rarely happen
      t_perror("discover: getsockopt() failed on TCP discovery_socket:");
      close(discovery_socket);
      return;
    }

    if (optval != 0) {
      // connect did not succeed
      t_print("discover: connect() on TCP socket did not succeed\n");
      close(discovery_socket);
      return;
    }

    // Step 4. reset the socket to normal (blocking) mode
    fcntl(discovery_socket, F_SETFL, flags &  ~O_NONBLOCK);
    break;

  default:
    return;
    break;
  }

  optval = 1;
  SETSOCKOPT(discovery_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  SETSOCKOPT(discovery_socket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
  rc = devices;
  // start a receive thread to collect discovery response packets
  discover_thread_id = g_thread_new( "old discover receive", discover_receive_thread, GINT_TO_POINTER(discflag));

  // send discovery packet
  // If this is a TCP connection, send a "long" packet
  switch (discflag) {
  case 1:
  case 2:
    len = 63; // send UDP packet
    break;

  case 3:
    len = 1032; // send TCP packet
    break;
  }

  buffer[0] = 0xEF;
  buffer[1] = 0xFE;
  buffer[2] = 0x02;

  for (i = 3; i < len; i++) {
    buffer[i] = 0x00;
  }

  if (sendto(discovery_socket, buffer, len, 0, (struct sockaddr * )&to_addr, sizeof(to_addr)) < 0) {
    t_perror("discover: sendto socket failed for discovery_socket:");
    close (discovery_socket);
    return;
  }

  // wait for receive thread to complete
  g_thread_join(discover_thread_id);
  close(discovery_socket);

  switch (discflag) {
  case 1:
    t_print("discover: exiting discover for %s\n", iface->ifa_name);
    break;

  case 2:
    t_print("discover: exiting HPSDR discover for IP %s\n", ipaddr_radio);

    if (devices == rc + 1) {
      //
      // METIS detection UDP packet sent to fixed IP address got a valid response.
      //
      memcpy((void *)&discovered[rc].info.network.address, (void *)&to_addr, sizeof(to_addr));
      discovered[rc].info.network.address_length = sizeof(to_addr);
      snprintf(discovered[rc].info.network.interface_name, sizeof(discovered[rc].info.network.interface_name), "UDP");
      discovered[rc].use_routing = 1;
    }

    break;

  case 3:
    t_print("discover: exiting TCP discover for IP %s\n", ipaddr_radio);

    if (devices == rc + 1) {
      //
      // METIS detection TCP packet sent to fixed IP address got a valid response.
      // Patch the IP addr into the device field
      // and set the "use TCP" flag.
      //
      memcpy((void*)&discovered[rc].info.network.address, (void*)&to_addr, sizeof(to_addr));
      discovered[rc].info.network.address_length = sizeof(to_addr);
      snprintf(discovered[rc].info.network.interface_name, sizeof(discovered[rc].info.network.interface_name), "TCP");
      discovered[rc].use_routing = 1;
      discovered[rc].use_tcp = 1;
    }

    break;
  }
}

static gpointer discover_receive_thread(gpointer data) {
  struct sockaddr_in addr;
  socklen_t len;
  unsigned char buffer[2048];
  struct timeval tv;
  int i;
  int flag = GPOINTER_TO_INT(data);
  int oldnumdev = devices;
  t_print("discover_receive_thread\n");
  tv.tv_sec = 2;
  tv.tv_usec = 0;
  setsockopt(discovery_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));
  len = sizeof(addr);

  while (1) {
    if (flag != 1 && devices > oldnumdev) {
      //
      // For a 'directed' (UDP or TCP) discovery packet, return as soon as there
      // has been a valid answer.
      //
      break;
    }

    int bytes_read = recvfrom(discovery_socket, buffer, sizeof(buffer), 1032, (struct sockaddr*)&addr, &len);

    if (bytes_read < 0) {
      t_print("discovery: bytes read %d\n", bytes_read);
      t_perror("old_discovery: recvfrom socket failed for discover_receive_thread");
      break;
    }

    if (bytes_read == 0) { break; }

    t_print("old_discovery: received %d bytes\n", bytes_read);

    if ((buffer[0] & 0xFF) == 0xEF && (buffer[1] & 0xFF) == 0xFE) {
      int status = buffer[2] & 0xFF;

      if (status == 2 || status == 3) {
        if (devices < MAX_DEVICES) {
          discovered[devices].protocol = ORIGINAL_PROTOCOL;
          discovered[devices].device = buffer[10] & 0xFF;
          discovered[devices].software_version = buffer[9] & 0xFF;

          switch (discovered[devices].device) {
          case DEVICE_METIS:
            snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Metis");
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_HERMES:
            snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Hermes");
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_GRIFFIN:
            snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Griffin");
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_ANGELIA:
            snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Angelia");
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_ORION:
            snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Orion");
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_HERMES_LITE:
            //
            // HermesLite V2 boards use
            // DEVICE_HERMES_LITE as the ID and a software version
            // that is larger or equal to 40, while the original
            // (V1) HermesLite boards have software versions up to 31.
            // Furthermode, HL2 uses a minor version in buffer[21]
            // so the official version number e.g. 73.2 stems from buf9=73 and buf21=2
            //
            discovered[devices].software_version = 10 * (buffer[9] & 0xFF) + (buffer[21] & 0xFF);

            if (discovered[devices].software_version < 400) {
              snprintf(discovered[devices].name, sizeof(discovered[devices].name), "HermesLite V1");
            } else {
              snprintf(discovered[devices].name, sizeof(discovered[devices].name), "HermesLite V2");
              discovered[devices].device = DEVICE_HERMES_LITE2;
              t_print("discovered HL2: Gateware Major Version=%d Minor Version=%d\n", buffer[9], buffer[21]);
            }

            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 38400000.0;
            break;

          case DEVICE_ORION2:
            snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Orion2");
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_STEMLAB:
            // This is in principle the same as HERMES but has two ADCs
            // (and therefore, can do DIVERSITY).
            // There are some problems with the 6m band on the RedPitaya
            // but with additional filtering it can be used.
            snprintf(discovered[devices].name, sizeof(discovered[devices].name), "STEMlab");
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          case DEVICE_STEMLAB_Z20:
            // This is in principle the same as HERMES but has two ADCs
            // (and therefore, can do DIVERSITY).
            // There are some problems with the 6m band on the RedPitaya
            // but with additional filtering it can be used.
            snprintf(discovered[devices].name, sizeof(discovered[devices].name), "STEMlab-Zync7020");
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;

          default:
            snprintf(discovered[devices].name, sizeof(discovered[devices].name), "Unknown");
            discovered[devices].frequency_min = 0.0;
            discovered[devices].frequency_max = 61440000.0;
            break;
          }

          t_print("old_discovery: name=%s min=%0.3f MHz max=%0.3f MHz\n", discovered[devices].name,
                  discovered[devices].frequency_min * 1E-6,
                  discovered[devices].frequency_max * 1E-6);

          for (i = 0; i < 6; i++) {
            discovered[devices].info.network.mac_address[i] = buffer[i + 3];
          }

          discovered[devices].status = status;
          memcpy((void*)&discovered[devices].info.network.address, (void*)&addr, sizeof(addr));
          discovered[devices].info.network.address_length = sizeof(addr);
          memcpy((void*)&discovered[devices].info.network.interface_address, (void*)&interface_addr, sizeof(interface_addr));
          memcpy((void*)&discovered[devices].info.network.interface_netmask, (void*)&interface_netmask,
                 sizeof(interface_netmask));
          discovered[devices].info.network.interface_length = sizeof(interface_addr);
          snprintf(discovered[devices].info.network.interface_name, sizeof(discovered[devices].info.network.interface_name), "%s",
                   interface_name);
          discovered[devices].use_tcp = 0;
          discovered[devices].use_routing = 0;
          discovered[devices].supported_receivers = 2;
          t_print("old_discovery: found device=%d software_version=%d status=%d address=%s (%02X:%02X:%02X:%02X:%02X:%02X) on %s min=%0.3f MHz max=%0.3f MHz\n",
                  discovered[devices].device,
                  discovered[devices].software_version,
                  discovered[devices].status,
                  inet_ntoa(discovered[devices].info.network.address.sin_addr),
                  discovered[devices].info.network.mac_address[0],
                  discovered[devices].info.network.mac_address[1],
                  discovered[devices].info.network.mac_address[2],
                  discovered[devices].info.network.mac_address[3],
                  discovered[devices].info.network.mac_address[4],
                  discovered[devices].info.network.mac_address[5],
                  discovered[devices].info.network.interface_name,
                  discovered[devices].frequency_min * 1E-6,
                  discovered[devices].frequency_max * 1E-6);
          devices++;
        }
      }
    }
  }

  t_print("discovery: exiting discover_receive_thread\n");
  g_thread_exit(NULL);
  return NULL;
}

void old_discovery() {
  struct ifaddrs *addrs,*ifa;
  t_print("old_discovery\n");
  //
  // Start with discovering from a fixed ip address
  //
  int previous_devices = devices;
  discover(NULL, 2);

  if (tcp_enable) {
    discover(NULL, 3);
  }

  //
  // If we have been successful with the fixed IP address,
  // assume that we want that radio, and do not discover any
  // more.
  //
  if (devices <= previous_devices) {
    getifaddrs(&addrs);
    ifa = addrs;

    while (ifa) {
      g_main_context_iteration(NULL, 0);

      //
      // Sometimes there are many (virtual) interfaces, and some
      // of them are very unlikely to offer a radio connection.
      // These are skipped.
      // Note the "loopback" interfaces are checked:
      // the RadioBerry for example, is handled by a driver
      // which connects to HPSDR software via a loopback interface.
      //
      if (ifa->ifa_addr) {
        if (
          ifa->ifa_addr->sa_family == AF_INET
          && (ifa->ifa_flags & IFF_UP) == IFF_UP
          && (ifa->ifa_flags & IFF_RUNNING) == IFF_RUNNING
          //&& (ifa->ifa_flags & IFF_LOOPBACK) != IFF_LOOPBACK
          && strncmp("veth", ifa->ifa_name, 4)
          && strncmp("dock", ifa->ifa_name, 4)
          && strncmp("hass", ifa->ifa_name, 4)
        ) {
          discover(ifa, 1);   // send UDP broadcast packet to interface
        }
      }

      ifa = ifa->ifa_next;
    }

    freeifaddrs(addrs);
  }

  t_print( "discovery found %d devices\n", devices);

  for (int i = 0; i < devices; i++) {
    t_print("discovery: found device=%d software_version=%d status=%d address=%s (%02X:%02X:%02X:%02X:%02X:%02X) on %s\n",
            discovered[i].device,
            discovered[i].software_version,
            discovered[i].status,
            inet_ntoa(discovered[i].info.network.address.sin_addr),
            discovered[i].info.network.mac_address[0],
            discovered[i].info.network.mac_address[1],
            discovered[i].info.network.mac_address[2],
            discovered[i].info.network.mac_address[3],
            discovered[i].info.network.mac_address[4],
            discovered[i].info.network.mac_address[5],
            discovered[i].info.network.interface_name);
  }
}
