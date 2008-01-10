#include <unistd.h>
#include <iostream>
#include <stdio.h>
#include <string>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>

#include <linux/types.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include "netlink_send.hh"
#include "netlink_listener.hh"
#include "netlink_linkstatus.hh"
#include "netlink_event.hh"

using namespace std;


void print_netlink(NetlinkEvent &nl_event);
void process_down(NetlinkEvent &nl_event, const string &link_dir, bool debug);
void process_up(NetlinkEvent &nl_event, const string &link_dir, bool debug);
pid_t pid_output (const char *path);

struct option longopts[] = 
{
  { 0 }
};


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
int 
main(int argc, char* const argv[])
{
  int ch;
  string link_dir = "/var/linkstatus";
  bool send_request = false;
  bool debug = false;
  bool daemon = false;
  string pid_path;
  bool print_nl_msg = false;
  
  cout << "starting..." << endl;
  
  while ((ch = getopt_long(argc, argv, "sl:i:dvhp",longopts,0)) != -1) {
    switch (ch) {
    case 's':
      send_request = true;
      break;
    case 'l':
      link_dir = optarg;
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
  NetlinkListener nl_listener;

  //add check here to ensure only one watchlink process

  if (daemon) {
    if (fork() != 0) {
      exit(0);
    }
  }

  if (pid_path.empty() == false) {
    pid_output(pid_path.c_str());
  }


  int sock = nl_listener.init();
  if (sock <= 0) {
    cerr << "watchlink(), bad voodoo. exiting.." << endl;
    exit(1);
  }

  if (send_request) {
    cout << "sending initial netlink request" << endl;
    nl_listener.set_multipart(true);
    if (nl_send.send_get(sock, RTM_GETLINK) != 0) {
      cerr << "test_netlink(), error sending" << endl;
      exit(1);
    }
  }

  NetlinkLinkStatus nl_ls(sock, link_dir, debug);

  while (true) {
    //    cout << "test_netlink: now entering listening mode: " << endl;

    NetlinkEvent nl_event;
    if (nl_listener.process(nl_event) == true) {
      if (send_request) {
	if (nl_send.send_get(sock, RTM_GETADDR) != 0) {
	  cerr << "test_netlink(), error sending" << endl;
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
	cout << "  addr: " << nl_event.get_addr().str().c_str() << endl;
	cout << "  broadcast: " << nl_event.get_broadcast().str().c_str() << endl;
	char buf[20];
	sprintf(buf, "%d", nl_event.get_mask_len());
	cout << "  mask length: " << string(buf) << endl;
      }
      cout << endl;

}

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
