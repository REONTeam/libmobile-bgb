#!/usr/bin/env python3

import sys
import os
import time
import socket
import struct
import subprocess
import multiprocessing
import threading
import select
import unittest


class BGBMaster:
    BGB_CMD_VERSION = 1
    BGB_CMD_JOYPAD = 101
    BGB_CMD_SYNC1 = 104
    BGB_CMD_SYNC2 = 105
    BGB_CMD_SYNC3 = 106
    BGB_CMD_STATUS = 108
    BGB_CMD_WANTDISCONNECT = 109

    def __init__(self, host="127.0.0.1", port=8765):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((host, port))
        sock.settimeout(1)
        sock.listen(1)
        self.sock = sock
        self.conn = None

        self.timeoffset = 0

    def close(self):
        if self.conn is not None:
            self.conn.close()
            self.conn = None
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def recv(self):
        try:
            pack = self.conn.recv(8)
        except BlockingIOError:
            return None
        unpack = struct.unpack("<BBBBI", pack)
        return {
            "cmd": unpack[0],
            "b2": unpack[1],
            "b3": unpack[2],
            "b4": unpack[3],
            "timestamp": unpack[4],
        }

    def send(self, pack):
        for field in ["b2", "b3", "b4", "timestamp"]:
            if field not in pack:
                pack[field] = 0
        pack = struct.pack(
            "<BBBBI",
            pack["cmd"],
            pack["b2"],
            pack["b3"],
            pack["b4"],
            pack["timestamp"]
        )
        self.conn.send(pack)

    def accept(self):
        try:
            conn, addr = self.sock.accept()
        except Exception as e:
            self.close()
            raise e
        self.sock.close()
        self.sock = None
        self.conn = conn
        self.conn.setblocking(True)

        pack = self.recv()
        ver = {
            "cmd": BGBMaster.BGB_CMD_VERSION,
            "b2": 1,
            "b3": 4,
            "b4": 0,
            "timestamp": 0,
        }
        for x in ver:
            if ver[x] != pack[x]:
                return False
        self.send(ver)

    def handle(self):
        pack = self.recv()
        if not pack:
            return 0,

        if pack["cmd"] in [BGBMaster.BGB_CMD_JOYPAD, BGBMaster.BGB_CMD_STATUS,
                           BGBMaster.BGB_CMD_SYNC3]:
            # Nothing to do
            return pack["cmd"],
        if pack["cmd"] == BGBMaster.BGB_CMD_SYNC2:
            return pack["cmd"], pack["b2"]

        print("BGBMaster.handle: Unhandled packet:", pack, file=sys.stderr)
        return 0,

    def add_time(self, offset):
        # Offset in seconds
        self.timeoffset += offset
        self.update()

    def get_time(self):
        return int((time.time() + self.timeoffset) * (1 << 21)) & 0x7FFFFFFF

    def update(self):
        pack = {
            "cmd": BGBMaster.BGB_CMD_SYNC3,
            "timestamp": self.get_time(),
        }
        self.send(pack)

    def transfer(self, byte):
        pack = {
            "cmd": BGBMaster.BGB_CMD_SYNC1,
            "b2": byte,
            "b3": 0x81,
            "timestamp": self.get_time(),
        }
        self.send(pack)

        while True:
            res = self.handle()
            if res[0] == BGBMaster.BGB_CMD_SYNC2:
                byte_ret = res[1]
                break
        # print("%02X %02X" % (byte, byte_ret))
        return byte_ret


class MobileCmdError(Exception):
    def __init__(self, code, *args):
        super().__init__(*args)
        self.code = code


