/*
 * Module: watchlink.cc
 *
 * **** License ****
 * Version: VPL 1.0
 *
 * The contents of this file are subject to the Vyatta Public License
 * Version 1.0 ("License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.vyatta.com/vpl
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * This code was originally developed by Vyatta, Inc.
 * Portions created by Vyatta are Copyright (C) 2008 Vyatta, Inc.
 * All Rights Reserved.
 *
 * Author: Michael Larson
 * Date: 2008
 * Description:
 *
 * **** End License ****
 *
 */
#include <unistd.h>
#include <iostream>
#include <stdio.h>
#include <string>
#include <set>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>

#include <linux/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <linux/rtnetlink.h>
#include "rl_str_proc.hh"
#include "netlink_send.hh"
#include "netlink_listener.hh"
#include "netlink_linkstatus.hh"
#include "netlink_event.hh"

using namespace std;


void print_netlink(NetlinkEvent &nl_event);
pid_t pid_output (const char *path);

struct option longopts[] = 
{
  { 0 }
};

set<string> g_exclude;
string g_link_dir = "/var/linkstatus";

/**
 *
 *
 **/
void 
usage()
{
  fprintf(stdout, "-s   start with sending netlink request\n");
  fprintf(stdout, "-l   specify directory for link status. default /var/linkstatus\n");
  fprintf(stdout, "-i   specify location of pid directory\n");
  fprintf(stdout, "-p   print netlink messages\n");
  fprintf(stdout, "-d   run as a daemon\n");
  fprintf(stdout, "-v   debug to stdout\n");
  fprintf(stdout, "-h   help message\n");
}

/**
 *
 *
 **/
set<string> 
load_exclusion_file(const string &link_dir)
{
  set<string> coll;

  string file = link_dir + "/exclude";
  FILE *fp = fopen(file.c_str(), "r");
  if (fp == NULL) {
    syslog(LOG_INFO,"load_exclusion_file(), failed to open state file");
    //    cerr << "load_exclusion_file(), failed to open state file" << endl;
    return coll; //means we are still up, ignore...
  }

  char str[1025];
  while (fgets(str, 1024, fp)) {
    string line(str);

    StrProc tokens(line, ",");
    for (int i = 0; i < tokens.size(); ++i) {
      coll.insert(tokens.get(i));
    }
  }
  fclose(fp);
  return coll;
}

/**
 *
 *
 **/
static void 
sig_user(int signo)
{
  //reload interface exclusion list
  g_exclude = load_exclusion_file(g_link_dir);
}


/**
 *
 *
 **/
