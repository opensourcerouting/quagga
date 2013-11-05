import time

from testutils import quagga
from testutils import requires
from testutils import system

@requires.root
class TestAddDel(quagga.TestCase):
    maxDiff = None
    def setUp(self):
        self.dummy = system.DummyIface()
        self.zebra = quagga.Zebra()

    def tearDown(self):
        del self.zebra
        del self.dummy

    # TODO: these tests are badly written - we should have some that work
    # w/o ipv6
    @requires.ipv6
    def test_sys_addr_updown(self):
        self.dummy.up()
        self.dummy.addr_add('192.0.2.1/24')
        self.dummy.addr_add('2001:db8::1/64', 6)
        time.sleep(1)
        self.assertEqual(
            self.zebra.addr_list(self.dummy.name),
            self.dummy.addr_list())

        self.dummy.up(False)
        time.sleep(0.1)
        self.assertEqual(
            self.zebra.addr_list(self.dummy.name),
            self.dummy.addr_list())

        self.dummy.up()
        time.sleep(1)
        self.assertEqual(
            self.zebra.addr_list(self.dummy.name),
            self.dummy.addr_list())

    # TODO: these tests are badly written - we should have some that work
    # w/o ipv6
    @requires.ipv6
    def test_zeb_addr_updown(self):
        self.dummy.up()
        self.zebra.config(
            'interface %s' % (self.dummy.name),
            'ip address 192.0.2.1/24',
            'ipv6 address 2001:db8::1/64')
        time.sleep(1)
        syslist = self.dummy.addr_list()
        self.assertEqual(
            self.zebra.addr_list(self.dummy.name),
            syslist)
        self.assertIn((4, '192.0.2.1/24'), syslist)
        self.assertIn((6, '2001:db8::1/64'), syslist)

        self.dummy.up(False)
        time.sleep(0.1)
        self.assertEqual(
            self.zebra.addr_list(self.dummy.name),
            self.dummy.addr_list())

        self.dummy.up()
        time.sleep(1)
        self.assertEqual(
            self.zebra.addr_list(self.dummy.name),
            self.dummy.addr_list())

    def test_addrdel_routedel(self):
        self.dummy.up()
        self.dummy.addr_add('192.0.2.1/24')
        self.zebra.config('ip route 198.51.100.0/28 192.0.2.2')

        # Route should be in RIB and marked as active&fib
        self.assertRoutes({
            '198.51.100.0/28': {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy.name,
                        'fib': True,
                        'active': True,
                    },
                ]
            },
        }, self.zebra.rib('S'))
        # Route should be in fib
        self.assertRoutes({
            '198.51.100.0/28': {
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy.name
                    },
                ]
            },
        }, system.fib())

        self.dummy.addr_del('192.0.2.1/24')
        time.sleep(.1)

        # Route should be in RIB and marked as inactive
        self.assertRoutes({
            '198.51.100.0/28': {
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
        }, self.zebra.rib('S'))
        # Route should not be in FIB
        self.assertNotIn('198.51.100.0/28', system.fib())

        self.dummy.addr_add('192.0.2.1/24')
        time.sleep(.1)

        # Route should be in RIB and marked as active&fib
        self.assertRoutes({
            '198.51.100.0/28': {
                'selected': True,
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy.name,
                        'fib': True,
                        'active': True,
                    },
                ]
            },
        }, self.zebra.rib('S'))
        # Route should be in fib
        self.assertRoutes({
            '198.51.100.0/28': {
                'nexthops': [
                    {
                        'gate': '192.0.2.2',
                        'iface': self.dummy.name
                    },
                ]
            },
        }, system.fib())
