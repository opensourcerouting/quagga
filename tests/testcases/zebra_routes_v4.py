import contextlib
import time
import unittest

from testutils import quagga
from testutils import pyzclient
from testutils import system

class TestSimple(quagga.TestCase):
    def setUp(self):
        self.dummy1 = system.DummyIface()
        self.dummy1.up()
        self.dummy1.addr_add('192.0.2.1/29')

        self.dummy2 = system.DummyIface()
        self.dummy2.up()
        self.dummy2.addr_add('192.0.2.9/29')

        self.zebra = quagga.Zebra()

        self.zclient = pyzclient.ZClient(pyzclient.ZEBRA_ROUTE_OSPF)
        self.route = pyzclient.Route(None, '198.51.100.128/25')

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

        self.assertIn('198.51.100.128/25', self.zebra.rib('O'))
        self.assertRoutes({
            '198.51.100.128/25': {
                'nexthops': [
                    {
                        'gate': None,
                        'iface': self.dummy1.name,
                    },
                ],
            },
        }, system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', system.fib())
        self.assertNotIn('198.51.100.128/25', self.zebra.rib('O'))

    def test_nexthop_ipv4(self):
        self.route.add_nexthop('192.0.2.2')

        self.zclient.add_route(self.route)
        time.sleep(0.1)

        self.assertIn('198.51.100.128/25', self.zebra.rib('O'))
        self.assertRoutes({
            '198.51.100.128/25': {
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                    },
                ],
            },
        }, system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', system.fib())
        self.assertNotIn('198.51.100.128/25', self.zebra.rib('O'))

    def test_nexthop_ipv4_ifindex(self):
        self.route.add_nexthop('192.0.2.2', self.dummy1.index)
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        self.assertIn('198.51.100.128/25', self.zebra.rib('O'))
        self.assertRoutes({
            '198.51.100.128/25': {
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                    },
                ],
            },
        }, system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', system.fib())
        self.assertNotIn('198.51.100.128/25', self.zebra.rib('O'))

    def test_nexthop_ipv4_ifindex_invalid(self):
        self.route.add_nexthop('192.0.2.2', self.dummy2.index)
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        # Route should be in RIB, but not in fib
        # XXX: Is it right to mark that route as selected and active??
        self.assertRoutes({
            '198.51.100.128/25': {
#                'selected': False,
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy2.name,
                        'fib': False,
#                        'active': False
                    },
                ]
            },
        }, self.zebra.rib('O'))
        # Route shouldn't be in FIB
        self.assertNotIn('198.51.100.128/25', system.fib())

        # Make the gateway reachable by adding an address to the interface
        self.dummy2.addr_add('192.0.2.1/29')
        time.sleep(0.1)

        # RIB should now show nexthop as installed into FIB
        self.assertRoutes({
            '198.51.100.128/25': {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy2.name,
                        'fib': True,
                        'active': True
                    },
                ]
            },
        }, self.zebra.rib('O'))
        # Route should be in FIB
        self.assertRoutes({
            '198.51.100.128/25': {
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy2.name
                    }
                ]
            },
        }, system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', system.fib())
        self.assertNotIn('198.51.100.128/25', self.zebra.rib('O'))

    def test_nexthop_ipv4_invalid(self):
        self.route.add_nexthop('192.0.2.2')
        self.dummy1.addr_del('192.0.2.1/29')
        time.sleep(0.1)

        self.zclient.add_route(self.route)
        time.sleep(0.1)

        # Route should be in RIB, marked as inactive
        self.assertRoutes({
            '198.51.100.128/25': {
                'selected': False,
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': None,
                        'fib': False,
                        'active': False,
                    },
                ]
            },
        }, self.zebra.rib('O'))
        # Route shouldn't be in FIB
        self.assertNotIn('198.51.100.128/25', system.fib())

        # Make the gateway reachable by adding the address
        self.dummy1.addr_add('192.0.2.1/29')
        time.sleep(0.1)

        # RIB should show route as active and fib now
        self.assertRoutes({
            '198.51.100.128/25': {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                        'fib': True,
                        'active': True,
                    },
                ]
            },
        }, self.zebra.rib('O'))
        # route should be in fib now
        self.assertRoutes({
            '198.51.100.128/25': {
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                    },
                ]
            },
        }, system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', system.fib())
        self.assertNotIn('198.51.100.128/25', self.zebra.rib('O'))


