from testutils import expect

def run_test():
    program = expect.spawn("./testcases/zebra/test_nexthop_iterator")
    yield program.expect('Simple test passed.\r\n', 'Simple Test')
    yield program.expect('PRNG test passed.\r\n', 'PRNG Test')
    yield program.finish()
