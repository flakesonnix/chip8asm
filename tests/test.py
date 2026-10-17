#!/usr/bin/env python3
"""No third-party dependencies. Independent decoder plus external tool checks."""
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ASM = ROOT / "chip8asm"
UNIT = ROOT / "tests/unit"


def run(args, ok=True):
    env = os.environ.copy()
    if not ok: env["ASAN_OPTIONS"] = "detect_leaks=0"  # fatal exits reclaim allocations at process exit
    p = subprocess.run(list(map(str, args)), capture_output=True, env=env)
    if ok:
        assert p.returncode == 0, (args, p.stderr.decode(errors="replace"))
    else:
        assert p.returncode > 0, (args, p.returncode, p.stderr)
        assert b"chip8asm:" in p.stderr, p.stderr
        assert b"AddressSanitizer" not in p.stderr and b"runtime error:" not in p.stderr, p.stderr
    return p


def sections(path):
    data = path.read_bytes()
    assert data[:7] == b"\x7fELF\x01\x01\x01"
    typ, machine, version, entry, phoff, shoff = struct.unpack_from("<HHIIII", data, 16)
    assert (typ, machine, version, entry) == (2, 0, 1, 0x200)
    ehsize, phsize, phnum, shsize, shnum, names_index = struct.unpack_from("<6H", data, 40)
    assert (ehsize, phsize, phnum, shsize, shnum) == (52, 32, 1, 40, 6)
    headers = [struct.unpack_from("<10I", data, shoff + i * shsize) for i in range(shnum)]
    assert headers[0] == (0,) * 10
    n = headers[names_index]
    names = data[n[4]:n[4] + n[5]]
    result = {}
    for h in headers[1:]:
        name = names[h[0]:].split(b"\0", 1)[0].decode()
        assert h[4] + h[5] <= shoff
        result[name] = (h, data[h[4]:h[4] + h[5]])
    h, text = result[".text"]
    assert h[3] == 0x200 and h[2] == 6
    ph = struct.unpack_from("<8I", data, phoff)
    assert ph == (1, h[4], 0x200, 0x200, len(text), len(text), 5, 1)
    # Minimal CU and abbreviation attributes are checked independently.
    info = result[".debug_info"][1]
    length, version, abbrev_off, addr_size = struct.unpack_from("<IHIB", info)
    assert length + 4 == len(info) and (version, abbrev_off, addr_size) == (4, 0, 4)
    assert info[11] == 1
    pos = info.index(0, 12) + 1
    pos = info.index(0, pos) + 1
    assert struct.unpack_from("<III", info, pos) == (0, 0x200, len(text))
    assert result[".debug_abbrev"][1] == bytes.fromhex("01 11 00 03 08 25 08 10 17 11 01 12 06 00 00 00")
    return result


def decode(data):
    length, version, header_size = struct.unpack_from("<IHI", data)
    assert length + 4 == len(data) and version == 4
    minimum, maximum, default_stmt, base, line_range, opcode_base = struct.unpack_from("<BBBbBB", data, 10)
    assert (minimum, maximum, default_stmt, base, line_range, opcode_base) == (1, 1, 1, -5, 14, 13)
    assert data[16:28] == bytes([0,1,1,1,1,0,0,0,1,0,0,1])
    pos = 28

    def leb(signed=False):
        nonlocal pos
        value = shift = 0
        while True:
            v = data[pos]; pos += 1
            value |= (v & 127) << shift; shift += 7
            assert shift <= 70
            if v < 128:
                return value - (1 << shift) if signed and v & 64 else value

    def cstring():
        nonlocal pos
        end = data.index(0, pos)
        s = os.fsdecode(data[pos:end]); pos = end + 1
        return s

    dirs = []
    while data[pos]: dirs.append(cstring())
    pos += 1
    files = []
    while data[pos]:
        name = cstring(); directory, mtime, size = leb(), leb(), leb()
        assert directory == mtime == size == 0
        files.append(name)
    pos += 1
    assert dirs == [] and pos == 10 + header_size
    rows = []
    address, line, file, column, stmt = 0, 1, 1, 0, bool(default_stmt)
    active = False
    while pos < len(data):
        op = data[pos]; pos += 1
        if op == 0:
            n = leb(); end = pos + n
            ext = data[pos]; pos += 1
            if ext == 2:
                assert n == 5
                address = struct.unpack_from("<I", data, pos)[0]; pos += 4
                active = True
            elif ext == 1:
                assert n == 1
                rows.append((address, file, line, column, stmt, True))
                address, line, file, column, stmt = 0, 1, 1, 0, bool(default_stmt)
                active = False
            else: raise AssertionError(ext)
            assert pos == end
        elif op == 1: rows.append((address, file, line, column, stmt, False))
        elif op == 2: address += leb() * minimum
        elif op == 3: line += leb(True)
        elif op == 4: file = leb()
        elif op == 5: column = leb()
        elif op == 6: stmt = not stmt
        else: raise AssertionError(op)
    assert not active
    return files, rows


class AssemblyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="chip8 test ")
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def source(self, text, name="program.asm"):
        p = self.dir / name; p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(text.encode() if isinstance(text, str) else text)
        return p

    def build(self, paths):
        if isinstance(paths, Path): paths = [paths]
        rom = self.dir / "out.ch8"
        run([ASM, *paths, "-o", rom])
        plain = rom.read_bytes()
        run([ASM, *paths, "-o", rom, "--debug-info"])
        self.assertEqual(plain, rom.read_bytes())
        elf = Path(str(rom) + ".debug.elf")
        s = sections(elf)
        self.assertEqual(s[".text"][1], plain)
        self.validate(elf)
        return plain, decode(s[".debug_line"][1])

    def validate(self, elf):
        if not shutil.which("readelf"):
            self.skipTest("readelf missing: external ELF/DWARF validation unavailable")
        for option in ["--sections", "--program-headers", "--debug-dump=rawline", "--debug-dump=decodedline", "--debug-dump=info", "--debug-dump=abbrev"]:
            result = run(["readelf", option, elf])
            self.assertEqual(result.stderr, b"")
            self.assertTrue(result.stdout)
        llvm = shutil.which("llvm-dwarfdump")
        if llvm:
            result = run([llvm, "--verify", elf]); self.assertEqual(result.stderr, b"")

    def test_minimal(self):
        rom, (_, rows) = self.build(self.source("CLS"))
        self.assertEqual(rom, bytes.fromhex("00e0"))
        self.assertEqual(rows, [(0x200,1,1,1,True,False), (0x202,1,1,1,True,True)])

    def test_example_golden_rows(self):
        text = (ROOT / "examples/program.asm").read_text()
        rom, (files, rows) = self.build(self.source(text))
        self.assertEqual(rom.hex(), "00e0600a611480141200")
        self.assertEqual(files, [str(self.dir / "program.asm")])
        self.assertEqual(rows, [(0x200+2*i,1,i+2,5,True,False) for i in range(5)] + [(0x20a,1,6,5,True,True)])

    def test_comments_labels_and_forward_reference(self):
        text = "; comment\n\n: start\n  JP end # forward\n\nend: RET ; end\n"
        rom, (_, rows) = self.build(self.source(text))
        self.assertEqual(rom.hex(), "120200ee")
        self.assertEqual([(r[0],r[2],r[3]) for r in rows[:-1]], [(0x200,4,3),(0x202,6,6)])

    def test_data_and_gaps(self):
        rom, (_, rows) = self.build(self.source("CLS\n.org 0x210\n.byte 1, 2, 3\n.word 0x1234\nRET\n.org 0x220\n"))
        self.assertEqual(rom, bytes.fromhex("00e0") + bytes(14) + bytes.fromhex("010203123400ee") + bytes(9))
        self.assertEqual(rows, [(0x200,1,1,1,True,False),(0x202,1,1,1,True,True),
                                (0x210,1,3,1,False,False),(0x213,1,4,1,False,False),
                                (0x215,1,5,1,True,False),(0x217,1,5,1,True,True)])

    def test_multiple_files_negative_line_delta(self):
        a = self.source("\n" * 130 + "JP target\n", "one/a.asm")
        b = self.source("target: RET\n", "two/a.asm")
        rom, (files, rows) = self.build([a,b])
        self.assertEqual(rom.hex(), "120200ee")
        self.assertEqual(files, [str(a),str(b)])
        self.assertEqual([(r[0],r[1],r[2]) for r in rows[:-1]], [(0x200,1,131),(0x202,2,1)])

    def test_empty_and_label_only(self):
        for text in ["", "\n; comment\n: main\n"]:
            rom, (_, rows) = self.build(self.source(text))
            self.assertEqual(rom, b""); self.assertEqual(len(rows),1)
            self.assertEqual(rows[0][0],0x200); self.assertTrue(rows[0][-1])

    def test_address_boundary(self):
        rom, (_, rows) = self.build(self.source(".org 0xffe\nCLS\n"))
        self.assertEqual(len(rom),3584); self.assertEqual(rom[-2:],b"\x00\xe0")
        self.assertEqual([r[0] for r in rows],[0xffe,0x1000])
        rom, (_, rows) = self.build(self.source(".org 0xfff\n.byte 255\n"))
        self.assertEqual(rom[-1],255); self.assertEqual(rows[-1][0],0x1000)

    def test_large_source_line(self):
        _, (_, rows) = self.build(self.source("\n" * 70000 + "CLS\n"))
        self.assertEqual(rows[0][2],70001)
        raw = run([UNIT,"max-line"]).stdout
        self.assertEqual(decode(raw)[1][0][2], 0xffffffff)

    def test_all_classic_instruction_encodings(self):
        pairs = [("SYS 0x123","0123"),("CLS","00e0"),("RET","00ee"),("JP 0x345","1345"),
                 ("CALL 0x345","2345"),("SE V1, 42","312a"),("SNE V1, 42","412a"),
                 ("SE V1, V2","5120"),("LD V1, 42","612a"),("ADD V1,42","712a"),
                 ("LD V1,V2","8120"),("OR V1,V2","8121"),("AND V1,V2","8122"),
                 ("XOR V1,V2","8123"),("ADD V1,V2","8124"),("SUB V1,V2","8125"),
                 ("SHR V1,V2","8126"),("SUBN V1,V2","8127"),("SHL V1,V2","812e"),
                 ("SNE V1,V2","9120"),("LD I,0x345","a345"),("JP V0,0x345","b345"),
                 ("RND V1,42","c12a"),("DRW V1,V2,15","d12f"),("SKP V1","e19e"),
                 ("SKNP V1","e1a1"),("LD V1,DT","f107"),("LD V1,K","f10a"),
                 ("LD DT,V1","f115"),("LD ST,V1","f118"),("ADD I,V1","f11e"),
                 ("LD F,V1","f129"),("LD B,V1","f133"),("LD [I],V1","f155"),
                 ("LD V1,[I]","f165"),("SHR V1","8106"),("SHL V1","810e")]
        rom, _ = self.build(self.source("\n".join(x[0] for x in pairs)))
        self.assertEqual(rom.hex(), "".join(x[1] for x in pairs))

    def test_errors_leave_existing_output_untouched(self):
        invalid = ["WAT", "LD V0,256", "JP nowhere", "JP 0x1000", "DRW V0,V1,16", "LD VG,1",
                   ": x\n: x", ": 1bad", ".org 0x1ff", ".org 0x1001", ".org -1",
                   "CLS\n.org 0x200", ".org 0xfff\nCLS", ".include foo", ".byte", ".word 65536",
                   ".byte -1", ".byte 0x", "LD V0,1junk", "CLS extra", "LD V0", ",,,",
                   ".byte " + ",".join(["1"] * 257), "x" * 65536, "CLS\0RET"]
        out = self.dir / "out.ch8"; out.write_bytes(b"preserve")
        for text in invalid:
            with self.subTest(text=text[:40]):
                run([ASM,self.source(text),"-o",out,"--debug-info"],ok=False)
                self.assertEqual(out.read_bytes(),b"preserve")
                self.assertFalse(Path(str(out)+".debug.elf").exists())

    def test_invalid_source_ranges(self):
        for mode in ["line-zero","file-zero","file-high","address-low","address-high","range-overflow","empty-range","rom-bound","empty-path","overlap","null-ranges"]:
            with self.subTest(mode=mode): run([UNIT,mode],ok=False)

    def test_cli_and_output_errors(self):
        src = self.source("CLS\n"); out = self.dir / "out.ch8"
        for args in [[], [src], [src,"-o"], [src,"--wat"], [src,"-o",out,"-o",out],
                     [self.dir/"missing","-o",out], [src,"-o",self.dir/"missing/out"],
                     [src,"-o",src], [src,"-o",out,"--debug-output",out],
                     [src,"-o",self.dir], [src,"-o",out,"--debug-output",self.dir]]:
            p = subprocess.run([str(ASM),*map(str,args)],capture_output=True,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
            self.assertGreater(p.returncode,0)
        self.assertEqual(src.read_text(),"CLS\n")
        alias = self.dir / "alias"; os.link(src,alias)
        run([ASM,src,"-o",alias],ok=False)
        alias.unlink(); alias.symlink_to(src)
        run([ASM,src,"-o",alias],ok=False)
        debug = self.dir / "custom.elf"
        run([ASM,src,"-o",out,"--debug-output",debug]); self.validate(debug)
        run([ASM,"--help"])

    def test_write_failure_is_reported_and_temp_removed(self):
        import resource
        import signal
        src = self.source("CLS\nRET\n")
        out = self.dir / "out.ch8"; out.write_bytes(b"preserve")
        def limit_file():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (1,1))
        p = subprocess.run([str(ASM),str(src),"-o",str(out)],capture_output=True,
                           env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"},preexec_fn=limit_file)
        self.assertEqual(p.returncode,1); self.assertIn(b"write",p.stderr)
        self.assertEqual(out.read_bytes(),b"preserve")
        self.assertEqual(list(self.dir.glob("*.tmp.*")),[])

    def test_nonregular_input_rejected(self):
        fifo = self.dir / "pipe.asm"; os.mkfifo(fifo)
        p = subprocess.run([str(ASM),str(fifo),"-o",str(self.dir/"out.ch8")],capture_output=True,
                           env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"},timeout=5)
        self.assertEqual(p.returncode,1); self.assertIn(b"regular file",p.stderr)

    def test_deterministic_output(self):
        src = self.source("CLS\nRET\n")
        self.build(src); elf = self.dir / "out.ch8.debug.elf"; before = elf.read_bytes()
        self.build(src); self.assertEqual(before,elf.read_bytes())

    def test_deterministic_malformed_input_smoke(self):
        rng = random.Random(8)
        for _ in range(100):
            text = bytes(rng.randrange(1,128) for _ in range(rng.randrange(1,200)))
            src = self.source(text)
            p = subprocess.run([str(ASM),str(src),"-o",str(self.dir/"fuzz.ch8"),"--debug-info"],capture_output=True,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
            self.assertIn(p.returncode,[0,1])
            self.assertNotIn(b"AddressSanitizer",p.stderr)
            self.assertNotIn(b"runtime error:",p.stderr)


if __name__ == "__main__":
    if not shutil.which("llvm-dwarfdump"):
        print("VALIDATOR UNAVAILABLE: llvm-dwarfdump; readelf + independent decoder used",flush=True)
    unittest.main(verbosity=2)
