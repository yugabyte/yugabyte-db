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
from typing import NamedTuple, List, Union
from uuid import UUID
from yb.txndump.model import DocHybridTime, HybridTime, SubDocKey, Tombstone
from yb.txndump.parser import AnalyzerBase, DumpProcessor, TransactionBase, Update

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


def tombstone_to_zero(value: Union[int, Tombstone]) -> int:
    if isinstance(value, Tombstone):
        return 0
    return value


class BankAccountsAnalyzer(AnalyzerBase[int, int]):
    def __init__(self):
        super().__init__()
        self.log = []

    def extract_key(self, tablet: str, key: SubDocKey):
        if key.sub_keys and key.sub_keys[0] == kValueColumn:
            return key.hash_components[0] if key.hash_components else key.range_components[0]
        if not key.sub_keys:
            if len(key.hash_components) == 1:
                return key.hash_components[0]
            if len(key.range_components) == 1:
                return key.range_components[0]
        return None

    def create_transaction(self, txn_id: UUID):
        return BankAccountTransaction(txn_id)

    def analyze(self):
        self.check_status_logs()

        for key in self.rows:
            self.analyze_key(key)

        for txn in self.txns.values():
            self.check_transaction(txn)

        if not self.report_errors():
            for line in sorted(self.log):
                print(line)

    def initial_value(self, key: int):
        return 100 if key == 0 else 0

    def analyze_update(
            self, key: int, update: Update[Union[int, Tombstone]],
            old_balance_or_tombstone: Union[int, Tombstone]) -> Union[int, Tombstone]:
        if update.value is None:
            return old_balance_or_tombstone
        new_balance = tombstone_to_zero(update.value)
        old_balance = tombstone_to_zero(old_balance_or_tombstone)

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
                err_fmt = "Delta mismatch update: {}, key: {}, delta: {} / {} {}"
                self.error(hybrid_time, txn.id,
                           err_fmt.format(txn, key, delta, old_balance_or_tombstone, update.value))

            if duplicate and \
                    update.value is not Tombstone.kTombstone and \
                    old_balance_or_tombstone is not Tombstone.kTombstone:
                err_fmt = "Duplicate update: {}, key: {}, balance: {} / {} {}"
                self.error(hybrid_time, txn.id,
                           err_fmt.format(txn, key, new_balance, old_balance_or_tombstone,
                                          update.value))

            if txn.key1 is not None and txn.key2 is not None:
                self.log.append((hybrid_time, 'w', txn))
        return update.value

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