class Mobile:
    MOBILE_COMMAND_BEGIN_SESSION = 0x10
    MOBILE_COMMAND_END_SESSION = 0x11
    MOBILE_COMMAND_DIAL_TELEPHONE = 0x12
    MOBILE_COMMAND_HANG_UP_TELEPHONE = 0x13
    MOBILE_COMMAND_WAIT_FOR_TELEPHONE_CALL = 0x14
    MOBILE_COMMAND_TRANSFER_DATA = 0x15
    MOBILE_COMMAND_RESET = 0x16
    MOBILE_COMMAND_TELEPHONE_STATUS = 0x17
    MOBILE_COMMAND_SIO32_MODE = 0x18
    MOBILE_COMMAND_READ_CONFIGURATION_DATA = 0x19
    MOBILE_COMMAND_WRITE_CONFIGURATION_DATA = 0x1A
    MOBILE_COMMAND_TRANSFER_DATA_END = 0x1F
    MOBILE_COMMAND_ISP_LOGIN = 0x21
    MOBILE_COMMAND_ISP_LOGOUT = 0x22
    MOBILE_COMMAND_OPEN_TCP_CONNECTION = 0x23
    MOBILE_COMMAND_CLOSE_TCP_CONNECTION = 0x24
    MOBILE_COMMAND_OPEN_UDP_CONNECTION = 0x25
    MOBILE_COMMAND_CLOSE_UDP_CONNECTION = 0x26
    MOBILE_COMMAND_DNS_QUERY = 0x28
    MOBILE_COMMAND_FIRMWARE_VERSION = 0x3F
    MOBILE_COMMAND_ERROR = 0x6E

    def __init__(self, bus):
        self.bus = bus

    def transfer(self, cmd, *args, error=False):
        self.bus.update()

        data = list(map(lambda x: x & 0xFF, args))
        full = [0x99, 0x66, cmd, 0, 0, len(data), *data]
        cksum = cmd + len(data) + sum(data)
        full += [(cksum >> 8) & 0xFF, cksum & 0xFF]
        for byte in full:
            res = self.bus.transfer(byte)
            if res != 0xD2:
                raise Exception(
                        "Mobile.transfer: Unexpected idle byte: %02X"
                        % res)

        self.bus.transfer(0x80)
        err = self.bus.transfer(0)
        if err != cmd ^ 0x80:
            raise Exception(
                    "Mobile.transfer: Unexpected acknowledgement byte: %02X"
                    % err)

        while True:
            timeout = 0
            while self.bus.transfer(0x4B) != 0x99:
                if timeout >= 200:
                    raise Exception("Mobile.transfer: wait timeout")
                timeout += 1
                time.sleep(0.01)
                continue
            if self.bus.transfer(0x4B) == 0x66:
                break

        pack = bytearray()
        for x in range(4):
            pack.append(self.bus.transfer(0x4B))
        for x in range(pack[3]):
            pack.append(self.bus.transfer(0x4B))
        cksum = self.bus.transfer(0x4B) << 8
        cksum |= self.bus.transfer(0x4B)
        if cksum != (sum(pack) & 0xFFFF):
            raise Exception("Mobile.transfer: invalid checksum")
        self.bus.transfer(0x80)
        self.bus.transfer(pack[0] ^ 0x80)

        recv_cmd = pack[0] ^ 0x80

        # On error receive
        if recv_cmd == Mobile.MOBILE_COMMAND_ERROR:
            if pack[4] != cmd:
                raise Exception("Mobile.transfer: Unexpected packet: %s"
                                % pack)
            if not error:
                raise MobileCmdError(pack[5])
            return pack[5]

        # TCP close receive
        if (cmd == Mobile.MOBILE_COMMAND_TRANSFER_DATA and
                recv_cmd == Mobile.MOBILE_COMMAND_TRANSFER_DATA_END):
            return None

        if recv_cmd != cmd:
            raise Exception("Mobile.transfer: Unexpected packet: %s" % pack)
        return bytes(pack[4:])

    def transfer_noreply(self, cmd, *data, **kwargs):
        res = self.transfer(cmd, *data, **kwargs)
        if res == b"":
            return True
        if isinstance(res, int):
            return res
        raise Exception("Mobile.transfer_noreply: Unexpected data: %s" % res)

    def cmd_begin_session(self):
        data = b"NINTENDO"
        res = self.transfer(Mobile.MOBILE_COMMAND_BEGIN_SESSION, *data)
        if res != data:
            raise Exception(
                    "Mobile.cmd_begin_session: Unexpected handshake: %s"
                    % res)

    def cmd_end_session(self):
        return self.transfer_noreply(Mobile.MOBILE_COMMAND_END_SESSION)

    def cmd_dial_telephone(self, number, prot=0):
        return self.transfer_noreply(Mobile.MOBILE_COMMAND_DIAL_TELEPHONE,
                                     prot, *number.encode())

    def cmd_hang_up_telephone(self):
        return self.transfer_noreply(Mobile.MOBILE_COMMAND_HANG_UP_TELEPHONE)

    def cmd_wait_for_telephone_call(self, error=False):
        res = self.transfer_noreply(
                Mobile.MOBILE_COMMAND_WAIT_FOR_TELEPHONE_CALL,
                error=error)
        if res is True:
            return True
        if not error:
            raise MobileCmdError(res)
        return res

    def cmd_transfer_data(self, conn=None, data=b""):
        if isinstance(data, str):
            data = data.encode()
        if conn is None:
            conn = 0xFF

        res = self.transfer(Mobile.MOBILE_COMMAND_TRANSFER_DATA, conn, *data)
        if res is None:
            return None
        if len(res) < 1:
            raise Exception("Mobile.cmd_transfer_data: Invalid packet")
        if res[0] != conn:
            raise Exception("Mobile.cmd_transfer_data: "
                            + "Unexpected connection ID: %d != %d"
                            % (res[0], conn))
        return res[1:]

    def cmd_telephone_status(self):
        res = self.transfer(Mobile.MOBILE_COMMAND_TELEPHONE_STATUS)
        if len(res) != 3:
            raise Exception("Mobile.cmd_telephone_status: Unexpected data: %s"
                            % res)
        return {
            "state": res[0],
            "service": res[1],
            "flags": res[2]
        }

    def cmd_isp_login(self, s_id="nozomi", s_pass="wahaha1",
                      dns1=(0, 0, 0, 0), dns2=(0, 0, 0, 0)):
        res = self.transfer(
                Mobile.MOBILE_COMMAND_ISP_LOGIN,
                len(s_id), *s_id.encode(),
                len(s_pass), *s_pass.encode(),
                *dns1, *dns2)
        if not res:
            return None
        return {
            "ip": (res[0], res[1], res[2], res[3]),
            "dns1": (res[4], res[5], res[6], res[7]),
            "dns2": (res[8], res[9], res[10], res[11]),
        }

    def cmd_isp_logout(self):
        return self.transfer_noreply(Mobile.MOBILE_COMMAND_ISP_LOGOUT)

    def cmd_open_tcp_connection(self, ip=(0, 0, 0, 0), port=0):
        res = self.transfer(Mobile.MOBILE_COMMAND_OPEN_TCP_CONNECTION,
                            *ip, port >> 8, port)
        if not res:
            return None
        return res[0]

    def cmd_close_tcp_connection(self, conn):
        if conn is None:
            conn = 0xFF
        return self.transfer_noreply(
                Mobile.MOBILE_COMMAND_CLOSE_TCP_CONNECTION, conn)

    def cmd_open_udp_connection(self, ip=(0, 0, 0, 0), port=0):
        res = self.transfer(Mobile.MOBILE_COMMAND_OPEN_UDP_CONNECTION,
                            *ip, port >> 8, port)
        if not res:
            return None
        return res[0]

    def cmd_close_udp_connection(self, conn):
        return self.transfer_noreply(
                Mobile.MOBILE_COMMAND_CLOSE_UDP_CONNECTION, conn)

    def cmd_dns_query(self, addr):
        res = self.transfer(Mobile.MOBILE_COMMAND_DNS_QUERY, *addr.encode())
        if not res:
            return None
        return tuple(res)


