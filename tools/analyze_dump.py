"""Parse a Windows minidump: exception info + faulting module mapping.

Usage: python tools/analyze_dump.py <dump.dmp> [--all-modules]
"""
import struct
import sys


def main():
    path = sys.argv[1]
    show_all = "--all-modules" in sys.argv
    with open(path, "rb") as f:
        data = f.read()

    if data[:4] != b"MDMP":
        print("not a minidump")
        return 1

    n_streams = struct.unpack_from("<I", data, 8)[0]
    dir_rva = struct.unpack_from("<I", data, 12)[0]

    streams = {}
    for i in range(n_streams):
        t, sz, rva = struct.unpack_from("<III", data, dir_rva + i * 12)
        streams.setdefault(t, []).append((sz, rva))

    # ModuleListStream = 4; MINIDUMP_MODULE is 108 bytes
    mods = []
    if 4 in streams:
        _, rva = streams[4][0]
        n = struct.unpack_from("<I", data, rva)[0]
        off = rva + 4
        for _ in range(n):
            base, size, _cksum, _ts, name_rva = struct.unpack_from("<QIIII", data, off)
            mlen = struct.unpack_from("<I", data, name_rva)[0]
            name = data[name_rva + 4:name_rva + 4 + mlen].decode("utf-16-le", "replace")
            mods.append((base, size, name))
            off += 108

    def locate(addr):
        for base, size, name in mods:
            if base <= addr < base + size:
                return "%s+0x%x" % (name.split("\\")[-1], addr - base)
        return "unknown-address"

    if show_all:
        for base, size, name in mods:
            print("  %016x %8x  %s" % (base, size, name))

    print("modules: %d" % len(mods))

    # ExceptionStream = 6
    if 6 not in streams:
        print("no exception stream")
        return 1
    _, rva = streams[6][0]
    thread_id = struct.unpack_from("<I", data, rva)[0]
    code, flags = struct.unpack_from("<II", data, rva + 8)
    ex_addr = struct.unpack_from("<Q", data, rva + 8 + 16)[0]
    n_params = struct.unpack_from("<I", data, rva + 8 + 24)[0]
    info = struct.unpack_from("<2Q", data, rva + 8 + 32)
    ctx_size, ctx_rva = struct.unpack_from("<II", data, rva + 8 + 152)

    print("thread id: %d" % thread_id)
    print("exception code: 0x%08X" % (code & 0xFFFFFFFF))
    if code == 0xC0000005:
        kind = {0: "READ", 1: "WRITE", 8: "DEP/EXEC"}.get(info[0], str(info[0]))
        print("access violation: %s at 0x%x (%s)" % (kind, info[1], locate(info[1])))
    print("exception address: 0x%x -> %s" % (ex_addr, locate(ex_addr)))

    # x64 CONTEXT: rip +0xF8, rsp +0x98
    rip = struct.unpack_from("<Q", data, ctx_rva + 0xF8)[0]
    rsp = struct.unpack_from("<Q", data, ctx_rva + 0x98)[0]
    print("rip: 0x%x -> %s" % (rip, locate(rip)))
    print("rsp: 0x%x" % rsp)
    return 0


if __name__ == "__main__":
    sys.exit(main())
