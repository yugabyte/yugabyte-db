#!/usr/bin/env python3

"""
YugaByte DocDB model. I.e. keys, values, time etc.
"""

from datetime import timedelta
from enum import Enum
from functools import total_ordering
from io import BytesIO
from typing import List, NamedTuple, Optional
from uuid import UUID
from yb.txndump.io import BinaryIO


MINUTES_PER_HOUR = 60
SECONDS_PER_MINUTE = 60
SECONDS_PER_HOUR = MINUTES_PER_HOUR * SECONDS_PER_MINUTE


def micros_to_string(total_micros: int) -> str:
    total_time = timedelta(microseconds=total_micros)
    days = total_time.days
    hours = total_time.seconds // SECONDS_PER_HOUR
    minutes = (total_time.seconds % SECONDS_PER_HOUR) // SECONDS_PER_MINUTE
    seconds = total_time.seconds % SECONDS_PER_MINUTE
    micros = total_time.microseconds
    return "days: {} time: {:02d}:{:02d}:{:02d}.{:06d}".format(
        days, hours, minutes, seconds, micros)


@total_ordering
class HybridTime:
    kMax = 0xffffffffffffffff
    kInvalid = kMax - 1
    kMin = 0
    kInitial = 1

    kBitsForLogicalComponent = 12

    def __init__(self, **kwargs):
        if 'micros' in kwargs:
            self.repr = HybridTime.repr_from_micros_and_logical(kwargs['micros'], kwargs['logical'])
        else:
            self.repr = kwargs['repr'] if 'repr' in kwargs else HybridTime.kInvalid

    def __repr__(self):
        return self.to_string(True)

    def to_string(self, with_braces: bool) -> str:
        if self.repr == HybridTime.kInvalid:
            return "<invalid>"
        if self.repr == HybridTime.kMax:
            return "<max>"
        if self.repr == HybridTime.kMin:
            return "<min>"
        if self.repr == HybridTime.kInitial:
            return "<initial>"
        logical = self.logical()

        result = ("{ " if with_braces else "") + micros_to_string(self.micros())
        if logical != 0:
            result += " logical: {}".format(logical)
        if with_braces:
            result += " }"
        return result

    def logical(self) -> int:
        return self.repr & ((1 << HybridTime.kBitsForLogicalComponent) - 1)

    def micros(self) -> int:
        return self.repr >> HybridTime.kBitsForLogicalComponent

    def __lt__(self, other) -> bool:
        return self.repr < other.repr

    def __eq__(self, other) -> bool:
        return self.repr == other.repr

    def valid(self) -> bool:
        return self.repr != HybridTime.kInvalid

    def is_min(self) -> bool:
        return self.repr == HybridTime.kMin

    @staticmethod
    def repr_from_micros_and_logical(micros: int, logical: int) -> int:
        return (micros << HybridTime.kBitsForLogicalComponent) | logical

    @staticmethod
    def load(inp: BinaryIO):
        return HybridTime(repr=inp.read_uint64())


class ReadHybridTime(NamedTuple):
    read: HybridTime
    local_limit: HybridTime
    global_limit: HybridTime
    in_txn_limit: HybridTime
    serial_no: int

    @staticmethod
    def load(inp: BinaryIO):
        read = HybridTime.load(inp)
        local_limit = HybridTime.load(inp)
        global_limit = HybridTime.load(inp)
        in_txn_limit = HybridTime.load(inp)
        serial_no = inp.read_uint64()
        return ReadHybridTime(read, local_limit, global_limit, in_txn_limit, serial_no)


class DocHybridTime(NamedTuple):
    hybrid_time: HybridTime
    write_id: int

    # The 'load' methods parse data structure that was just copied from memory as is.
    @staticmethod
    def load(inp: BinaryIO) -> 'DocHybridTime':
        hybrid_time = HybridTime.load(inp)
        write_id = inp.read_uint32()
        inp.read_uint32()  # alignment
        return DocHybridTime(hybrid_time, write_id)

    # The 'read' methods parse data structure that was serialized to binary representation by docdb.
    @staticmethod
    def read(inp: BinaryIO) -> 'DocHybridTime':
        inp.read_desc_key_varint()  # Generation
        micros = kYugaByteMicrosecondEpoch + inp.read_desc_key_varint()
        logical = inp.read_desc_key_varint()
        write_id = inp.read_desc_key_varint()
        if write_id < 0:
            raise Exception("Negative shifted write_id: {}".format(write_id))
        return DocHybridTime(HybridTime(micros=micros, logical=logical),
                             (write_id >> kNumBitsForHybridTimeSize) - 1)


