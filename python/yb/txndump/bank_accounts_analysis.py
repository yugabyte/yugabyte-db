#!/usr/bin/env python3

"""
Analyze binary dump for bank accounts jepsen test.

To create dump set tserver flag dump_transactions to true and reproduce the issue.
In will generate DUMP.* files to logs dir.
Copy files from all nodes to the same folder and specify this folder as an argument to analysis
script.
"""

import sys

from time import monotonic
from typing import NamedTuple, List
from uuid import UUID
from yb.txndump.model import DocHybridTime, HybridTime, SubDocKey
from yb.txndump.parser import AnalyzerBase, DumpProcessor, TransactionBase

kValueColumn = 1


class BankAccountTransaction(TransactionBase):
    def __init__(self, txn_id: UUID):
        super().__init__(txn_id)
        self.key1 = None
        self.key1_balance = None
        self.key2 = None
        self.key2_balance = None
        self.delta = None

    def __repr__(self) -> str:
        result = "{ " + self.fields_to_string()
        if self.key1 is not None:
            result += " key1: {} key1_balance: {}".format(self.key1, self.key1_balance)
        if self.key2 is not None:
            result += " key2: {} key2_balance: {}".format(self.key2, self.key2_balance)
        if self.delta is not None:
            result += " delta: {}".format(self.delta)
        return result + " }"


class Update(NamedTuple):
    doc_ht: DocHybridTime
    txn_id: UUID
    value: int
    log_ht: HybridTime


class Read(NamedTuple):
    read_time: HybridTime
    value: int
    write_time: DocHybridTime
    txn_id: UUID
    same_transaction: bool


class KeyData(NamedTuple):
    updates: List[Update] = []
    reads: List[Read] = []


class BankAccountsAnalyzer(AnalyzerBase):
    def __init__(self):
        super().__init__()
        self.rows = {}
        self.log = []

    def apply_row(self, txn_id: UUID, key: SubDocKey, value: int, log_ht: HybridTime):
        if key.sub_keys[0] == kValueColumn:
            row = key.hash_components[0]
            self.get_row(row).updates.append(Update(key.doc_ht, txn_id, value, log_ht))

    def read_value(
        self, txn_id: UUID, key, value, read_time: HybridTime, write_time: DocHybridTime,
            same_transaction: bool):
        if key.sub_keys[0] == kValueColumn and (write_time.hybrid_time <= read_time):
            self.get_row(key.hash_components[0]).reads.append(Read(
                read_time, value, write_time, txn_id, same_transaction))

    def get_row(self, key) -> KeyData:
        if key not in self.rows:
            self.rows[key] = KeyData()
        return self.rows[key]

    def get_transaction(self, txn_id: UUID):
        if txn_id not in self.txns:
            self.txns[txn_id] = BankAccountTransaction(txn_id)
        return self.txns[txn_id]

    def check_same_updates(self, key: int, update: Update, same_updates: int):
        if same_updates < 3:
            err_fmt = "Wrong number of same updates for key {}, update {}: {}"
            self.error(
                update.doc_ht.hybrid_time, update.txn_id, err_fmt.format(key, update, same_updates))

    def analyze(self):
        self.check_status_logs()

        for key in self.rows:
            self.analyze_key(key)

        for txn in self.txns.values():
            self.check_transaction(txn)

        if not self.report_errors():
            for line in sorted(self.log):
                print(line)

    def analyze_key(self, key):
        updates = sorted(self.rows[key].updates,
                         key=lambda upd: (upd.doc_ht, upd.txn_id))
        reads = sorted(self.rows[key].reads,
                       key=lambda read: read.read_time)
        read_idx = 0
        old_balance = 100 if key == 0 else 0
        prev_update = None
        same_updates = 3
        for update in updates:
            if prev_update is not None and prev_update == update:
                same_updates += 1
                continue
            else:
                self.check_same_updates(key, prev_update, same_updates)
                same_updates = 1

            new_balance: int = self.analyze_update(key, update, old_balance)

            read_idx = self.analyze_read(
                key, reads, read_idx, update.doc_ht.hybrid_time, old_balance)

            old_balance = new_balance
            prev_update = update
        self.check_same_updates(key, prev_update, same_updates)

    def analyze_update(self, key: int, update: Update, old_balance: int) -> int:
        new_balance = update.value
        hybrid_time = update.doc_ht.hybrid_time
        txn = self.txns[update.txn_id]
        if old_balance is not None:
            delta = new_balance - old_balance
            duplicate = False
            old_delta = txn.delta
            if delta > 0:
                if txn.key2 is None:
                    txn.key2 = key
                    txn.key2_balance = new_balance
                else:
                    duplicate = True
            else:
                if txn.key1 is None:
                    txn.key1 = key
                    txn.key1_balance = new_balance
                else:
                    duplicate = True

            if old_delta is None:
                txn.delta = abs(delta)
            elif old_delta != abs(delta):
                err_fmt = "Delta mismatch update: {}, key: {}, delta: {}"
                self.error(hybrid_time, txn.id, err_fmt.format(txn, key, delta))

            if duplicate:
                err_fmt = "Duplicate update: {}, key: {}, balance: {}"
                self.error(hybrid_time, txn.id, err_fmt.format(txn, key, new_balance))

            if txn.key1 is not None and txn.key2 is not None:
                self.log.append((hybrid_time, 'w', txn))
        return new_balance

    def analyze_read(
            self, key: int, reads: List[Read], read_idx: int, hybrid_time: HybridTime,
            old_balance: int) -> int:
        while read_idx < len(reads) and hybrid_time > reads[read_idx].read_time:
            read_txn = reads[read_idx].txn_id
            if read_txn in self.txns:
                read_balance = reads[read_idx].value
                if old_balance != read_balance:
                    self.error(
                        reads[read_idx].read_time,
                        read_txn,
                        "Bad read key: {}, actual: {}, read: {}".format(
                            key, old_balance, reads[read_idx]))
                self.log.append((reads[read_idx].read_time, 'r', read_txn, key, read_balance))
            read_idx += 1
        return read_idx

    def check_transaction(self, txn: BankAccountTransaction):
        cnt_keys = (1 if txn.key1 is not None else 0) + (1 if txn.key2 is not None else 0)
        if cnt_keys == 0:
            self.error(txn.commit_time, txn.id, "Txn without keys: {}".format(txn))
        elif cnt_keys == 1:
            self.error(txn.commit_time, txn.id, "Not full txn: {}".format(txn))


def main():
    analyzer = BankAccountsAnalyzer()
    processor = DumpProcessor(analyzer)
    processor.process(sys.argv[1])
    processing_time = monotonic() - processor.start_time
    analyzer.analyze()
    print("Processing time: {}".format(processing_time))


if __name__ == '__main__':
    main()
