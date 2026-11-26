import ctypes as c
import os
from pathlib import Path
import unittest


Read = c.CFUNCTYPE(c.c_int32, c.c_void_p, c.c_uint64, c.c_void_p, c.c_uint32)


class Frame(c.Structure):
    _fields_ = [
        ("size", c.c_uint32), ("profile", c.c_int32),
        ("quest", c.c_int32), ("state", c.c_int32), ("generation", c.c_uint64),
        ("image", c.c_uint64), ("context", c.c_void_p), ("read", Read),
    ]


class Row(c.Structure):
    _fields_ = [
        ("monster", c.c_uint64), ("component", c.c_uint64),
        ("species", c.c_int32), ("kind", c.c_uint32),
    ]


Sample = c.CFUNCTYPE(c.c_int32, c.POINTER(Frame), c.POINTER(Row), c.c_uint32)


class Module(c.Structure):
    _fields_ = [
        ("size", c.c_uint32), ("abi", c.c_uint32), ("period_ms", c.c_uint32),
        ("sample", Sample),
    ]


class CrownModuleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        dll = os.environ.get("ICEBORN_CROWN_TEST_DLL", "build/crown_tracker.dll")
        cls.dll = c.CDLL(str(Path(__file__).resolve().parents[1] / dll))
        cls.query = cls.dll.IcebornQueryCrownModule
        cls.query.argtypes = [c.c_uint32]
        cls.query.restype = c.POINTER(Module)

    def setUp(self):
        self.image, self.manager, self.zone = 0x10000000, 0x20000000, 0x30000000
        self.slots = (c.c_uint64 * 128)()
        self.memory = {
            self.image + 0x0500CF40: c.c_uint64(self.manager),
            self.manager + 0x38: self.slots,
            self.image + 0x0500ECA0: c.c_uint64(self.zone),
            self.zone + 0xAED0: c.c_int32(109),
        }

        def read(_, address, dest, size):
            data = self.memory.get(address)
            if data is None or c.sizeof(data) != size:
                return 0
            c.memmove(dest, c.addressof(data), size)
            return 1

        self.frame = Frame(c.sizeof(Frame), 421810, 30001, 2, 1, self.image, None, Read(read))
        self.rows = (Row * 128)()
        self.sample = self.query(0x00010000).contents.sample

    def monster(self, index, species, scale, modifier=1.0):
        component = 0x41000000 + index * 0x10000
        monster = 0x51000000 + index * 0x20000
        status = 0x61000000 + index * 0x10000
        self.slots[index] = component
        self.memory.update({
            component + 0x138: c.c_uint64(monster),
            monster + 0x12280: c.c_int32(species),
            monster + 0x1BC48: c.c_uint64(status),
            status + 0x148: c.c_uint64(monster),
            monster + 0x184: c.c_float(scale),
            monster + 0x7730: c.c_float(modifier),
        })
        return monster

    def results(self):
        count = self.sample(c.byref(self.frame), self.rows, len(self.rows))
        self.assertGreaterEqual(count, 0)
        return [(row.species, row.kind) for row in self.rows[:count]]

    def test_crowns_are_classified_by_species_without_health(self):
        self.monster(0, 0, 1.20)
        self.monster(1, 1, 1.20)
        self.monster(2, 1, 1.08, 1.20)
        self.monster(3, 1, 1.00)
        self.monster(4, 101, 1.30)
        self.slots[5] = self.slots[0]
        self.assertEqual(self.results(), [(0, 2), (1, 3), (1, 1)])

    def test_guiding_lands_and_hub_transition(self):
        self.frame.quest, self.frame.state = -1, 0
        self.monster(0, 1, 0.90)
        self.assertEqual(self.results(), [(1, 1)])
        self.slots[0] = 0
        self.monster(2, 1, 1.23)
        self.assertEqual(self.results(), [(1, 2)])
        self.memory[self.zone + 0xAED0] = c.c_int32(306)
        self.assertEqual(self.results(), [])

    def test_event_sizes_are_classified_outside_standard_quest_range(self):
        self.monster(0, 7, 0.25)
        self.monster(1, 7, 1.80)
        self.monster(2, 7, 1.00)
        self.assertEqual(self.results(), [(7, 1), (7, 2)])

    def test_incomplete_snapshot_and_unknown_profile(self):
        monster = self.monster(0, 1, 1.23)
        del self.memory[monster + 0x7730]
        self.assertEqual(self.sample(c.byref(self.frame), self.rows, 128), -1)
        self.frame.profile = 0
        self.assertEqual(self.sample(c.byref(self.frame), self.rows, 128), -1)
        self.assertFalse(self.query(0))
