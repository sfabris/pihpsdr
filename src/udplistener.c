/* Copyright (C)
* 2024 - Christoph van W"ullen, DL1YCF
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

//
// Stand-alone "UDP listener program. It depends on the existence of
// the /usr/bin/festival program. This program is a text-to-speech
// application.
//
// This program will grap UDP packets (which may be broadcast)
// to port 19080.
// The contents are interpreted as a text and wrapped into a
// SayText command suitable for festival.
//
// Just start this program in a separate window and let it
// run.
//

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main() {
  int optval = 1;
  int udpsock;                   // UDP listening socket
  struct sockaddr_in addr;       // address to listen to
  struct sockaddr_in from;       // filled in recvfrm: where the packet came from
  socklen_t lenaddr;             
  ssize_t bytes;                  // size (in bytes) of a received packet
  char msg[130];                 // buffer for message from piHPSDR
  char cmd[256];                 // buffer for a command for festival
  int pipefd[2];                 // communication path to festival
  struct timeval tv;             // timeout for UDP receives

  pipe(pipefd);

  if (fork() == 0) {
    //
    // Child process: connect pipe to stdin
    // connect stdout and stderr to /dev/null
    // run festival program from /usr/bin/festival
    // 
    int fd1=open("/dev/null", O_WRONLY);
    int fd2=open("/dev/null", O_WRONLY);
    close (0);
    close (1);
    close (2); dup(pipefd[0]);    // assigned to stdin
    dup(fd1);          // assigned to stdout
    dup(fd2);          // assigned to stderr
    execl("/usr/bin/festival", "/usr/bin/festival", "--interactive", (char *) NULL);
    //
    // We should not arrive at this point
    //
    exit(1);
  }

  //
  // Prepeare UDP listener on port 19080
  //
  if ((udpsock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("SOCKET:");
    exit(1);
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family=AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(19080);

  setsockopt(udpsock, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval));
  setsockopt(udpsock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  setsockopt(udpsock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

  tv.tv_sec = 0;
  tv.tv_usec = 10000;
  setsockopt(udpsock, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv, sizeof(tv));

  if (bind(udpsock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("BIND:");
    exit(1);
  }

  for (;;) {
    //
    // Infinite loop: get UDP packet,
    //                wrap text into a SayText command,
    //                send it to festival
    //
    lenaddr = sizeof(from);
    bytes=recvfrom(udpsock, msg, 128, 0, (struct sockaddr *)&from, &lenaddr);
    if (bytes <= 0) {
      usleep(100000);
      continue;
    }
    msg[bytes]=0;                                             // form null-terminated string
    snprintf(cmd, sizeof(cmd), "(SayText \"%s\")\n", msg);    // form festival command
    write (pipefd[1], cmd, strlen(cmd));                      // write command to festival
  }
}
