import time
import unittest

from testutils import quagga
from testutils import pyzclient
from testutils import system

class TestSimple(quagga.TestCase):
    def setUp(self):
        self.dummy1 = system.DummyIface()
        self.dummy1.up()
        self.dummy1.addr_add('2001:db8:1:1::1/64', 6)

        self.dummy2 = system.DummyIface()
        self.dummy2.up()
        self.dummy2.addr_add('2001:db8:1:2::1/64', 6)

        self.zebra = quagga.Zebra()

        self.zclient = pyzclient.ZClient(pyzclient.ZEBRA_ROUTE_OSPF6)
        self.route = pyzclient.Route(None, '2001:db8:2::/48')

    def tearDown(self):
        del self.route
        del self.zclient
        del self.zebra
        del self.dummy2
        del self.dummy1

    def test_nexthop_ifindex(self):
        self.route.add_nexthop(ifindex=self.dummy1.index)
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        self.assertIn(str(self.route.dest), self.zebra.rib('O', 6))
        self.assertRoutes({
            str(self.route.dest): {
                'nexthops': [
                    {
                        'gate': None,
                        'iface': self.dummy1.name,
                    },
                ],
            },
        }, system.fib(6))

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), system.fib(6))
        self.assertNotIn(str(self.route.dest), self.zebra.rib('O',6))

    def test_nexthop_ipv6(self):
        self.route.add_nexthop('2001:db8:1:1::2')

        self.zclient.add_route(self.route)
        time.sleep(0.1)

        self.assertIn(str(self.route.dest), self.zebra.rib('O',6))
        self.assertRoutes({
            str(self.route.dest): {
                'nexthops': [
                    {
                        'gate': '2001:db8:1:1::2',
                        'iface': self.dummy1.name,
                    },
                ],
            },
        }, system.fib(6))

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), system.fib(6))
        self.assertNotIn(str(self.route.dest), self.zebra.rib('O',6))

    def test_nexthop_ipv6_ifindex(self):
        # There is currently no multipath support for IPv6 :(
        # Zebra will just merge all the nexthop information
        # into one :/

        self.route.add_nexthop('fe80::23')
        self.route.add_nexthop(ifindex=self.dummy1.index)
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        self.assertIn(str(self.route.dest), self.zebra.rib('O',6))
        self.assertRoutes({
            str(self.route.dest): {
                'nexthops': [
                    {
                        'gate': 'fe80::23',
                        'iface': self.dummy1.name,
                    },
                ],
            },
        }, system.fib(6))

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), system.fib(6))
        self.assertNotIn(str(self.route.dest), self.zebra.rib('O', 6))

    def test_nexthop_ipv6_ifindex_invalid(self):
        self.route.add_nexthop('2001:db8:1:1::2')
        self.route.add_nexthop(ifindex=self.dummy2.index)
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        # Route should be in RIB, but not in fib
        # XXX: Is it right to mark that route as selected and active??
        self.assertRoutes({
            str(self.route.dest): {
#                'selected': False,
                'nexthops': [
                    {
                        'gate': '2001:db8:1:1::2',
                        'iface': self.dummy2.name,
                        'fib': False,
#                        'active': False
                    },
                ]
            },
        }, self.zebra.rib('O',6))
        # Route shouldn't be in FIB
        self.assertNotIn(str(self.route.dest), system.fib(6))

        # Make the gateway reachable by adding an address to the interface
        self.dummy2.addr_add('2001:db8:1:1::1/64', 6)
        time.sleep(0.1)

        # RIB should now show nexthop as installed into FIB
        self.assertRoutes({
            str(self.route.dest): {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '2001:db8:1:1::2',
                        'iface': self.dummy2.name,
                        'fib': True,
                        'active': True
                    },
                ]
            },
        }, self.zebra.rib('O',6))
        # Route should be in FIB
        self.assertRoutes({
            str(self.route.dest): {
                'nexthops': [
                    {
                        'gate': '2001:db8:1:1::2',
                        'iface': self.dummy2.name
                    }
                ]
            },
        }, system.fib(6))

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), system.fib(6))
        self.assertNotIn(str(self.route.dest), self.zebra.rib('O',6))

    def test_nexthop_ipv6_invalid(self):
        self.route.add_nexthop('2001:db8:1:1::2')
        self.dummy1.addr_del('2001:db8:1:1::1/64', 6)
        time.sleep(0.1)

        self.zclient.add_route(self.route)
        time.sleep(0.1)

        # Route should be in RIB, marked as inactive
        self.assertRoutes({
            str(self.route.dest): {
                'selected': False,
                'nexthops': [
                    {
                        'gate': '2001:db8:1:1::2',
                        'iface': None,
                        'fib': False,
                        'active': False,
                    },
                ]
            },
        }, self.zebra.rib('O',6))
        # Route shouldn't be in FIB
        self.assertNotIn(str(self.route.dest), system.fib(6))

        # Make the gateway reachable by adding the address
        self.dummy1.addr_add('2001:db8:1:1::1/64', 6)
        time.sleep(0.1)

        # RIB should show route as active and fib now
        self.assertRoutes({
            str(self.route.dest): {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '2001:db8:1:1::2',
                        'iface': self.dummy1.name,
                        'fib': True,
                        'active': True,
                    },
                ]
            },
        }, self.zebra.rib('O',6))
        # route should be in fib now
        self.assertRoutes({
            str(self.route.dest): {
                'nexthops': [
                    {
                        'gate': '2001:db8:1:1::2',
                    },
                ]
            },
        }, system.fib(6))

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), system.fib(6))
        self.assertNotIn(str(self.route.dest), self.zebra.rib('O',6))


