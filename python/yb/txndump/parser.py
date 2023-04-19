#!/usr/bin/env python3

"""
Generic binary transaction dump parser.
"""

import gzip
import os
import sys

from abc import ABC, abstractmethod
from enum import Enum
from functools import total_ordering
from io import BytesIO
from time import monotonic
from typing import Any, Dict, Generic, List, NamedTuple, Optional, Set, TypeVar
from uuid import UUID
from yb.txndump.io import BinaryIO
from yb.txndump.model import DocHybridTime, HybridTime, ReadHybridTime, SubDocKey, \
    TransactionStatus, decode_value, read_slice, read_txn_id


class CommitTimeReason(Enum):
    kLocalBefore = 0
    kNoMetadata = 1
    kLocalAfter = 2
    kRemoteAborted = 3
    kRemoteCommitted = 4
    kRemotePending = 5


class RemoveReason(Enum):
    kApplied = 0
    kLargeApplied = 1
    kProcessCleanup = 2
    kStatusReceived = 3
    kAbortReceived = 4
    kShutdown = 5
    kSetDB = 6


class WriteBatchEntryType(Enum):
    kTypeDeletion = 0x0
    kTypeValue = 0x1
    kTypeMerge = 0x2
    kTypeLogData = 0x3
    kTypeColumnFamilyDeletion = 0x4
    kTypeColumnFamilyValue = 0x5
    kTypeColumnFamilyMerge = 0x6
    kTypeSingleDeletion = 0x7
    kTypeColumnFamilySingleDeletion = 0x8


class TransactionConflictData(NamedTuple):
    id: UUID = UUID(int=0)
    status: TransactionStatus = TransactionStatus.UNKNOWN
    commit_time: HybridTime = HybridTime()
    priority: int = 0
    failed: bool = False


def load_transaction_conflict_data(inp: BinaryIO) -> Optional[TransactionConflictData]:
    txn_id = read_txn_id(inp)
    if txn_id is None:
        return None
    status = TransactionStatus(inp.read_uint32())
    inp.read_uint32()
    commit_time = HybridTime.load(inp)
    priority = inp.read_uint64()
    failed = inp.read_uint64() != 0
    return TransactionConflictData(txn_id, status, commit_time, priority, failed)


class StatusLogEntry(NamedTuple):
    read_time: ReadHybridTime
    by_txn_id: UUID
    commit_time: HybridTime
    reason: CommitTimeReason


class Analyzer:
    @abstractmethod
    def get_transaction(self, txn_id: UUID):
        pass

    @abstractmethod
    def apply_row(self, tablet: str, txn_id: UUID, key: SubDocKey, value, log_ht: HybridTime):
        pass

    @abstractmethod
    def read_value(
            self, tablet: str, txn_id: UUID, key: SubDocKey, value, read_time: HybridTime,
            write_time: DocHybridTime, same_transaction: bool):
        pass


