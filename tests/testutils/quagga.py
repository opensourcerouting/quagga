# vim:set ts=4 expandtab:

import pprint
import re
import signal
import sys
import unittest

import pexpect

from . import generic

class Zebra(object):
    def __init__(self):
        self.zebra = pexpect.spawn(
                '../zebra/zebra -t',
                timeout = 5,
                logfile = file('/tmp/zebraio.log', 'a')
        )
        self.zebra.expect('# ')

        self.config(
            "debug zebra events",
            "debug zebra fpm",
            "debug zebra kernel",
            "debug zebra packet",
            "debug zebra rib",
            "log file /tmp/zebra.log"
        )

    def addr_list(self, ifname):
        self.zebra.sendline('show interface ' + ifname)
        addrs = []
        while True:
            i = self.zebra.expect ([
                r'inet ([\d\.]+/\d+).*?\r',
                r'inet6 ([0-9a-fA-F\:]+\/\d+).*?\r',
                '# '])
            if i == 2:
                break
            elif i == 0:
                addr = self.zebra.match.group(1)
                addrs.append((4, addr))
            elif i == 1:
                addr = self.zebra.match.group(1)
                addrs.append((6, addr))
        return sorted(addrs)

    def rib(self, protocol=None, ipv = 4):
        rib = self.route_list(ipv)
        if protocol is not None:
            rib = rib.get(protocol, {})
        return rib

    def route_list(self, ipv = 4):
        command = 'show ip%s route ' % ('' if ipv == 4 else 'v6')
        self.zebra.sendline(command)

        # Expect echo of command.
        self.zebra.expect(r'\n{0}'.format(command))

        # Expect prompt to show again.
        self.zebra.expect(r'# ')

        # Split output into lines and remove the prompt (last line)
        rib_info = self.zebra.before.replace('\r', '').splitlines()[:-1]

        # Clear empty lines at beginning and end
        while rib_info and not rib_info[0]:
            rib_info = rib_info[1:]

        while rib_info and not rib_info[-1]:
            rib_info = rib_info[:-1]

        rib = {}
        in_header = True
        for line in rib_info:
            # Skip the header
            if in_header:
                if not line:
                    in_header = False
                continue

            match = re.match(r'(?P<protocol>[A-Z])(?P<selected>[> ])(?P<fib>[\* ])'
                                 r' (?P<prefix>[\da-fA-F:\.]+/\d+)'
                                 r'(?: \[(?P<distance>\d+)/(?P<metric>\d+)\])? ',
                             line)
            if match is not None:
                # New prefix. Create and populate route dict
                protocol = match.group('protocol')
                selected = match.group('selected') == '>'
                # group fib will be used later
                prefix = match.group('prefix')
                distance = match.group('distance')
                metric = match.group('metric')

                generic.do_log("route_list: process route for {0}".format(prefix))

                current_route = {
                    'selected': selected,
                    'distance': distance,
                    'metric': metric,
                    'nexthops': []
                }

                if protocol not in rib:
                    rib[protocol] = {}

                rib[protocol][prefix] = current_route

                expected_len = len(' {0} '.format(prefix))
                if distance is not None:
                    expected_len += len(' [{0}/{1}]'.format(distance, metric))

                # The first nexthop never is the result of recursive resolution
                nexthop_recursive = False
            else:
                match = re.match(r'^  (?P<fib>[\* ])(?P<white> *)', line)
                if match is None:
                    raise AssertionError('Coulnd\'t parse line %r from zebra rib.' % line)

                # Only a new nexthop - check whether it was obtained by recursive resolution
                whitespace_len = len(match.group('white'))
                try:
                    assert whitespace_len == expected_len or whitespace_len == expected_len + 2
                except Exception:
                    print 'Unexpected indentation. Expected %d or %d but got %d' % (
                        expected_len, expected_len + 2, whitespace_len
                    )
                    raise

                nexthop_recursive =  whitespace_len > expected_len

            nexthop_fib = match.group('fib') == '*'

            nexthop = {
                'fib': nexthop_fib
            }

            tail = line[match.end():]
            # parse information about the nexthop type
            match = re.match(r'is directly connected, (?P<iface>[^\s,]+)', tail)
            if match is not None:
                nexthop['gate'] = None
            else:
                match = re.match(r'via (?P<gate>[\da-fA-F:\.]+)(?:, (?P<iface>[^\s,]+))?', tail)
                if match is None:
                    raise AssertionError('Couldn\'t parse nexthop for line %r from zebra rib.' % line)
                nexthop['gate'] = match.group('gate')
            nexthop['iface'] = match.group('iface')

            tail = tail[match.end():]
            match = re.match(r'(?P<inactive> inactive)?(?P<onlink> onlink)?(?P<recursive> \(recursive\))?'
                    r'(?:, src (?P<src>[\da-fA-F:\.]+))?(?P<blackhole>, bh)?(?P<reject>, rej)?'
                    r'(?:, (?P<uptime>[\d:wdhm]+))?',
                tail
            )

            nexthop['active'] = match.group('inactive') is None
            nexthop['onlink'] = match.group('onlink') is not None
            nexthop['recursive'] = match.group('recursive') is not None
            nexthop['src'] = match.group('src')
            nexthop['blackhole'] = match.group('blackhole') is not None
            nexthop['reject'] = match.group('reject') is not None
            nexthop['uptime'] = match.group('uptime')

            if nexthop_recursive:
                current_nexthop = current_route['nexthops'][-1]
                if 'resolved' not in current_nexthop:
                    current_nexthop['resolved'] = []

                current_nexthop['resolved'].append(nexthop)
            else:
                current_route['nexthops'].append(nexthop)

        return rib

    def config(self, *lines):
        self.zebra.sendline('configure terminal')
        self.zebra.expect('\(config\)#')
        for l in lines:
            self.zebra.sendline(l)
            self.zebra.expect('#')
        self.zebra.sendline('end')
        self.zebra.expect('#')

    def __del__(self):
        self.zebra.kill(signal.SIGTERM)
        del self.zebra