class TestMultiPath(quagga.TestCase):
    def setUp(self):
        self.dummy1 = system.DummyIface()
        self.dummy1.up()
        self.dummy1.addr_add('192.0.2.1/25')

        self.dummy2 = system.DummyIface()
        self.dummy2.up()
        self.dummy2.addr_add('192.0.2.129/26')

        self.zebra = quagga.Zebra()

        self.zclient = pyzclient.ZClient(pyzclient.ZEBRA_ROUTE_OSPF)

    def tearDown(self):
        del self.zclient
        del self.zebra
        del self.dummy2
        del self.dummy1

    @unittest.skipUnless(system.SUPPORTS_MULTIPATH, "Platform doesn't support multipath")
    def test_nexthop_ipv4(self):
        route = pyzclient.Route(None, '198.51.100.128/25')
        route.nexthops.append(pyzclient.Nexthop('192.0.2.2'))
        route.nexthops.append(pyzclient.Nexthop('192.0.2.3'))
        self.zclient.add_route(route)
        time.sleep(0.1)

        expected_rib = {
            '198.51.100.128/25': {
                'selected': True,
                'nexthops': [
                    {
                        'fib': True,
                        'active': True,
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name
                    },
                    {
                        'fib': True,
                        'active': True,
                        'gate': '192.0.2.3',
                        'iface': self.dummy1.name
                    }
                ]
            }
        }

        expected_fib = {
            '198.51.100.128/25': {
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name
                    },
                    {
                        'gate': '192.0.2.3',
                        'iface': self.dummy1.name
                    }
                ]
            }
        }

        self.assertRoutes(expected_rib,self.zebra.rib('O'))
        self.assertRoutes(expected_fib,system.fib())

        self.zclient.del_route(route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', self.zebra.rib('O'))
        self.assertNotIn('198.51.100.128/25', system.fib())

        self.zclient.add_route(route)
        time.sleep(0.1)

        self.assertRoutes(expected_rib,self.zebra.rib('O'))
        self.assertRoutes(expected_fib,system.fib())

        route.nexhops = list(reversed(route.nexthops))
        self.zclient.del_route(route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', self.zebra.rib('O'))
        self.assertNotIn('198.51.100.128/25', system.fib())

    def test_excessive_nexthops(self):
        route = pyzclient.Route(None, '198.51.100.128/25')
        for i in range(2,126):
            route.nexthops.append(pyzclient.Nexthop('192.0.2.%d' % i))
        self.zclient.add_route(route)
        time.sleep(0.1)

        rib = self.zebra.rib('O')
        # Assert that the route has been completely installed into fib
        self.assertRoutes({
            '198.51.100.128/25': {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '192.0.2.%d' % i,
                    } for i in range(2,126)
                ]
            }
        }, rib)

        # Examine the FIB flags in the RIB and determine which nexthops
        # should be in the FIB
        fib_nexthops = []
        expected_fib = {
            '198.51.100.128/25': {
                'nexthops': fib_nexthops
            }
        }
        for nexthop in rib['198.51.100.128/25']['nexthops']:
            if not nexthop['fib']:
                continue

            fib_nexthops.append({
                'gate': nexthop['gate'],
                'iface': nexthop['iface']
            })

        # There should be at least one nexthop installed
        self.assertTrue(fib_nexthops)
        self.assertRoutes(expected_fib, system.fib())

        route.nexhops = list(reversed(route.nexthops))
        self.zclient.del_route(route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', self.zebra.rib('O'))
        self.assertNotIn('198.51.100.128/25', system.fib())

    @unittest.skipUnless(system.SUPPORTS_MULTIPATH, "Platform doesn't support multipath")
    def test_nexthop_ifindex(self):
        route = pyzclient.Route(None, '198.51.100.128/25')
        route.nexthops.append(pyzclient.Nexthop(ifindex=self.dummy1.index))
        route.nexthops.append(pyzclient.Nexthop(ifindex=self.dummy2.index))

        expected_rib = {
            '198.51.100.128/25': {
                'selected': True,
                'nexthops': [
                    {
                        'gate': None,
                        'iface': self.dummy1.name
                    },
                    {
                        'gate': None,
                        'iface': self.dummy2.name
                    }
                ]
            }
        }

        expected_fib = {
            '198.51.100.128/25': {
                'nexthops': [
                    {
                        'gate': None,
                        'iface': self.dummy1.name
                    },
                    {
                        'gate': None,
                        'iface': self.dummy2.name
                    }
                ]
            }
        }

        self.zclient.add_route(route)
        time.sleep(0.1)

        self.assertRoutes(expected_rib, self.zebra.rib('O'))
        self.assertRoutes(expected_fib, system.fib())

        self.zclient.del_route(route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', self.zebra.rib('O'))
        self.assertNotIn('198.51.100.128/25', system.fib())

        self.zclient.add_route(route)
        time.sleep(0.1)

        self.assertRoutes(expected_rib, self.zebra.rib('O'))
        self.assertRoutes(expected_fib, system.fib())

        route.nexthops = list(reversed(route.nexthops))
        self.zclient.del_route(route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', self.zebra.rib('O'))
        self.assertNotIn('198.51.100.128/25', system.fib())


class TestRecursive(quagga.TestCase):
    maxDiff = None
    def setUp(self):
        self.dummy1 = system.DummyIface()
        self.dummy1.up()
        self.dummy1.addr_add('192.0.2.1/29')

        self.dummy2 = system.DummyIface()
        self.dummy2.up()
        self.dummy2.addr_add('192.0.2.9/29')

        self.zebra = quagga.Zebra()

        self.ospf_client = pyzclient.ZClient(pyzclient.ZEBRA_ROUTE_OSPF)

        route = pyzclient.Route(None, '192.0.2.16/29')
        route.add_nexthop(ifindex=self.dummy1.index)
        self.ospf_client.add_route(route)

        route = pyzclient.Route(None, '192.0.2.24/29')
        route.add_nexthop(ifindex=self.dummy2.index)
        self.ospf_client.add_route(route)

        route = pyzclient.Route(None, '192.0.2.32/29')
        route.add_nexthop(gate='192.0.2.2')
        self.ospf_client.add_route(route)

        route = pyzclient.Route(None, '192.0.2.40/29')
        route.add_nexthop(gate='192.0.2.3')
        self.ospf_client.add_route(route)

        self.bgp_client = pyzclient.ZClient(pyzclient.ZEBRA_ROUTE_BGP)
        self.route = pyzclient.Route(None, '198.51.100.128/25')
        self.route.rib_flags |= pyzclient.ZEBRA_FLAG_INTERNAL
        time.sleep(0.1)

    def setUpMultipath(self):
        route = pyzclient.Route(None, '192.0.2.48/29')
        route.add_nexthop(ifindex=self.dummy1.index)
        route.add_nexthop(ifindex=self.dummy2.index)
        self.ospf_client.add_route(route)

        route = pyzclient.Route(None, '192.0.2.56/29')
        route.add_nexthop(gate='192.0.2.4')
        route.add_nexthop(gate='192.0.2.5')
        self.ospf_client.add_route(route)

        route = pyzclient.Route(None, '192.0.2.64/29')
        route.add_nexthop(gate='192.0.2.6', ifindex=self.dummy1.index)
        route.add_nexthop(gate='192.0.2.10', ifindex=self.dummy2.index)
        self.ospf_client.add_route(route)
        time.sleep(0.1)

    def tearDown(self):
        del self.route
        del self.bgp_client
        del self.ospf_client
        del self.zebra
        del self.dummy2
        del self.dummy1

    def test_unresolvable(self):
        self.zebra.config('ip route 192.0.2.255/32 reject')
        self.route.add_nexthop('192.0.2.255')
        self.bgp_client.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            '198.51.100.128/25': {
                'selected': False,
                'nexthops': [
                    {
                        'gate': '192.0.2.255',
                        'iface': None,
                        'active': False,
                        'fib': False
                    }
                ]
            }
        }, self.zebra.rib('B'))
        self.assertNotIn('198.51.100.128/25', system.fib())

        self.bgp_client.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', self.zebra.rib('B'))

        self.zebra.config('no ip route 192.0.2.255/32 reject')
        self.zebra.config('ip route 192.0.2.255/32 Null0')
        self.bgp_client.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            '198.51.100.128/25': {
                'selected': False,
                'nexthops': [
                    {
                        'gate': '192.0.2.255',
                        'iface': None,
                        'active': False,
                        'fib': False
                    }
                ]
            }
        }, self.zebra.rib('B'))
        self.assertNotIn('198.51.100.128/25', system.fib())

        self.bgp_client.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', self.zebra.rib('B'))

    def test_resolve_via_ifindex(self):
        self.route.add_nexthop('192.0.2.17')
        self.bgp_client.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            '198.51.100.128/25': {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '192.0.2.17',
                        'resolved': [
                            {
                                'gate': '192.0.2.17',
                                'iface': self.dummy1.name
                            }
                        ]
                    }
                ]
            }
        }, self.zebra.rib('B'))
        self.assertRoutes({
            '198.51.100.128/25': {
                'nexthops': [
                    {
                        'gate': '192.0.2.17',
                        'iface': self.dummy1.name
                    }
                ]
            }
        }, system.fib())

        self.bgp_client.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', self.zebra.rib('B'))
        self.assertNotIn('198.51.100.128/25', system.fib())

    @unittest.skipUnless(system.SUPPORTS_MULTIPATH, "Platform doesn't support multipath")
    def test_resolve_via_multipath_ifindex(self):
        self.setUpMultipath()
        self.route.add_nexthop('192.0.2.49')
        self.bgp_client.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            '198.51.100.128/25': {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '192.0.2.49',
                        'resolved': [
                            {
                                'gate': '192.0.2.49',
                                'iface': self.dummy1.name
                            },
                            {
                                'gate': '192.0.2.49',
                                'iface': self.dummy2.name
                            }
                        ]
                    }
                ]
            }
        }, self.zebra.rib('B'))
        self.assertRoutes({
            '198.51.100.128/25': {
                'nexthops': [
                    {
                        'gate': '192.0.2.49',
                        'iface': self.dummy1.name
                    },
                    {
                        'gate': '192.0.2.49',
                        'iface': self.dummy2.name
                    }
                ]
            }
        }, system.fib())

        self.bgp_client.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', self.zebra.rib('B'))
        self.assertNotIn('198.51.100.128/25', system.fib())

    def test_resolve_via_ipv4(self):
        self.route.add_nexthop('192.0.2.33')
        self.bgp_client.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            '198.51.100.128/25': {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '192.0.2.33',
                        'resolved': [
                            {
                                'gate': '192.0.2.2',
                                'iface': self.dummy1.name
                            }
                        ]
                    }
                ]
            }
        }, self.zebra.rib('B'))
        self.assertRoutes({
            '198.51.100.128/25': {
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name
                    }
                ]
            }
        }, system.fib())

        self.bgp_client.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', self.zebra.rib('B'))
        self.assertNotIn('198.51.100.128/25', system.fib())

    @unittest.skipUnless(system.SUPPORTS_MULTIPATH, "Platform doesn't support multipath")
    def test_resolve_via_multipath_ipv4(self):
        self.setUpMultipath()
        self.route.add_nexthop('192.0.2.57')
        self.bgp_client.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            '198.51.100.128/25': {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '192.0.2.57',
                        'resolved': [
                            {
                                'gate': '192.0.2.4',
                                'iface': self.dummy1.name
                            },
                            {
                                'gate': '192.0.2.5',
                                'iface': self.dummy1.name
                            }
                        ]
                    }
                ]
            }
        }, self.zebra.rib('B'))

        self.assertRoutes({
            '198.51.100.128/25': {
                'nexthops': [
                    {
                        'gate': '192.0.2.4',
                        'iface': self.dummy1.name
                    },
                    {
                        'gate': '192.0.2.5',
                        'iface': self.dummy1.name
                    }
                ]
            }
        }, system.fib())

        self.bgp_client.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn('198.51.100.128/25', self.zebra.rib('B'))
        self.assertNotIn('198.51.100.128/25', system.fib())


