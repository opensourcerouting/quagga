#!/bin/bash
#
### BEGIN INIT INFO
# Provides: vyatta-quagga
# Required-Start: $local_fs $network $remote_fs $syslog
# Required-Stop: $local_fs $network $remote_fs $syslog
# Default-Start:  2 3 4 5
# Default-Stop: 0 1 6
# Short-Description: start and stop the Quagga routing suite
# Description: Quagga is a routing suite for IP routing protocols like 
#              BGP, OSPF, RIP and others. 
#
#	       Only zebra is started here, others are started by protocols
#	       during config process.
### END INIT INFO
#

. /lib/lsb/init-functions

declare progname=${0##*/}
declare action=$1; shift

pid_dir=/var/run/vyatta/quagga
log_dir=/var/log/vyatta/quagga

for dir in $pid_dir $log_dir ; do
    if [ ! -d $dir ]; then
	mkdir -p $dir
	chown quagga:quagga $dir
	chmod 755 $dir
    fi
done

pidfile=$pid_dir/zebra.pid

vyatta_quagga_start ()
{
    log_daemon_msg "Starting routing manager" "zebra"
    start-stop-daemon --start --quiet --oknodo \
	--chdir $log_dir \
	--exec /usr/sbin/vyatta-zebra \
    	-- -d -P 0 -l -S -s 1048576 -i $pidfile
    log_end_msg $?
}

vyatta_quagga_stop ()
{
    log_action_begin_msg "Stopping routing manager"
    for daemon in bgpd isisd ospfd ospf6d ripd ripngd
    do
	pidfile=$pid_dir/${daemon}.pid
	if [ -f $pidfile ]; then 
	    log_action_cont_msg "$daemon"
	    start-stop-daemon --stop --quiet --oknodo \
		--exec /usr/sbin/vyatta-${daemon}
	    rm $pidfile
	fi
    done    

    log_action_cont_msg "zebra"
    start-stop-daemon --stop --quiet --oknodo --retry 2 --exec /usr/sbin/vyatta-zebra
    log_action_end_msg $?

    log_begin_msg "Removing all Quagga Routes"
    ip route flush proto zebra
    log_end_msg $?
}

case "$action" in
    start)
	vyatta_quagga_start $*
    	;;
	
    stop|0)
	vyatta_quagga_stop $*
   	;;

    restart|force-reload)
	vyatta_quagga_stop $*
	sleep 2
	vyatta_quagga_start $*
	;;

    *)
    	echo "Usage: $progname {start|stop|restart|force-reload} [daemon...]"
	exit 1
	;;
esac