class TestCase(unittest.TestCase):
    def assertNexthop(self, check_nexthop, given_nexthop):
        for check_key, check_value in check_nexthop.items():
            self.assertIn(check_key, given_nexthop)
            if check_key == 'resolved':
                self.assertNexthops(check_value, given_nexthop[check_key])
            else:
                self.assertEqual(check_value, given_nexthop[check_key])

    def assertNexthops(self, check_nexthops, given_nexthops):
        def nexthop_key(nexthop):
            gate = nexthop.get('gate', None)
            iface = nexthop.get('iface', None)
            return (gate, iface)

        self.assertEqual(len(check_nexthops), len(given_nexthops))

        sorted_check_nexthops = sorted(check_nexthops, key=nexthop_key)
        sorted_given_nexthops = sorted(given_nexthops, key=nexthop_key)

        for check_nexthop, given_nexthop in zip(
                sorted_check_nexthops, sorted_given_nexthops):
            self.assertNexthop(check_nexthop, given_nexthop)

    def assertRoute(self, check_route, given_route):
        for check_key, check_value in check_route.items():
            self.assertIn(check_key, given_route)
            if check_key == 'nexthops':
                self.assertNexthops(check_value, given_route[check_key])
            else:
                self.assertEqual(check_value, given_route[check_key])

    def assertRoutes(self, check_routes, given_routes):
        try:
            for check_dest, check_info in check_routes.items():
                self.assertIn(check_dest, given_routes)
                self.assertRoute(check_info, given_routes[check_dest])
        except self.failureException:
            typ, exc, trace = sys.exc_info()

            args = list(exc.args)
            if not args:
                args[0] = ''

            args[0] += '\n' + 'The given routes don\'t match the given pattern.\n'
            args[0] += 'Given:\n'
            args[0] += pprint.pformat(given_routes)
            args[0] += '\n\nPattern:\n'
            args[0] += pprint.pformat(check_routes)

            exc.args = tuple(args)

            raise typ, exc, trace

