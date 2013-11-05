from testutils import expect
from testutils import requires

def run_test():
    program = expect.spawn('./testcases/bgpd/test_mp_attr 2>/dev/null')
    program.set_requirements(requires.bgpd)

    yield program.multiline_test("IPv6: IPV6 MP Reach, global nexthop, 1 NLRI", requirements=requires.ipv6)
    yield program.multiline_test("IPv6-2: IPV6 MP Reach, global nexthop, 2 NLRIs", requirements=requires.ipv6)
    yield program.multiline_test("IPv6-default: IPV6 MP Reach, global nexthop, 2 NLRIs + default", requirements=requires.ipv6)
    yield program.multiline_test("IPv6-lnh: IPV6 MP Reach, global+local nexthops, 2 NLRIs + default", requirements=requires.ipv6)
    yield program.multiline_test("IPv6-nhlen: IPV6 MP Reach, inappropriate nexthop length")
    yield program.multiline_test("IPv6-nhlen2: IPV6 MP Reach, invalid nexthop length")
    yield program.multiline_test("IPv6-nhlen3: IPV6 MP Reach, nexthop length overflow")
    yield program.multiline_test("IPv6-nhlen4: IPV6 MP Reach, nexthop length short")
    yield program.multiline_test("IPv6-nlri: IPV6 MP Reach, NLRI bitlen overflow")
    yield program.multiline_test("IPv4: IPv4 MP Reach, 2 NLRIs + default")
    yield program.multiline_test("IPv4-nhlen: IPv4 MP Reach, nexthop lenth overflow")
    yield program.multiline_test("IPv4-nlrilen: IPv4 MP Reach, nlri lenth overflow")
    yield program.multiline_test("IPv4-MLVPN: IPv4/MPLS-labeled VPN MP Reach, RD, Nexthop, 3 NLRIs")
    yield program.multiline_test("IPv6-bug: IPv6, global nexthop, 1 default NLRI")
    yield program.multiline_test("IPv6-unreach: IPV6 MP Unreach, 1 NLRI", requirements=requires.ipv6)
    yield program.multiline_test("IPv6-unreach2: IPV6 MP Unreach, 2 NLRIs", requirements=requires.ipv6)
    yield program.multiline_test("IPv6-unreach-default: IPV6 MP Unreach, 2 NLRIs + default", requirements=requires.ipv6)
    yield program.multiline_test("IPv6-unreach-nlri: IPV6 MP Unreach, NLRI bitlen overflow")
    yield program.multiline_test("IPv4-unreach: IPv4 MP Unreach, 2 NLRIs + default")
    yield program.multiline_test("IPv4-unreach-nlrilen: IPv4 MP Unreach, nlri length overflow")
    yield program.multiline_test("IPv4-unreach-MLVPN: IPv4/MPLS-labeled VPN MP Unreach, RD, 3 NLRIs")
    yield program.finish()
