"""The stock executable (main.dol): its sections, and the word at a guest address."""
import struct


class Dol:
    def __init__(self, data):
        self.data = data
        h = struct.unpack_from(">57I", data, 0)
        self.sections = [(h[18 + i], h[i], h[36 + i]) for i in range(18) if h[36 + i]]

    def holds(self, address):
        return any(a <= address < a + size for a, _, size in self.sections)

    def word(self, address):
        for a, offset, size in self.sections:
            if a <= address < a + size:
                return struct.unpack_from(">I", self.data, offset + address - a)[0]
        raise KeyError(f"{address:08X} is not in the executable")