class MobileProcess:
    def __init__(self, *args, port=8765):
        self.port = port
        self.exe = ["./mobile", "--config", "config_test.bin",
                    *args, "127.0.0.1", str(port)]
        self.bgb = None
        self.sub = None
        self.mob = None

    def __enter__(self):
        if not self.mob:
            self.run()
        return self.mob

    def __exit__(self, et, ev, tr):
        self.close()

    def run(self):
        self.bgb = BGBMaster(port=self.port)
        if os.getenv("TEST_CFG_NOPIPE"):
            self.sub = subprocess.Popen(self.exe)
        else:
            self.sub = subprocess.Popen(self.exe, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE)
        self.bgb.accept()
        self.mob = Mobile(self.bgb)

    def communicate(self):
        out, err = b"", b""
        if self.sub:
            out, err = self.sub.communicate(timeout=10)
            self.sub = None
        return out, err

    def kill(self):
        self.mob = None
        if self.bgb:
            self.bgb.close()
            self.bgb = None
        if self.sub:
            self.sub.kill()
        return self.communicate()

    def close(self):
        self.mob = None
        if self.bgb:
            self.bgb.close()
            self.bgb = None
        return self.communicate()


class SimpleTCPServer:
    def __init__(self, host="127.0.0.1", port=8766):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((host, port))
        sock.listen(1)
        self.sock = sock
        self.conn = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None
        if self.conn:
            self.conn.close()
            self.conn = None

    def accept(self):
        conn, addr = self.sock.accept()
        self.sock.close()
        self.sock = None
        self.conn = conn

    def recv(self, *args, **kwargs):
        return self.conn.recv(*args, **kwargs)

    def send(self, *args, **kwargs):
        self.conn.send(*args, **kwargs)


