#!/usr/bin/env python3

"""
Base binary input/output.
"""


class BinaryIO:
    def __init__(self, input):
        self._input = input

    def read_int(self, size: int, signed: bool, byteorder: str=None) -> int:
        bin_data = self._input.read(size)
        if len(bin_data) == 0:
            return None
        return int.from_bytes(bin_data, byteorder if byteorder is not None else "little",
                              signed=signed)

    def read_int8(self) -> int:
        return self.read_int(1, True)

    def read_bool(self) -> int:
        return self.read_int8() != 0

    def read_uint8(self) -> int:
        return self.read_int(1, False)

    def read_int16(self) -> int:
        return self.read_int(2, True)

    def read_uint16(self) -> int:
        return self.read_int(2, False)

    def read_int32(self) -> int:
        return self.read_int(4, True)

    def read_uint32(self) -> int:
        return self.read_int(4, False)

    def read_be_int32(self) -> int:
        return self.read_int(4, True, "big")

    def read_int64(self) -> int:
        return self.read_int(8, True)

    def read_uint64(self) -> int:
        return self.read_int(8, False)

    def read_be_int64(self) -> int:
        return self.read_int(8, True, "big")

    def read_varint(self) -> int:
        shift = 0
        result = 0
        while True:
            i = self.read_uint8()
            result |= (i & 0x7f) << shift
            shift += 7
            if not (i & 0x80):
                break
        return result

    def read_key_varint(self) -> int:
        header: int = self.read_uint8()
        neg: bool = header & 0x80 == 0
        if neg:
            header ^= 0xff
        header &= 0x7f
        length: int = 0
        mask: int = 0x40
        while header & mask != 0:
            length += 1
            header ^= mask
            mask >>= 1
        if mask == 0:
            raise Exception('Long var int not supported')
        bin_data = self._input.read(length)
        if neg:
            bin_data = bytearray(bin_data)
            for i in range(0, length):
                bin_data[i] = bin_data[i] ^ 0xff
        value = (header << (8 * length)) ^ int.from_bytes(bin_data, "big", signed=False)
        return -value if neg else value

    def read_desc_key_varint(self) -> int:
        return -self.read_key_varint()

    def read_varstring(self) -> str:
        length = self.read_varint()
        return self._input.read(length) if length is not None else None

    def read_varbytes(self) -> bytes:
        length = self.read_varint()
        return self._input.read(length) if length is not None else None

    def read(self, size: int = -1) -> bytes:
        return self._input.read(size)

    def read_string(self) -> str:
        return self._input.read(-1).decode('utf-8')
