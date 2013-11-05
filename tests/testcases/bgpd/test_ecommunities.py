from testutils import expect
from testutils import requires

def run_test():
    program = expect.spawn('./testcases/bgpd/test_ecommunities 2>/dev/null')
    program.set_requirements(requires.bgpd)
    program.set_ok_pattern('OK') # Test program uses non-colored OK string

    yield program.multiline_test('ipaddr', 'ipaddr: ')
    yield program.multiline_test('ipaddr-so', 'ipaddr-so: ')
    yield program.multiline_test('asn', 'asn: ')
    yield program.multiline_test('asn4', 'asn4: ')

    yield program.finish()