kYugaByteMicrosecondEpoch: int = 1500000000 * 1000000
kNumBitsForHybridTimeSize: int = 5


def read_txn_id(inp) -> Optional[UUID]:
    bin_data: bytes = inp.read(16)
    if bin_data == b'':
        return None
    return UUID(bytes=bin_data)


def read_slice(inp) -> bytes:
    len = inp.read_uint64()
    return inp.read(len)


class ValueType(Enum):
    kGroupEnd = 33
    kHybridTime = 35
    kNullLow = 36
    kUInt16Hash = 71
    kInt32 = 72
    kInt64 = 73
    kSystemColumnId = 74
    kColumnId = 75
    kString = 83
    kTombstone = 88

    @staticmethod
    def read(inp: BinaryIO) -> 'ValueType':
        code = inp.read_uint8()
        return ValueType(code) if code is not None else None


kInt32SignBitFlipMask = 0x80000000


def decode_key_entry(inp: BinaryIO, value_type: ValueType):
    if value_type == ValueType.kInt32:
        barr = bytearray(inp.read(4))
        barr[0] ^= 0x80
        return int.from_bytes(barr, "big", signed=True)
    if value_type == ValueType.kSystemColumnId or value_type == ValueType.kColumnId:
        return inp.read_key_varint()
    raise Exception('Not supported key value type: {}'.format(value_type))


class SubDocKey(NamedTuple):
    hash_components: List
    range_components: List
    sub_keys: List
    doc_ht: Optional[DocHybridTime]

    @staticmethod
    def decode(key: bytes, has_hybrid_time: bool):
        inp = BinaryIO(BytesIO(key))
        value_type = ValueType.read(inp)
        if value_type != ValueType.kUInt16Hash:
            raise Exception('Missing key hash in {}, {}'.format(key, value_type))
        inp.read_int16()
        hash_components = []
        range_components = []
        for out in (hash_components, range_components):
            while True:
                value_type = ValueType.read(inp)
                if value_type == ValueType.kGroupEnd:
                    break
                out.append(decode_key_entry(inp, value_type))
        sub_keys = []
        while True:
            value_type = ValueType.read(inp)
            if has_hybrid_time:
                if value_type == ValueType.kHybridTime:
                    break
            else:
                if value_type is None:
                    break
            sub_keys.append(decode_key_entry(inp, value_type))
        doc_ht = None
        if has_hybrid_time:
            doc_ht = DocHybridTime.read(inp)
        return SubDocKey(hash_components, range_components, sub_keys, doc_ht)


class Tombstone(Enum):
    kTombstone = 0


def decode_value(value: bytes):
    inp = BinaryIO(BytesIO(value))
    value_type = ValueType.read(inp)
    if value_type == ValueType.kHybridTime:
        DocHybridTime.read(inp)  # Intent doc ht
        value_type = ValueType.read(inp)
    if value_type == ValueType.kNullLow:
        return None
    if value_type == ValueType.kInt32:
        return inp.read_be_int32()
    if value_type == ValueType.kInt64:
        return inp.read_be_int64()
    if value_type == ValueType.kString:
        return inp.read_string()
    if value_type == ValueType.kTombstone:
        return Tombstone.kTombstone
    raise Exception('Not supported value type: {}'.format(value_type))


class TransactionStatus(Enum):
    UNKNOWN = 0
    CREATED = 1
    PENDING = 2
    COMMITTED = 4
    SEALED = 5
    APPLIED_IN_ALL_INVOLVED_TABLETS = 7
    ABORTED = 8
    APPLYING = 20
    APPLIED_IN_ONE_OF_INVOLVED_TABLETS = 21
    IMMEDIATE_CLEANUP = 22
    GRACEFUL_CLEANUP = 23
