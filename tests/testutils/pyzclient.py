import ipaddr
import os
import re
import socket
import struct

# Load constants from headers
define_re = re.compile(r'^#define\s+(?P<name>[A-Z_0-9]+)\s+(?P<value>.*)$')

basedir = os.path.realpath(__file__)
basedir = os.path.dirname(basedir)
basedir = os.path.join(basedir, "../..")
for header in ["config.h",
               "lib/route_types.h",
               "lib/zclient.h",
               "lib/zebra.h"
              ]:
    with open(os.path.join(basedir, header)) as header_file:
        for line in header_file:
            match = define_re.match(line)
            if match is None:
                continue

            name = match.group('name')
            value = match.group('value').strip()
            if not value:
                continue

            if value[0] == '"':
                value = value[1:-1]
                try:
                    value = value.decode('string_escape')
                except ValueError:
                    continue
            else:
                try:
                    value = int(value, 0)
                except ValueError:
                    continue

            globals()[name] = value


def recv_all(input_socket, length):
    return_buf = bytes()

    while len(return_buf) < length:
        recv_buf = input_socket.recv(length - len(return_buf))
        if not recv_buf:
            raise RuntimeError("Connection closed.")
        return_buf += recv_buf

    return return_buf

def repr_constant(prefix, lookup_value):
    for key, value in globals().items():
        if not key.startswith(prefix):
            continue
        if value == lookup_value:
            return key
    return str(lookup_value)

class ZebraMessage(object):
    def __init__(self, command, payload):
        self.command = command
        self.payload = payload

    @classmethod
    def receive(cls, input_socket):
        header_format = '!HBBH'
        header_len = struct.calcsize(header_format)
        header_buf = recv_all(input_socket, header_len)

        msg_len, msg_marker, msg_version, msg_cmd = struct.unpack(
                header_format, header_buf)

        if msg_marker != ZEBRA_HEADER_MARKER:
            raise RuntimeError("Received zebra message with wrong marker")

        if msg_version != ZSERV_VERSION:
            raise RuntimeError("Received unsupported version of zebra message")

        msg_payload = recv_all(input_socket, msg_len - header_len)
        self = cls(msg_cmd, msg_payload)
        return self

    def serialize(self):
        if not self.command:
            raise RuntimeError("Trying to serialize abstract message.")

        header_format = '!HBBH'

        header = struct.pack(header_format,
                struct.calcsize(header_format) + len(self.payload),
                ZEBRA_HEADER_MARKER,
                ZSERV_VERSION,
                self.command,
        )

        return header + self.payload

    def __repr__(self):
        return 'ZebraMessage(command=%u, payload=%r)' % (
                self.command,
                self.payload
        )

class ZebraRouteTypeMessage(ZebraMessage):
    def __init__(self, route_type):
        payload = struct.pack('!B', route_type)
        ZebraMessage.__init__(self, 0, payload)

class Route(object):
    def __init__(self, route_type, dest, safi=SAFI_UNICAST):
        self.route_type = route_type
        self.dest = ipaddr.IPNetwork(dest)

        if self.dest.version == 4:
            self.afi = AFI_IP
        elif self.dest.version == 6:
            self.afi = AFI_IP6
        else:
            raise RuntimeError('Unsupported inet version')

        self.safi = safi
        self.nexthops = []
        self.rib_flags = 0
        self.distance = None
        self.metric = None

    def __repr__(self):
        return_value = 'Route(%s, %s, %r, nexthops=[\n' % (
                repr_constant('ZEBRA_ROUTE_', self.route_type),
                repr_constant('SAFI_', self.safi),
                self.dest
        )

        for nexthop in self.nexthops:
            return_value += '    %r,\n' % nexthop

        return_value += '])'

        return return_value

    def add_nexthop(self, *args, **kwargs):
        self.nexthops.append(Nexthop(*args, **kwargs))