class DumpProcessor:
    def __init__(self, analyzer):
        self.cnt_commands = 0
        self.cnt_bytes = 0
        self.start_time = monotonic()
        self.analyzer = analyzer
        self.txns = {}
        self.commands = {
            1: DumpProcessor.parse_apply_intent,
            2: DumpProcessor.parse_read,
            3: DumpProcessor.parse_commit,
            4: DumpProcessor.parse_status,
            5: DumpProcessor.parse_conflicts,
            6: DumpProcessor.parse_applied,
            7: DumpProcessor.parse_remove,
            8: DumpProcessor.parse_remove_intent,
        }

    def process(self, input_path: str):
        if os.path.isdir(input_path):
            for file in os.listdir(input_path):
                if file.startswith("DUMP."):
                    self.process_file(os.path.join(input_path, file))
        else:
            self.process_file(input_path)

    def process_file(self, fname: str):
        # path, processing_file = os.path.split(fname)
        print("Processing {}".format(fname))
        with gzip.open(fname, 'rb') as raw_input:
            inp = BinaryIO(raw_input)
            while True:
                size = inp.read_int64()
                if size is None:
                    break
                body = inp.read(size)
                self.cnt_bytes += 8 + size
                block = BinaryIO(BytesIO(body))
                cmd = block.read_int8()
                self.commands[cmd](self, block)
                left = block.read()
                if len(left) != 0:
                    raise Exception("Extra data left in block {}: {}".format(cmd, left))
                self.cnt_commands += 1
                if self.cnt_commands % 100000 == 0:
                    print("Parsed {} commands, {} bytes, passed: {}".format(
                        self.cnt_commands, self.cnt_bytes, monotonic() - self.start_time))

    def parse_apply_intent(self, inp: BinaryIO):
        txn_id = read_txn_id(inp)
        key = read_slice(inp)
        commit_ht = HybridTime.load(inp)
        write_id = inp.read_uint32()
        value = inp.read()

    def parse_remove_intent(self, inp: BinaryIO):
        txn_id = read_txn_id(inp)
        key = inp.read()

    def parse_read(self, inp: BinaryIO):
        tablet = inp.read(32).decode('utf-8')
        txn = read_txn_id(inp)
        read_time = ReadHybridTime.load(inp)
        write_time = DocHybridTime.load(inp)
        same_transaction = inp.read_bool()
        key_len = inp.read_uint64()
        key_bytes = inp.read(key_len)
        key = SubDocKey.decode(key_bytes, False)
        value_len = inp.read_uint64()
        value = decode_value(inp.read(value_len))
        self.analyzer.read_value(
            tablet, txn, key, value, read_time.read, write_time, same_transaction)

    def parse_commit(self, inp: BinaryIO):
        txn_id = read_txn_id(inp)
        commit_time = HybridTime.load(inp)
        txn = self.get_transaction(txn_id)
        tablets = inp.read_uint32()
        if txn.commit_time.valid():
            if txn.commit_time != commit_time:
                raise Exception('Wrong commit time {} vs {}'.format(commit_time, txn))
        else:
            txn.commit_time = commit_time
            txn.involved_tablets = tablets

    def parse_status(self, inp: BinaryIO):
        by_txn = read_txn_id(inp)
        read_time = ReadHybridTime.load(inp)
        txn_id = read_txn_id(inp)
        commit_time = HybridTime.load(inp)
        reason = CommitTimeReason(inp.read_uint8())
        status_time = HybridTime.load(inp)
        safe_time = HybridTime.load(inp)
        txn = self.get_transaction(txn_id)
        txn.status_log.append(StatusLogEntry(read_time, by_txn, commit_time, reason))
        txn.add_log(
            read_time.read, "status check by", by_txn, commit_time, reason, status_time, safe_time)
        self.get_transaction(by_txn).add_log(
            read_time.read, "see status", txn_id, commit_time, reason, status_time, safe_time)

    def parse_conflicts(self, inp: BinaryIO):
        txn_id = read_txn_id(inp)
        hybrid_time = HybridTime.load(inp)
        txn = self.get_transaction(txn_id)
        if not hybrid_time.valid():
            txn.aborted = True
        while True:
            txn_data = load_transaction_conflict_data(inp)
            if txn_data is None:
                break
            txn.add_log(hybrid_time, "see conflict", txn_data)
            self.get_transaction(txn_data.id).add_log(
                hybrid_time, "conflict check by", txn_id, txn_data)

    def parse_applied(self, inp: BinaryIO):
        txn_id = read_txn_id(inp)
        hybrid_time = HybridTime.load(inp)
        self.get_transaction(txn_id).add_log(hybrid_time, "applied")

    def parse_remove(self, inp: BinaryIO):
        tablet = inp.read(32).decode('utf-8')
        txn_id = read_txn_id(inp)
        hybrid_time = HybridTime.load(inp)
        reason = RemoveReason(inp.read_uint8())
        self.get_transaction(txn_id).add_log(hybrid_time, "remove", reason)

    def get_transaction(self, txn_id: UUID):
        return self.analyzer.get_transaction(txn_id)


