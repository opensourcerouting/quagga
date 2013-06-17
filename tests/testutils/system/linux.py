# vim:set ts=4 expandtab:

import os
import re

import pexpect

from .. import generic

def fib(ipv = 4):
    rlist = pexpect.spawn('ip -%d -o route list' % (ipv),
                          logfile = open('/tmp/zebraip.log', 'a'))
    routes = {}
    while True:
        i = rlist.expect([
                r'^(?P<dest>(?:default|[\da-fA-F\:\.]+\/\d+)) ',
                pexpect.EOF
        ])

        if i == 1:
            break

        dst = rlist.match.group('dest')
        if dst == 'default':
            dst = '0.0.0.0/0' if ipv == 4 else '::/0'

        i = rlist.expect([
                r'^[^\n\\]*?(?:via (?P<gate>[\da-fA-F\:\.]+) +)?dev (?P<iface>[^\s]+).*?\n',
                r'^[^\n]*?\\'
        ])

        src_match = re.search(r'src ([\da-fA-F\:\.]+)', rlist.after)
        if src_match is None:
            src = None
        else:
            src = src_match.group(1)

        if i == 0: # single path
            gate = rlist.match.group('gate')
            iface = rlist.match.group('iface')

            routes[dst] = {
                    'src': src,
                    'nexthops': [{
                        'gate': gate,
                        'iface': iface,
                    }],
            }
            continue

        # multipath
        routes[dst] = {
            'src': src,
            'nexthops': []
        }

        while True:
            rlist.expect(r'^\s*nexthop +(?:via (?P<gate>[\da-fA-F\:\.]+) +)?dev (?P<iface>[^\s]+).*?(?P<term>\\|\n)')

            gate = rlist.match.group('gate')
            iface = rlist.match.group('iface')

            routes[dst]['nexthops'].append({
                'gate': gate,
                'iface': iface
            })

            if rlist.match.group('term') == '\n':
                break

    return routes

class DummyIface(object):
    def __init__(self):
        for i in range(0,64):
            self.name = 'ztest{0}'.format(i)
            if not os.access('/sys/class/net/' + self.name, os.F_OK):
                break
        else:
            raise RuntimeError("Coulnd't find a suitable test interface")

        generic.call_log('ip', 'link', 'add', 'name', self.name, 'type', 'dummy')

        with open('/sys/class/net/{0}/ifindex'.format(self.name), 'r') as f:
            self.index = int(f.read().strip())

    def __del__(self):
        generic.call_log('ip', 'link', 'del', self.name)

    def up(self, up = True):
        generic.call_log('ip', 'link', 'set', self.name, 'up' if up else 'down')

    def addr_add(self, addr, ipv = 4):
        generic.call_log('ip', '-%d' % (ipv), 'addr', 'add', addr, 'dev', self.name)

    def addr_del(self, addr, ipv = 4):
        generic.call_log('ip', '-%d' % (ipv), 'addr', 'del', addr, 'dev', self.name)

    def addr_list(self):
        alist = pexpect.spawn('ip -o addr list dev ' + self.name)
        addrs = []
        while True:
            i = alist.expect ([
                'inet ([\d\.]+/\d+)[^\n]+',
                'inet6 ([0-9a-fA-F\:]+\/\d+)[^\n]+',
                pexpect.EOF])
            if i == 2:
                break
            elif i == 0:
                addr = alist.match.group(1)
                addrs.append((4, addr))
            elif i == 1:
                addr = alist.match.group(1)
                addrs.append((6, addr))
        return sorted(addrs)
