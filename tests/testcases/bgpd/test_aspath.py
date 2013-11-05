from testutils import expect
from testutils import requires

def run_test():
    program = expect.spawn('./testcases/bgpd/test_aspath 2>/dev/null')
    program.set_requirements(requires.bgpd)
    for parsertest in [
            "seq1", "seq2", "seq3", "seqset", "seqset2", "multi", "confed",
            "confed2", "confset", "confmulti", "seq4", "tripleseq1",
            "someprivate", "allprivate", "long", "seq1extra", "empty",
            "redundantset", "reconcile_lead_asp", "reconcile_new_asp",
            "reconcile_confed", "reconcile_start_trans",
            "reconcile_start_trans4", "reconcile_start_trans_error",
            "redundantset2", "zero-size overflow",
            "zero-size overflow + valid segment", "invalid segment type"
            ]:
        for testname in [parsertest, 'empty prepend {0}'.format(parsertest)]:
            yield program.multiline_test(testname, '{0}: '.format(testname))

    for i in range(10):
        testname = 'prepend test {0}'.format(i)
        yield program.multiline_test(testname, '{0}\r\n'.format(testname))

    for i in range(5):
        testname = 'aggregate test {0}'.format(i)
        yield program.multiline_test(testname, '{0}\r\n'.format(testname))

    for i in range(5):
        testname = 'reconcile test {0}'.format(i)
        yield program.multiline_test(testname, '{0}\r\n'.format(testname))

    for i in range(22):
        testname = 'compare {0}'.format(i)
        yield program.multiline_test(testname, 'left cmp ')

    yield program.multiline_test('empty_get_test', 'empty_get_test, ')

    for attrtest in [
            "basic test", "length too short", "length too long",
            "incorrect flag", "as4_path, with as2 format data",
            "as4, with incorrect attr length", "basic 4-byte as-path",
            "4b AS_PATH: too short", "4b AS_PATH: too long",
            "4b AS_PATH: too long2", "4b AS_PATH: bad flags",
            "4b AS4_PATH w/o AS_PATH", "4b AS4_PATH: confed"
            ]:
        yield program.multiline_test(attrtest, '{0}\r\n'.format(attrtest))

    yield program.finish()