class Error(NamedTuple):
    hybrid_time: HybridTime
    msg: str


class TxnLogEntry(NamedTuple):
    hybrid_time: HybridTime
    op: str
    args: Any


@total_ordering
class TransactionBase:
    def __init__(self, txn_id: UUID):
        self.id = txn_id
        self.involved_tablets = None
        self.commit_time = HybridTime()
        self.status_log: List[StatusLogEntry] = []
        self.aborted = False
        self.log: List[TxnLogEntry] = []

    def add_log(self, hybrid_time: HybridTime, op: str, *args):
        self.log.append(TxnLogEntry(hybrid_time, op, args))

    def __lt__(self, other) -> bool:
        return (self.id, self.commit_time) < (other.id, other.commit_time)

    def report(self):
        print("TXN: {}".format(self))
        for entry in sorted(self.log, key=lambda t: t.hybrid_time):
            print("  log: {}".format(entry))

    def fields_to_string(self):
        return "id: {} commit_time: {}".format(self.id, self.commit_time)


K = TypeVar('K')
T = TypeVar('T')


class Update(Generic[T]):
    def __init__(self, doc_ht: DocHybridTime, txn_id: UUID, value: T, log_ht: HybridTime):
        self.doc_ht = doc_ht
        self.txn_id = txn_id
        self.value = value
        self.log_ht = log_ht

    def __eq__(self, other):
        return self.doc_ht == other.doc_ht and self.txn_id == other.txn_id and \
               self.value == other.value and self.log_ht == other.log_ht


class Read(Generic[T]):
    def __init__(self, read_time: HybridTime, value: T, write_time: DocHybridTime, txn_id: UUID,
                 same_transaction: bool):
        self.read_time = read_time
        self.value = value
        self.write_time = write_time
        self.txn_id = txn_id
        self.same_transaction = same_transaction


class KeyData(Generic[T]):
    def __init__(self):
        self.updates: List[Update[T]] = []
        self.reads: List[Read[T]] = []