class Nexthop(object):
    def __init__(self, gate=None, ifindex=None):
        if gate is not None:
            self.gate = ipaddr.IPAddress(gate)
        else:
            self.gate = None

        self.ifindex = ifindex

    @property
    def nexthop_type(self):
        if self.gate is None:
            if self.ifindex is not None:
                return ZEBRA_NEXTHOP_IFINDEX
            return 0

        if self.gate.version == 4:
            if self.ifindex is not None:
                return ZEBRA_NEXTHOP_IPV4_IFINDEX
            else:
                return ZEBRA_NEXTHOP_IPV4

        if self.gate.version == 6:
            if self.ifindex is not None:
                return ZEBRA_NEXTHOP_IPV6_IFINDEX
            else:
                return ZEBRA_NEXTHOP_IPV6

        raise RuntimeError('Unsupported inet version')

    def __repr__(self):
        args = {}
        if self.gate is not None:
            args['gate'] = self.gate
        if self.ifindex is not None:
            args['ifindex'] = self.ifindex

        return 'Nexthop(%s)' % (
            ', '.join([ '%s=%r' % item for item in args.items() ])
        )

class ZebraRouteMessage(ZebraMessage):
    def __init__(self, route):
        self.route = route

        message = 0

        # Prefixlen in bits
        payload = struct.pack('!B', route.dest.prefixlen)

        # Prefix padded to whole byte
        payload += route.dest.packed[:((route.dest.prefixlen + 7)/8)]

        if route.nexthops:
            message |= ZAPI_MESSAGE_NEXTHOP
            payload += struct.pack('!B', len(route.nexthops))

            for nexthop in route.nexthops:
                payload += struct.pack('!B', nexthop.nexthop_type)
                if nexthop.nexthop_type == ZEBRA_NEXTHOP_IFINDEX:
                    payload += struct.pack('!L', nexthop.ifindex)
                elif nexthop.nexthop_type == ZEBRA_NEXTHOP_IPV4:
                    payload += nexthop.gate.packed
                elif nexthop.nexthop_type == ZEBRA_NEXTHOP_IPV4_IFINDEX:
                    payload += nexthop.gate.packed
                    payload += struct.pack('!L', nexthop.ifindex)
                elif nexthop.nexthop_type == ZEBRA_NEXTHOP_IPV6:
                    payload += nexthop.gate.packed
                else:
                    raise RuntimeError("Unknown nexthop type")

        if route.distance is not None:
            message |= ZAPI_MESSAGE_DISTANCE
            payload += struct.pack('!B', route.distance)

        if route.metric is not None:
            message |= ZAPI_MESSAGE_METRIC
            payload += struct.pack('!B', route.metric)

        header = struct.pack('!BBBH',
                route.route_type,
                route.rib_flags,
                message,
                route.safi,
        )

        payload = header + payload
        ZebraMessage.__init__(self, 0, payload)

    def __repr__(self):
        return 'ZebraRouteMessage(%s, %r)' % (
            repr_constant('ZEBRA_IPV', self.command),
            self.route
        )

    @classmethod
    def parse(cls, afi, raw_message):
        buf = raw_message.payload

        header_format = '!BBBB'
        header_len = struct.calcsize(header_format)
        header_buf, buf = buf[:header_len], buf[header_len:]
        route_type, rib_flags, message, prefix_len = struct.unpack(
                                                        header_format,
                                                        header_buf
                                                    )

        prefix_bytes = (prefix_len + 7) / 8
        prefix_buf, buf = buf[:prefix_bytes], buf[prefix_bytes:]
        if afi == AFI_IP:
            prefix_buf += '\x00' * (4 - len(prefix_buf))
            prefix_str = socket.inet_ntop(socket.AF_INET, prefix_buf)
        elif afi == AFI_IP6:
            prefix_buf += '\x00' * (16 - len(prefix_buf))
            prefix_str = socket.inet_ntop(socket.AF_INET6, prefix_buf)
        else:
            raise RuntimeError("Unsupported AFI")
        prefix = '%s/%d' % (prefix_str, prefix_len)

        route = Route(route_type, prefix)
        route.rib_flags = rib_flags

        if message & ZAPI_MESSAGE_NEXTHOP:
            nexthop_num, = struct.unpack('!B', buf[:1])
            buf = buf[1:]

            for i in range(nexthop_num):
                if afi == AFI_IP:
                    nexthop_gate_buf, buf = buf[:4], buf[4:]
                    nexthop_gate = socket.inet_ntop(socket.AF_INET,
                                                    nexthop_gate_buf)
                elif afi == AFI_IP6:
                    nexthop_gate_buf, buf = buf[:16], buf[16:]
                    nexthop_gate = socket.inet_ntop(socket.AF_INET6,
                                                    nexthop_gate_buf)
                else:
                    raise RuntimeError("Unsupported AFI")

                if ipaddr.IPAddress(nexthop_gate).is_unspecified:
                    nexthop_gate = None

                nexthop_ifindex_format = '!BL'
                nexthop_ifindex_len = struct.calcsize(nexthop_ifindex_format)
                nexthop_ifindex_buf, buf = (buf[:nexthop_ifindex_len],
                                            buf[nexthop_ifindex_len:])

                nexthop_dummy, nexthop_ifindex = struct.unpack(
                                                    nexthop_ifindex_format,
                                                    nexthop_ifindex_buf
                                                 )
                if not nexthop_ifindex:
                    nexthop_ifindex = None

                nexthop = Nexthop(nexthop_gate, nexthop_ifindex)
                route.nexthops.append(nexthop)

        if message & ZAPI_MESSAGE_DISTANCE:
            route.distance, = struct.unpack('!B', buf[:1])
            buf = buf[1:]

        if message & ZAPI_MESSAGE_METRIC:
            route.metric, = struct.unpack('!L', buf[:4])
            buf = buf[4:]

        if len(buf):
            raise RuntimeError("Unexpected trailing data: %r" % buf)

        self = cls(route)
        self.command = raw_message.command
        return self


