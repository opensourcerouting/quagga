# vim:set ts=4 expandtab:

import ctypes
import ctypes.util
import os
import re
import socket
import struct
import subprocess

import pexpect

from .. import generic

SUPPORTS_MULTIPATH = False
SUPPORTS_PREFSRC = False

_libc = ctypes.CDLL(ctypes.util.find_library('c'))

def if_nametoindex(name):
    return _libc.if_nametoindex(name)

def fib(ipv = 4):
    rlist = pexpect.spawn(
            'netstat -r -nW -f %s' % ('inet' if ipv == 4 else 'inet6'),
            logfile=open('/tmp/zebraip.log', 'a')
    )

    rlist.expect(r'Expire\s*\n')
    routes = {}
    while True:
        i = rlist.expect([
                r'^(?P<dest>(?:default|[\da-fA-F\:\.]+(?:%[^/\s]+)?(?:/\d+)?))' # Destination
                    r'\s+(?:link#\d+|(?P<gate>[\da-fA-F\:\.]+)(?:%[^\s]+)?|[^\s]+)' # Gateway
                    r'\s+(?P<flags>[A-Za-z\d]*)' # Flags
                    r'\s+\d+' # Refs
                    r'\s+\d+' # Use
                    r'\s+\d+' # Mtu
                    r'\s+(?P<iface>[^\s]+)' # Iface
                    r'.*?\n', # End of line
                pexpect.EOF
        ])

        if i == 1:
            break

        dst = rlist.match.group('dest')
        if dst == 'default':
            dst = '0.0.0.0/0' if ipv == 4 else '::/0'
        dst = dst.split('/')
        if len(dst) < 2: # Host route without prefixlen
            dst.append('32' if ipv == 4 else '128')
        if dst[0].find('%') >= 0: # Remove interface information
            dst[0] = dst[0][:dst[0].find('%')]
        dst = '/'.join(dst)

        gate = rlist.match.group('gate')
        if gate is not None and gate.find('%') >= 0:
            gate = gate[:gate.find('%')]
        iface = rlist.match.group('iface')

        routes[dst] = {
                'src': None, # Unsupported?
                'nexthops': [{
                    'gate': gate,
                    'iface': iface,
                }],
        }

    return routes

class DummyIface(object):
    def __init__(self):
        for i in range(0,64):
            self.name = 'disc{0}'.format(i)
            try:
                generic.call_log('ifconfig', self.name, 'create', stderr=open('/dev/null', 'w'))
            except subprocess.CalledProcessError:
                continue
            break
        else:
            self.name = None
            raise RuntimeError("Coulnd't create a suitable test interface")

        self.index = if_nametoindex(self.name)
        if self.index == 0:
            raise RuntimeError("The newly created interface has no ifindex. Strange.")

    def __del__(self):
        if self.name is None:
            return
        generic.call_log('ifconfig', self.name, 'destroy')

    def up(self, up = True):
        generic.call_log('ifconfig', self.name, 'up' if up else 'down')

    def addr_add(self, addr, ipv = 4):
        generic.call_log('ifconfig', self.name, 'inet' if ipv == 4 else 'inet6',
                         addr, 'alias')

    def addr_del(self, addr, ipv = 4):
        generic.call_log('ifconfig', self.name, 'inet' if ipv == 4 else 'inet6',
                         addr, '-alias')

    def addr_list(self):
        alist = pexpect.spawn('ifconfig ' + self.name)
        addrs = []
        while True:
            i = alist.expect ([
                r'inet (?P<addr>[\d\.]+) netmask (?P<netmask>0x[0-9a-fA-F]+).*?\n',
                r'inet6 (?P<addr>[0-9a-fA-F\:\.]+)(?:%[^\s]+)? prefixlen (?P<prefixlen>\d+).*?\n',
                pexpect.EOF])
            if i == 2:
                break
            elif i == 0:
                netmask = int(alist.match.group('netmask'), 16)
                host_bits = 0
                while netmask % 2 == 0:
                    host_bits += 1
                    netmask >>= 1
                prefixlen = 32 - host_bits
                addr = alist.match.group('addr')
                addrs.append((4, '%s/%d' % (addr, prefixlen)))
            elif i == 1:
                addr = alist.match.group('addr')
                prefixlen = alist.match.group('prefixlen')
                addrs.append((6, '%s/%s' % (addr, prefixlen)))
        return sorted(addrs)