class TestRecursive(quagga.TestCase):
    maxDiff = None
    def setUp(self):
        self.dummy1 = system.DummyIface()
        self.dummy1.up()
        self.dummy1.addr_add('2001:db8:1:1::1/64',6)

        self.zebra = quagga.Zebra()

        self.ospf_client = pyzclient.ZClient(pyzclient.ZEBRA_ROUTE_OSPF)

        route = pyzclient.Route(None, '2001:db8:1:3::/64')
        route.add_nexthop(ifindex=self.dummy1.index)
        self.ospf_client.add_route(route)

        route = pyzclient.Route(None, '2001:db8:1:4::/64')
        route.add_nexthop(gate='2001:db8:1:1::2')
        self.ospf_client.add_route(route)

        route = pyzclient.Route(None, '2001:db8:1:5::/64')
        route.add_nexthop(gate='fe80::42')
        route.add_nexthop(ifindex=self.dummy1.index)
        self.ospf_client.add_route(route)

        self.bgp_client = pyzclient.ZClient(pyzclient.ZEBRA_ROUTE_BGP)
        self.route = pyzclient.Route(None, '2001:db8:2::/48')
        self.route.rib_flags |= pyzclient.ZEBRA_FLAG_INTERNAL

    def tearDown(self):
        del self.route
        del self.bgp_client
        del self.ospf_client
        del self.zebra
        del self.dummy1

    def test_unresolvable(self):
        self.route.add_nexthop('2001:db8:3::1')
        self.bgp_client.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            str(self.route.dest): {
                'selected': False,
                'nexthops': [
                    {
                        'gate': '2001:db8:3::1',
                        'iface': None,
                        'active': False,
                        'fib': False
                    }
                ]
            }
        }, self.zebra.rib('B',6))
        self.assertNotIn('198.51.100.128/25', system.fib(6))

        self.bgp_client.del_route(self.route)
        time.sleep(0.1)
        self.assertNotIn('198.51.100.128/25', self.zebra.rib('B',6))

    def test_resolve_via_ifindex(self):
        self.route.add_nexthop('2001:db8:1:3::1')
        self.bgp_client.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            str(self.route.dest): {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '2001:db8:1:3::1',
                        'resolved': [
                            {
                                'gate': '2001:db8:1:3::1',
                                'iface': self.dummy1.name
                            }
                        ]
                    }
                ]
            }
        }, self.zebra.rib('B',6))
        self.assertRoutes({
            str(self.route.dest): {
                'nexthops': [
                    {
                        'gate': '2001:db8:1:3::1',
                        'iface': self.dummy1.name
                    }
                ]
            }
        }, system.fib(6))

        self.bgp_client.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), self.zebra.rib('B',6))
        self.assertNotIn(str(self.route.dest), system.fib(6))

    def test_resolve_via_ipv6(self):
        self.route.add_nexthop('2001:db8:1:4::1')
        self.bgp_client.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            str(self.route.dest): {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '2001:db8:1:4::1',
                        'resolved': [
                            {
                                'gate': '2001:db8:1:1::2',
                                'iface': self.dummy1.name
                            }
                        ]
                    }
                ]
            }
        }, self.zebra.rib('B',6))
        self.assertRoutes({
            str(self.route.dest): {
                'nexthops': [
                    {
                        'gate': '2001:db8:1:1::2',
                        'iface': self.dummy1.name
                    }
                ]
            }
        }, system.fib(6))

        self.bgp_client.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), self.zebra.rib('B',6))
        self.assertNotIn(str(self.route.dest), system.fib(6))

    def test_resolve_via_ipv6_ifindex(self):
        self.route.add_nexthop('2001:db8:1:5::1')
        self.bgp_client.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            str(self.route.dest): {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '2001:db8:1:5::1',
                        'resolved': [
                            {
                                'gate': 'fe80::42',
                                'iface': self.dummy1.name
                            }
                        ]
                    }
                ]
            }
        }, self.zebra.rib('B',6))
        self.assertRoutes({
            str(self.route.dest): {
                'nexthops': [
                    {
                        'gate': 'fe80::42',
                        'iface': self.dummy1.name
                    }
                ]
            }
        }, system.fib(6))

        self.bgp_client.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), self.zebra.rib('B'))
        self.assertNotIn(str(self.route.dest), system.fib(6))

if __name__ == '__main__':
    unittest.main()