class SimpleDNSServer:
    def __init__(self, host="127.0.0.1", port=5353):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((host, port))
        self.sock = sock
        self.proc = None

    def __enter__(self):
        self.run()

    def __exit__(self, *args):
        self.close()

    def close(self):
        if self.proc:
            self.proc.terminate()
            self.proc.join()
            self.proc.close()
            self.proc = None
        if self.sock:
            self.sock.close()
            self.sock = None

    def run(self):
        def _run():
            while True:
                self.handle()
        self.proc = multiprocessing.Process(target=_run)
        self.proc.start()

    @staticmethod
    def query(name, qtype):
        ip = [127, 0, 0, 1]
        if qtype == 1:  # A (ipv4)
            if name == "example.com":
                ip = [93, 184, 216, 34]
        return bytes(ip)

    @staticmethod
    def make_name(string):
        res = bytearray()
        for x in string.split("."):
            res.append(len(x))
            res += x.encode()
        res.append(0)
        return bytes(res)

    @staticmethod
    def read_name(data, offs):
        res = ""
        while True:
            siz = data[offs]
            offs += 1
            if not siz:
                break
            if res:
                res += "."
            res += data[offs:offs+siz].decode()
            offs += siz
        return res, offs

    def handle(self):
        mesg, addr = self.sock.recvfrom(512)

        (pid, flags, qdcount, ancount, nscount, arcount
         ) = struct.unpack_from("!HHHHHH", mesg, 0)
        if flags != 0x0100 or qdcount != 1:
            return

        # Read first query section
        qname, offs = self.read_name(mesg, 12)
        qtype, qclass = struct.unpack_from("!HH", mesg, offs)
        if qclass != 1 or (qtype != 1 and qtype != 28):
            return
        resname = self.make_name(qname)
        resdata = self.query(qname, qtype)

        # Encode result
        res = bytearray()
        res += struct.pack("!HHHHHH", pid, 0x8180, 1, 1, 0, 0)
        res += resname
        res += struct.pack("!HH", qtype, qclass)
        res += b'\xc0\x0c'
        res += struct.pack("!HHIH", qtype, qclass, 0, len(resdata))
        res += resdata
        self.sock.sendto(res, addr)