int 
main(int argc, char* const argv[])
{
  int ch;
  bool send_request = false;
  bool debug = false;
  bool daemon = false;
  string pid_path;
  bool print_nl_msg = false;

  if (debug) {
    cout << "starting..." << endl;
  }

  while ((ch = getopt_long(argc, argv, "sl:i:dvhp",longopts,0)) != -1) {
    switch (ch) {
    case 's':
      send_request = true;
      break;
    case 'l':
      g_link_dir = optarg;
      break;
    case 'i':
      pid_path = optarg;
      break;
    case 'd':
      daemon = true;
      break;
    case 'v':
      debug = true;
      break;
    case 'p':
      print_nl_msg = true;
      break;
    case 'h':
      usage();
      exit(0);
    }
  }  
  
  NetlinkSend nl_send(debug);
  NetlinkListener nl_listener(debug);

  //add check here to ensure only one watchlink process

  if (daemon) {
    if (fork() != 0) {
      exit(0);
    }
  }

  if (pid_path.empty() == false) {
    pid_output(pid_path.c_str());
  }

  //load interface exclusion list
  g_exclude = load_exclusion_file(g_link_dir);

  signal(SIGUSR1, sig_user);

  int sock = nl_listener.init();
  if (sock <= 0) {
    syslog(LOG_ERR, "watchlink(), netlink listener failed in initialization. exiting..");
    cerr << "watchlink(), netlink listener failed in initialization. exiting.." << endl;
    exit(1);
  }

  if (send_request) {
    if (debug) {
      cout << "sending initial netlink request" << endl;
    }
    nl_listener.set_multipart(true);
    if (nl_send.send_get(sock, RTM_GETLINK) != 0) {
      syslog(LOG_ERR,"watchlink(), error sending, exiting..");
      cerr << "watchlink(), error sending. exiting.." << endl;
      exit(1);
    }
  }

  NetlinkLinkStatus nl_ls(sock, g_link_dir, debug);

  while (true) {
    //    cout << "watchlink: now entering listening mode: " << endl;

    NetlinkEvent nl_event;
    if (nl_listener.process(nl_event, g_exclude) == true) {
      if (send_request) {
	if (nl_send.send_get(sock, RTM_GETADDR) != 0) {
	  syslog(LOG_ERR,"watchlink(), error sending. exiting..");
	  cerr << "watchlink(), error sending. exiting.." << endl;
	  exit(1);
	}
	send_request = false;
      }
      else {
	nl_listener.set_multipart(false);
      }

      nl_ls.process(nl_event);

      if (debug) {
	cout << "  ifinfomsg: " << nl_event.get_ifinfomsg() << endl;
      }
      if (print_nl_msg) {
	print_netlink(nl_event);
      }

    }
    else {
      //      cout << "didn't receive a message, sleeping for 1 second" << endl;
      sleep(1);
    }
  }

  exit(0);
}


/**
 *
 *
 **/
void
print_netlink(NetlinkEvent &nl_event)
{
      char buf[20];
      sprintf(buf, "%d", nl_event.get_index());
      cout << "results for " << nl_event.get_iface() << "(" << string(buf) << ")" << endl;
      cout << "  running: " << string(nl_event.get_running() ? "yes" : "no") << endl;
      cout << "  enabled: " << string(nl_event.get_enabled() ? "yes" : "no") << endl;
      if (nl_event.get_type() == RTM_DELLINK || 
	  nl_event.get_type() == RTM_NEWLINK) {
	cout << "  type: " << string(nl_event.get_type()==RTM_DELLINK?"DELLINK":"NEWLINK") << endl;
	cout << "  state: " << string(nl_event.is_link_up()?"UP":"DOWN") << endl;
	sprintf(buf, "%d", nl_event.get_mtu());
	cout << "  mtu: " << string(buf) << endl;
	cout << "  mac: " << nl_event.get_mac_str() << endl;
	cout << "  alternate mac: " << nl_event.get_mac_str() << endl;
      }
      else if (nl_event.get_type() == RTM_DELADDR ||
	       nl_event.get_type() == RTM_NEWADDR) {
	cout << "  type: " << string(nl_event.get_type()==RTM_DELADDR?"DELADDR":"NEWADDR") << endl;
	cout << "  local addr: " << nl_event.get_local_addr().str().c_str() << endl;
	cout << "  addr: " << nl_event.get_addr().str().c_str() << endl;
	cout << "  broadcast: " << nl_event.get_broadcast().str().c_str() << endl;
	char buf[20];
	sprintf(buf, "%d", nl_event.get_mask_len());
	cout << "  mask length: " << string(buf) << endl;
      }
      cout << endl;

}

/**
 *
 *below borrowed from quagga library.
 **/
#define PIDFILE_MASK 0644
pid_t
pid_output (const char *path)
{
  FILE *fp;
  pid_t pid;
  mode_t oldumask;

  pid = getpid();

  oldumask = umask(0777 & ~PIDFILE_MASK);
  fp = fopen (path, "w");
  if (fp != NULL) 
    {
      fprintf (fp, "%d\n", (int) pid);
      fclose (fp);
      umask(oldumask);
      return pid;
    }
  /* XXX Why do we continue instead of exiting?  This seems incompatible
     with the behavior of the fcntl version below. */
  syslog(LOG_ERR,"Can't fopen pid lock file %s, continuing",
            path);
  umask(oldumask);
  return -1;
}
