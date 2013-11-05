from testutils import expect

def run_test():
    program = expect.spawn('./testcases/zebra/test_stream')

    yield program.expect("endp: 15, readable: 15, writeable: 1009", "test 1")
    yield program.expect("0xef 0xbe 0xef 0xde 0xad 0xbe 0xef 0xde 0xad 0xbe 0xef 0xde 0xad 0xbe 0xef", "test 2")
    yield program.expect("endp: 15, readable: 15, writeable: 0", "test 3")
    yield program.expect("0xef 0xbe 0xef 0xde 0xad 0xbe 0xef 0xde 0xad 0xbe 0xef 0xde 0xad 0xbe 0xef", "test 4")
    yield program.expect("c: 0xef", "test 5")
    yield program.expect("w: 0xbeef", "test 6")
    yield program.expect("l: 0xdeadbeef", "test 7")
    yield program.expect("q: 0xdeadbeefdeadbeef", "test 8")

    yield program.finish()