class SimpleRelayServer:
    def __init__(self, host="127.0.0.1", port=31227):
        sock = None
        if not os.getenv("TEST_CFG_REALRELAY"):
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind((host, port))
            sock.listen()
        self.sock = sock
        self.proc = None

        self.wait = {}
        self.call = {}
        self.index = 0

    def __enter__(self):
        if not os.getenv("TEST_CFG_REALRELAY"):
            self.run()

    def __exit__(self, *args):
        self.close()

    def close(self):
        if self.proc:
            self.proc.terminate()
            self.proc.join()
            self.proc.close()
            self.proc = None
        if self.sock:
            self.sock.close()
            self.sock = None

    def run(self):
        def _run():
            while True:
                self.accept()
        self.proc = multiprocessing.Process(target=_run)
        self.proc.start()

    def accept(self):
        _conn, _addr = self.sock.accept()

        def _run(conn):
            try:
                self.handle(conn)
            except ConnectionResetError:
                pass
            finally:
                conn.close()
        thread = threading.Thread(target=_run, args=(_conn,))
        thread.start()

    def relay(self, conn, pair):
        while True:
            data = conn.recv(1024)
            if not data:
                return
            pair.send(data)

    def handle_call(self, conn, index):
        try:
            number = int(conn.recv(conn.recv(1)[0]).decode())
        except ValueError:
            conn.send(b'\x00\x00\x01')
            return False
        pair = self.wait.get(number)
        if pair is None:
            conn.send(b'\x00\x00\x03')
            return False
        self.call[number] = (index, conn)
        conn.send(b'\x00\x00\x00')
        self.relay(conn, pair)
        return True

    def handle_wait(self, conn, index):
        self.wait[index] = conn
        try:
            poller = select.poll()
            poller.register(conn, select.POLLIN | select.POLLPRI)
            while not self.call.get(index):
                if poller.poll(100):
                    return False
        finally:
            del self.wait[index]

        caller = self.call[index]
        del self.call[index]
        num = ("%07d" % caller[0]).encode()
        pair = caller[1]
        conn.send(b'\x00\x01\x00' + bytes([len(num)]) + num)
        self.relay(conn, pair)
        return True

    def handle_get_number(self, conn, index):
        num = ("%07d" % index).encode()
        conn.send(b'\x00\x02' + bytes([len(num)]) + num)

    def handle(self, conn):
        magic = b'\x00MOBILE'
        handshake = conn.recv(0x18)

        assert handshake.startswith(magic)
        if handshake[7] == 0:
            self.index += 1
            index = self.index
            token = bytes([index]) * 16
            conn.send(magic + b'\x01' + token)
        else:
            index = handshake[8]
            conn.send(magic + b'\x00')

        while True:
            data = conn.recv(2)
            if not data:
                break
            assert data[0] == magic[0]

            if data[1] == 0:
                if self.handle_call(conn, index):
                    break
            if data[1] == 1:
                if self.handle_wait(conn, index):
                    break
            if data[1] == 2:
                self.handle_get_number(conn, index)


def mobile_process_test(*args, **kwargs):
    def _deco(func):
        def deco(self):
            m = MobileProcess(*args, **kwargs)
            try:
                m.run()
                func(self, m.mob)
            finally:
                out, err = m.close()
                if out and err:
                    print(out.decode(), file=sys.stdout)
                    print(err.decode(), file=sys.stderr)
        return deco
    return _deco