class AnalyzerBase(Analyzer, ABC, Generic[K, T]):
    def __init__(self):
        self.txns: Dict[UUID, TransactionBase] = {}
        self.errors: List[Error] = []
        self.reported_transactions: Set[UUID] = set()
        self.rows: Dict[K, KeyData[T]] = {}

    @abstractmethod
    def create_transaction(self, txn_id: UUID):
        pass

    @abstractmethod
    def extract_key(self, tablet: str, key: SubDocKey):
        pass

    @abstractmethod
    def initial_value(self, key: K):
        pass

    @abstractmethod
    def check_transaction(self, transaction):
        pass

    @abstractmethod
    def analyze_update(self, key: K, update: Update[T], old_value: T) -> T:
        pass

    def extract_value(self, value):
        return value

    def get_row(self, key: K) -> KeyData[T]:
        if key not in self.rows:
            self.rows[key] = KeyData[T]()
        return self.rows[key]

    def apply_row(self, tablet: str, txn_id: UUID, key: SubDocKey, value, log_ht: HybridTime):
        row_key = self.extract_key(tablet, key)
        if row_key is None:
            return
        row_value = self.extract_value(value)
        self.get_row(row_key).updates.append(Update(key.doc_ht, txn_id, row_value, log_ht))

    def read_value(
        self, tablet: str, txn_id: UUID, key, value, read_time: HybridTime,
            write_time: DocHybridTime, same_transaction: bool):
        if write_time.hybrid_time > read_time:
            return
        row_key = self.extract_key(tablet, key)
        if row_key is None:
            return
        row_value = self.extract_value(value)
        self.get_row(row_key).reads.append(Read(
            read_time, row_value, write_time, txn_id, same_transaction))

    def get_transaction(self, txn_id: UUID):
        if txn_id not in self.txns:
            self.txns[txn_id] = self.create_transaction(txn_id)
        return self.txns[txn_id]

    def analyze(self):
        self.check_status_logs()

        for key in self.rows:
            self.analyze_key(key)

        for txn in self.txns.values():
            self.check_transaction(txn)

        if not self.report_errors():
            for line in sorted(self.log):
                print(line)

    def check_same_updates(self, key: int, update: Update, same_updates: int):
        if same_updates < 3:
            err_fmt = "Wrong number of same updates for key {}, update {}: {}"
            self.error(
                update.doc_ht.hybrid_time, update.txn_id, err_fmt.format(key, update, same_updates))

    def analyze_key(self, key: K):
        updates = sorted(self.rows[key].updates,
                         key=lambda upd: (upd.doc_ht, upd.txn_id))
        filtered_reads = filter(
            lambda x:
                not x.same_transaction and
                x.txn_id in self.txns and
                self.txns[x.txn_id].commit_time.valid(),
            self.rows[key].reads)
        reads = sorted(filtered_reads, key=lambda read: read.read_time)
        read_idx = 0
        old_value = self.initial_value(key)
        prev_update = None
        same_updates = 3
        for update in updates:
            if prev_update is not None and prev_update == update:
                same_updates += 1
                continue
            else:
                self.check_same_updates(key, prev_update, same_updates)
                same_updates = 1

            new_value: str = self.analyze_update(key, update, old_value)

            read_idx = self.analyze_read(
                key, reads, read_idx, update.doc_ht.hybrid_time, old_value)

            old_value = new_value
            prev_update = update
        self.check_same_updates(key, prev_update, same_updates)

    def analyze_read(
            self, key: int, reads: List[Read[T]], read_idx: int, hybrid_time: HybridTime,
            old_value: str) -> int:
        while read_idx < len(reads) and hybrid_time > reads[read_idx].read_time:
            read = reads[read_idx]
            read_idx += 1
            read_txn = read.txn_id
            read_value = read.value
            if old_value != read_value:
                self.error(
                    read.read_time,
                    read_txn,
                    "Bad read key: {}, actual: '{}', read: {}".format(
                        key, old_value, read))
            self.log.append((read.read_time, 'r', read_txn, key, read_value))
        return read_idx

    def report_errors(self):
        for err in sorted(self.errors, key=lambda error: error.hybrid_time):
            sys.stderr.write(err.msg + "\n")
        return len(self.errors) != 0

    def error(self, error_time: HybridTime, txn_id: UUID, msg: str):
        self.errors.append(Error(error_time, msg))
        self.report_transaction(txn_id)
        if len(self.errors) >= 10:
            self.report_errors()
            sys.stderr.write("Too many errors, exiting\n")
            exit(1)

    def report_transaction(self, txn_id: UUID):
        if txn_id in self.reported_transactions:
            return
        self.txns[txn_id].report()
        self.reported_transactions.add(txn_id)

    def check_status_logs(self):
        for txn in self.txns.values():
            for entry in txn.status_log:
                by_txn_id = entry.by_txn_id
                if by_txn_id in self.txns and self.txns[by_txn_id].aborted:
                    continue
                seen_commit_time = entry.commit_time
                reason = entry.reason
                if txn.commit_time.valid():
                    if not seen_commit_time.is_min():
                        if seen_commit_time != txn.commit_time:
                            self.error(
                                entry.read_time.read, txn.id,
                                "Seen commit time mismatch: {} vs {}".format(
                                    seen_commit_time, txn))
                    elif entry.read_time.read >= txn.commit_time \
                            and reason != CommitTimeReason.kNoMetadata:
                        self.error(
                            entry.read_time.read, txn.id,
                            "Did not see commit of {} at {} by {} reason {}".format(
                                txn, entry.read_time.read, by_txn_id, entry.reason))
                    elif entry.reason == CommitTimeReason.kRemoteAborted:
                        self.error(
                            entry.read_time.read, txn.id,
                            "Committed transaction {} seen as aborted at {}".format(
                                txn, entry.read_time.read))
                elif not seen_commit_time.is_min():
                    self.error(
                        entry.read_time.read, txn.id,
                        "Aborted transaction seen as committed: {} by {} vs {}".format(
                            seen_commit_time, by_txn_id, txn))