@unittest.skipUnless(system.SUPPORTS_PREFSRC, "Platform doesn't support prefsrc")
class TestRouteMapSrc(quagga.TestCase):
    def setUp(self):
        self.dummy1 = system.DummyIface()
        self.dummy1.up()
        self.dummy1.addr_add('192.0.2.1/29')

        self.dummy2 = system.DummyIface()
        self.dummy2.up()
        self.dummy2.addr_add('192.0.2.9/29')

        self.dummy3 = system.DummyIface()
        self.dummy3.up()
        self.dummy3.addr_add('192.0.2.255/32')

        self.zebra = quagga.Zebra()

        self.zebra.config(
            "route-map src-test permit 10",
            "set src 192.0.2.255",
            "ip protocol ospf route-map src-test"
        )

        self.zclient = pyzclient.ZClient(pyzclient.ZEBRA_ROUTE_OSPF)
        self.route = pyzclient.Route(None, '198.51.100.128/25')

    def tearDown(self):
        del self.route
        del self.zclient
        del self.zebra
        del self.dummy3
        del self.dummy2
        del self.dummy1

    def test_singlepath_ifindex(self):
        self.route.add_nexthop(ifindex=self.dummy1.index)
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        # XXX: Src is currently not displayed for NEXTHOP_TYPE_IFINDEX
        self.assertRoutes({
            str(self.route.dest): {
                'selected': True,
                'nexthops': [
                    {
                        'active': True,
                        'fib': True,
                        'gate': None,
                        'iface': self.dummy1.name,
#                        'src': '192.0.2.255'
                    }
                ]
            }
        }, self.zebra.rib('O'))
        self.assertRoutes({
            str(self.route.dest): {
                'src': '192.0.2.255',
                'nexthops': [
                    {
                        'gate': None,
                        'iface': self.dummy1.name,
                    }
                ]
            }
        }, system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), self.zebra.rib('O'))
        self.assertNotIn(str(self.route.dest), system.fib())

    def test_singlepath_ipv4(self):
        self.route.add_nexthop('192.0.2.2')
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            str(self.route.dest): {
                'selected': True,
                'nexthops': [
                    {
                        'active': True,
                        'fib': True,
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                        'src': '192.0.2.255'
                    }
                ]
            }
        }, self.zebra.rib('O'))
        self.assertRoutes({
            str(self.route.dest): {
                'src': '192.0.2.255',
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                    }
                ]
            }
        }, system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), self.zebra.rib('O'))
        self.assertNotIn(str(self.route.dest), system.fib())

    def test_singlepath_ipv4_ifindex(self):
        self.route.add_nexthop('192.0.2.2', self.dummy1.index)
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            str(self.route.dest): {
                'selected': True,
                'nexthops': [
                    {
                        'active': True,
                        'fib': True,
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                        'src': '192.0.2.255'
                    }
                ]
            }
        }, self.zebra.rib('O'))
        self.assertRoutes({
            str(self.route.dest): {
                'src': '192.0.2.255',
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                    }
                ]
            }
        }, system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), self.zebra.rib('O'))
        self.assertNotIn(str(self.route.dest), system.fib())

    def test_multipath_ifindex(self):
        self.route.add_nexthop(ifindex=self.dummy1.index)
        self.route.add_nexthop(ifindex=self.dummy2.index)
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        # XXX: Src is currently not displayed for NEXTHOP_TYPE_IFINDEX
        self.assertRoutes({
            str(self.route.dest): {
                'selected': True,
                'nexthops': [
                    {
                        'active': True,
                        'fib': True,
                        'gate': None,
                        'iface': self.dummy1.name,
#                        'src': '192.0.2.255'
                    },
                    {
                        'active': True,
                        'fib': True,
                        'gate': None,
                        'iface': self.dummy2.name,
#                        'src': '192.0.2.255'
                    },
                ]
            }
        }, self.zebra.rib('O'))
        self.assertRoutes({
            str(self.route.dest): {
                'src': '192.0.2.255',
                'nexthops': [
                    {
                        'gate': None,
                        'iface': self.dummy1.name,
                    },
                    {
                        'gate': None,
                        'iface': self.dummy2.name,
                    }
                ]
            }
        }, system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), self.zebra.rib('O'))
        self.assertNotIn(str(self.route.dest), system.fib())

    def test_multipath_ipv4(self):
        self.route.add_nexthop('192.0.2.2')
        self.route.add_nexthop('192.0.2.3')
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            str(self.route.dest): {
                'selected': True,
                'nexthops': [
                    {
                        'active': True,
                        'fib': True,
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                        'src': '192.0.2.255'
                    },
                    {
                        'active': True,
                        'fib': True,
                        'gate': '192.0.2.3',
                        'iface': self.dummy1.name,
                        'src': '192.0.2.255'
                    },
                ]
            }
        }, self.zebra.rib('O'))
        self.assertRoutes({
            str(self.route.dest): {
                'src': '192.0.2.255',
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                    },
                    {
                        'gate': '192.0.2.3',
                        'iface': self.dummy1.name,
                    }
                ]
            }
        }, system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), self.zebra.rib('O'))
        self.assertNotIn(str(self.route.dest), system.fib())

    def test_multipath_ipv4_ifindex(self):
        self.route.add_nexthop('192.0.2.2', ifindex=self.dummy1.index)
        self.route.add_nexthop('192.0.2.3', ifindex=self.dummy1.index)
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            str(self.route.dest): {
                'selected': True,
                'nexthops': [
                    {
                        'active': True,
                        'fib': True,
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                        'src': '192.0.2.255'
                    },
                    {
                        'active': True,
                        'fib': True,
                        'gate': '192.0.2.3',
                        'iface': self.dummy1.name,
                        'src': '192.0.2.255'
                    },
                ]
            }
        }, self.zebra.rib('O'))
        self.assertRoutes({
            str(self.route.dest): {
                'src': '192.0.2.255',
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                    },
                    {
                        'gate': '192.0.2.3',
                        'iface': self.dummy1.name,
                    }
                ]
            }
        }, system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), self.zebra.rib('O'))
        self.assertNotIn(str(self.route.dest), system.fib())