class ZClient(object):
    def __init__(self, route_type, auto_connect=True):
        self.route_type = route_type
        self.socket = None

        if auto_connect:
            self.connect()

    def connect(self):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.connect(ZEBRA_SERV_PATH)

        message = ZebraRouteTypeMessage(self.route_type)
        message.command = ZEBRA_HELLO
        self.socket.sendall(message.serialize())

    def close(self):
        self.socket.close()

    def recvmsg(self):
        raw_msg = ZebraMessage.receive(self.socket)
        if raw_msg.command in [ZEBRA_IPV4_ROUTE_ADD, ZEBRA_IPV4_ROUTE_DELETE]:
            real_msg = ZebraRouteMessage.parse(AFI_IP, raw_msg)
        elif raw_msg.command in [ZEBRA_IPV6_ROUTE_ADD, ZEBRA_IPV6_ROUTE_DELETE]:
            real_msg = ZebraRouteMessage.parse(AFI_IP6, raw_msg)
        else:
            real_msg = raw_msg

        return real_msg

    def add_redistribute(self, redist_type):
        message = ZebraRouteTypeMessage(redist_type)
        message.command = ZEBRA_REDISTRIBUTE_ADD
        self.socket.sendall(message.serialize())

    def del_redistribute(self, redist_type):
        message = ZebraRouteTypeMessage(redist_type)
        message.command = ZEBRA_REDISTRIBUTE_DELETE
        self.socket.sendall(message.serialize())

    def add_route(self, route):
        if route.route_type is None:
            route.route_type = self.route_type
        message = ZebraRouteMessage(route)
        if route.afi == AFI_IP:
            message.command = ZEBRA_IPV4_ROUTE_ADD
        elif route.afi == AFI_IP6:
            message.command = ZEBRA_IPV6_ROUTE_ADD
        else:
            raise RuntimeError('Unsupported afi')

        self.socket.sendall(message.serialize())

    def del_route(self, route):
        if route.route_type is None:
            route.route_type = self.route_type
        message = ZebraRouteMessage(route)
        if route.afi == AFI_IP:
            message.command = ZEBRA_IPV4_ROUTE_DELETE
        elif route.afi == AFI_IP6:
            message.command = ZEBRA_IPV6_ROUTE_DELETE
        else:
            raise RuntimeError('Unsupported afi')

        self.socket.sendall(message.serialize())