class Tests(unittest.TestCase):
    @mobile_process_test("--device", "9")
    def test_simple(self, m):
        m.cmd_begin_session()
        status = m.cmd_telephone_status()
        self.assertEqual(status["state"], 0)
        self.assertEqual(status["service"], 0x48)
        self.assertEqual(status["flags"], 0)
        m.cmd_end_session()

    @mobile_process_test()
    def test_session_double_init(self, m):
        m.cmd_begin_session()
        with self.assertRaises(MobileCmdError) as e:
            m.cmd_begin_session()
        self.assertEqual(e.exception.code, 1)

    @mobile_process_test()
    def test_session_timeout(self, m):
        m.cmd_begin_session()

        # Trigger automatic session end
        m.bus.add_time(5)
        time.sleep(0.1)

        # Try to re-initialize the session
        m.cmd_begin_session()
        m.cmd_end_session()

    @mobile_process_test()
    def test_phone_fluke(self, m):
        m.cmd_begin_session()
        with self.assertRaises(MobileCmdError) as e:
            m.cmd_dial_telephone("benis :DD")
        self.assertEqual(e.exception.code, 3)
        m.cmd_end_session()

    @mobile_process_test("--p2p_port", "1028")
    def test_phone_server(self, m):
        m.cmd_begin_session()

        data = b"Hello World!"

        # Create server
        self.assertEqual(m.cmd_wait_for_telephone_call(error=True), 0)

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as t:
            # Start a connection to it
            t.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            t.setblocking(False)
            self.assertNotEqual(t.connect_ex(("127.0.0.1", 1028)), 0)
            t.setblocking(True)

            # Accept connection
            self.assertEqual(m.cmd_wait_for_telephone_call(), True)

            # Finish connection
            t.connect(("127.0.0.1", 1028))

            # Send the data
            self.assertEqual(m.cmd_transfer_data(0xFF, data), b"")

            # Echo the data
            d = t.recv(1024)
            self.assertEqual(d, data)
            t.send(d)

        # Receive the data
        self.assertEqual(m.cmd_transfer_data(0xFF), data)

        # Connection closed
        with self.assertRaises(MobileCmdError) as e:
            m.cmd_transfer_data(0xFF)
        self.assertEqual(e.exception.code, 0)

        m.cmd_hang_up_telephone()
        m.cmd_end_session()

    @mobile_process_test("--p2p_port", "1028")
    def test_phone_client(self, m):
        m.cmd_begin_session()

        data = b"Hello World!"

        with SimpleTCPServer("127.0.0.1", 1028) as t:
            # Start a connection to the server and send data
            m.cmd_dial_telephone("127000000001")
            self.assertEqual(m.cmd_transfer_data(0, data), b"")

            # Accept the connection and echo the data
            t.accept()
            d = t.recv(1024)
            self.assertEqual(d, data)
            t.send(d)

        # Receive the data
        self.assertEqual(m.cmd_transfer_data(0xFF), data)

        # Connection closed
        with self.assertRaises(MobileCmdError) as e:
            m.cmd_transfer_data(0xFF)
        self.assertEqual(e.exception.code, 0)

        m.cmd_hang_up_telephone()
        m.cmd_end_session()

    @mobile_process_test()
    def test_tcp_client(self, m):
        for x in range(2):
            # Log in to ISP
            m.cmd_begin_session()
            m.cmd_dial_telephone("0755311973")
            m.cmd_isp_login()

            data = b"Hello World!"

            c = []

            for x in range(2):
                with SimpleTCPServer("127.0.0.1", 8767 + x) as t:
                    # Connect to the server
                    cc = m.cmd_open_tcp_connection((127, 0, 0, 1), 8767 + x)
                    t.accept()

                    # Transfer data, close server
                    self.assertEqual(m.cmd_transfer_data(cc, data), b"")
                    d = t.recv(1024)
                    self.assertEqual(d, data)
                    t.send(d)
                    c.append(cc)

            # Test data receive after server close
            self.assertEqual(m.cmd_transfer_data(c[0]), data)
            self.assertEqual(m.cmd_transfer_data(c[1]), data)

            # Test server close
            self.assertIsNone(m.cmd_transfer_data(c[0]))
            self.assertIsNone(m.cmd_transfer_data(c[1]))

            # Test auto cleanup by ending session without closing connections
            m.cmd_end_session()

    @mobile_process_test("--dns2", "127.0.0.1", "--dns_port", "5353")
    def test_dns_query(self, m):
        m.cmd_begin_session()
        m.cmd_dial_telephone("0755311973")
        conn = m.cmd_isp_login()
        self.assertEqual(conn["ip"], (127, 0, 0, 1))
        self.assertEqual(conn["dns1"], (0, 0, 0, 0))
        self.assertEqual(conn["dns2"], (127, 0, 0, 1))

        with SimpleDNSServer():
            self.assertEqual(m.cmd_dns_query("example.com"),
                             (93, 184, 216, 34))
            self.assertEqual(m.cmd_dns_query("localhost"),
                             (127, 0, 0, 1))
        self.assertEqual(m.cmd_dns_query("003.4.089.123"), (3, 4, 89, 123))
        self.assertEqual(m.cmd_dns_query("000000..."), (255, 255, 255, 255))
        self.assertEqual(m.cmd_dns_query("benis :DDD"), (255, 255, 255, 255))

        m.cmd_isp_logout()
        m.cmd_hang_up_telephone()
        m.cmd_end_session()

    @unittest.skipIf(os.getenv("TEST_CFG_REALRELAY"), "Needs fake relay")
    @mobile_process_test("--relay", "127.0.0.1")
    def test_relay_fluke(self, m):
        m.cmd_begin_session()
        with SimpleRelayServer():
            with self.assertRaises(MobileCmdError) as e:
                m.cmd_dial_telephone("bagina :DD")
            self.assertEqual(e.exception.code, 3)
        with self.assertRaises(MobileCmdError) as e:
            m.cmd_dial_telephone("bagina :DD")
        self.assertEqual(e.exception.code, 3)
        m.cmd_end_session()

    @unittest.skipIf(os.getenv("TEST_CFG_REALRELAY"), "Needs fake relay")
    @mobile_process_test("--relay", "127.0.0.1")
    def test_relay_reset(self, m):
        # Test whether the relay state is reset properly across resets
        for x in range(3):
            m.cmd_begin_session()
            with self.assertRaises(MobileCmdError) as e:
                m.cmd_dial_telephone("bagina :DDD")
            self.assertEqual(e.exception.code, 3)
            m.bus.add_time(5)
            time.sleep(0.1)

    @mobile_process_test("--relay", "127.0.0.1", "--relay-token", "01" * 16)
    def test_relay_connection(self, m):
        p2 = MobileProcess("--relay", "127.0.0.1",
                           "--config", "config_test_p2.bin",
                           "--relay-token", "02" * 16,
                           port=8766)
        p2.run()
        m2 = p2.mob

        try:
            with SimpleRelayServer():
                # Start connection with opposite mode to test switching
                m2.cmd_begin_session()
                with self.assertRaises(MobileCmdError) as e:
                    m2.cmd_dial_telephone("0000001")
                self.assertEqual(e.exception.code, 0)
                self.assertEqual(m2.cmd_wait_for_telephone_call(error=True), 0)

                # Start listening
                m.cmd_begin_session()
                self.assertEqual(m.cmd_wait_for_telephone_call(error=True), 0)

                # Connect
                m2.cmd_dial_telephone("0000001")
                for x in range(10):
                    if m.cmd_wait_for_telephone_call(error=True) is True:
                        break
                    time.sleep(0.1)

                # Ping poong
                data = b"hello"
                self.assertEqual(m.cmd_transfer_data(0xFF, data), b"")
                self.assertEqual(m2.cmd_transfer_data(0xFF, data), data)
                self.assertEqual(m.cmd_transfer_data(0xFF), data)
            m.cmd_end_session()
            m2.cmd_end_session()
        finally:
            p2.close()


if __name__ == "__main__":
    unittest.main(buffer=not os.getenv("TEST_CFG_NOPIPE"), verbosity=2)