class TestRouteMapMatchAndDeny(quagga.TestCase):
    def setUp(self):
        self.dummy1 = system.DummyIface()
        self.dummy1.up()
        self.dummy1.addr_add('192.0.2.1/29')

        self.dummy2 = system.DummyIface()
        self.dummy2.up()
        self.dummy2.addr_add('192.0.2.9/29')

        self.zebra = quagga.Zebra()

        self.zclient = pyzclient.ZClient(pyzclient.ZEBRA_ROUTE_OSPF)
        self.route = pyzclient.Route(None, '198.51.100.128/25')

    def tearDown(self):
        del self.route
        del self.zclient
        del self.zebra
        del self.dummy2
        del self.dummy1

    def setUpAccessList(self):
        self.zebra.config(
            "access-list 10 permit 192.0.2.0 0.0.0.7",
            "access-list 10 deny any",
            "route-map match-test permit 10",
            "match ip next-hop 10",
            "route-map match-test deny 20",
            "ip protocol ospf route-map match-test"
        )

    def setUpPrefixList(self):
        self.zebra.config(
            "ip prefix-list test1 seq 10 permit 192.0.2.0/29 le 32",
            "ip prefix-list test1 seq 20 deny any",
            "route-map match-test permit 10",
            "match ip next-hop prefix-list test1",
            "route-map match-test deny 20",
            "ip protocol ospf route-map match-test"
        )

    def setUpIfaceMatch(self):
        self.zebra.config(
            "route-map match-test permit 10",
            "match interface {0}".format(self.dummy1.name),
            "route-map match-test deny 20",
            "ip protocol ospf route-map match-test"
        )

    def do_singlepath_test(self):
        # This route should make it through the filter
        self.route.add_nexthop('192.0.2.2')
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            str(self.route.dest): {
                'selected': True,
                'nexthops': [
                    {
                        'active': True,
                        'fib': True,
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                    }
                ]
            }
        }, self.zebra.rib('O'))
        self.assertRoutes({
            str(self.route.dest): {
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                    }
                ]
            }
        }, system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), self.zebra.rib('O'))
        self.assertNotIn(str(self.route.dest), system.fib())

        # This route should be filtered by the route-map
        self.route.nexthops = []
        self.route.add_nexthop('192.0.2.10')
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            str(self.route.dest): {
                'selected': False,
                'nexthops': [
                    {
                        'active': False,
                        'fib': False,
                        'gate': '192.0.2.10',
                        'iface': self.dummy2.name,
                    }
                ]
            }
        }, self.zebra.rib('O'))
        self.assertNotIn(str(self.route.dest), system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), self.zebra.rib('O'))
        self.assertNotIn(str(self.route.dest), system.fib())

    def test_singlepath_access_list(self):
        self.setUpAccessList()
        self.do_singlepath_test()

    def test_singlepath_prefix_list(self):
        self.setUpPrefixList()
        self.do_singlepath_test()

    def test_singlepath_interface_match(self):
        self.setUpIfaceMatch()
        self.do_singlepath_test()

    def do_multipath_test(self):
        self.route.add_nexthop('192.0.2.2')
        self.route.add_nexthop('192.0.2.10')
        self.zclient.add_route(self.route)
        time.sleep(0.1)

        self.assertRoutes({
            str(self.route.dest): {
                'selected': True,
                'nexthops': [
                    {
                        'active': True,
                        'fib': True,
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                    },
                    {
                        'active': False,
                        'fib': False,
                        'gate': '192.0.2.10',
                        'iface': self.dummy2.name,
                    }
                ]
            }
        }, self.zebra.rib('O'))
        self.assertRoutes({
            str(self.route.dest): {
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy1.name,
                    }
                ]
            }
        }, system.fib())

        self.zclient.del_route(self.route)
        time.sleep(0.1)

        self.assertNotIn(str(self.route.dest), self.zebra.rib('O'))
        self.assertNotIn(str(self.route.dest), system.fib())

    def test_multipath_access_list(self):
        self.setUpAccessList()
        self.do_multipath_test()

    def test_multipath_prefix_list(self):
        self.setUpPrefixList()
        self.do_multipath_test()

    def test_multipath_interface_match(self):
        self.setUpIfaceMatch()
        self.do_multipath_test()

    def test_recursive_deactivate(self):
        self.setUpPrefixList()
        self.zebra.config("no ip protocol ospf")
        self.zebra.config("ip protocol bgp route-map match-test")

        resolving_route = pyzclient.Route(None, '192.0.2.16/29')
        resolving_route.add_nexthop('192.0.2.2')
        self.zclient.add_route(resolving_route)
        time.sleep(0.1)

        with contextlib.closing(pyzclient.ZClient(pyzclient.ZEBRA_ROUTE_BGP)) as bgp_client:
            self.route.rib_flags |= pyzclient.ZEBRA_FLAG_INTERNAL
            self.route.add_nexthop('192.0.2.17')
            bgp_client.add_route(self.route)
            time.sleep(0.1)

            # 192.0.2.17 isn't matched by the prefix list, therefore, the route is rejected
            self.assertRoutes({
                str(self.route.dest): {
                    'selected': False,
                    'nexthops': [
                        {
                            'gate': '192.0.2.17',
                            'fib': False,
                            'active': False
                        }
                    ]
                }
            }, self.zebra.rib('B'))
            self.assertNotIn(str(self.route.dest), system.fib())

            bgp_client.del_route(self.route)
            time.sleep(0.1)

            self.assertNotIn(str(self.route.dest), self.zebra.rib('B'))
            self.assertNotIn(str(self.route.dest), system.fib())

            # permit 192.0.2.17 and re-add route
            self.zebra.config("ip prefix-list test1 seq 10 permit 192.0.2.17/32")

            bgp_client.add_route(self.route)
            time.sleep(0.1)

            self.assertRoutes({
                str(self.route.dest): {
                    'selected': True,
                    'nexthops': [
                        {
                            'active': True,
                            'gate': '192.0.2.17',
                            'resolved': [
                                {
                                    'gate': '192.0.2.2',
                                    'iface': self.dummy1.name,
                                    'fib': True,
                                    'active': True
                                }
                            ],
                        }
                    ]
                }
            }, self.zebra.rib('B'))
            self.assertRoutes({
                str(self.route.dest): {
                    'nexthops': [
                        {
                            'gate': '192.0.2.2',
                            'iface': self.dummy1.name
                        }
                    ]
                }
            }, system.fib())

            bgp_client.del_route(self.route)
            time.sleep(0.1)

            self.assertNotIn(str(self.route.dest), self.zebra.rib('B'))
            self.assertNotIn(str(self.route.dest), system.fib())

if __name__ == '__main__':
    unittest.main()
