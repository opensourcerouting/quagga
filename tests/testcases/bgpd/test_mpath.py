from testutils import expect
from testutils import requires

def run_test():
    program = expect.spawn('./testcases/bgpd/test_mpath 2>/dev/null')
    program.set_requirements(requires.bgpd)

    yield program.oneline_test("bgp maximum-paths config")
    yield program.oneline_test("bgp_mp_list")
    yield program.oneline_test("bgp_info_mpath_update")

    yield program.finish()
