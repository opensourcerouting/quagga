from testutils import expect

def run_test():
    program = expect.spawn('./testcases/zebra/test_table')

    for i in range(6):
        yield program.expect("Verifying cmp", "cmp {0}".format(i))

    for i in range(11):
        yield program.expect("Verifying successor", "succ {0}".format(i))

    yield program.expect("Verified pausing", "pause")
    yield program.finish()
