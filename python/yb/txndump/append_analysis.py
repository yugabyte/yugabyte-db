#!/usr/bin/env python3

import sys

from time import monotonic
from typing import NamedTuple, List
from uuid import UUID
from yb.txndump.model import DocHybridTime, HybridTime, SubDocKey, Tombstone
from yb.txndump.parser import AnalyzerBase, DumpProcessor, TransactionBase, Update

kValueColumns = [2, 3]


class AppendTransaction(TransactionBase):
    def __init__(self, txn_id: UUID):
        super().__init__(txn_id)

    def __repr__(self) -> str:
        return "{ " + self.fields_to_string() + " }"


class AppendAnalyzer(AnalyzerBase[str, str]):
    def __init__(self):
        super().__init__()
        self.log = []

    def create_transaction(self, txn_id: UUID):
        return AppendTransaction(txn_id)

    def extract_key(self, tablet: str, key: SubDocKey):
        if key.sub_keys[0] not in kValueColumns:
            return None
        return "{}_{}_{}".format(tablet, key.sub_keys[0], key.hash_components[0])

    def extract_value(self, value):
        return '' if value == Tombstone.kTombstone else value

    def check_transaction(self, transaction):
        return

    def initial_value(self, key: str):
        return ''

    def analyze_update(self, key: str, update: Update[str], old_value: str) -> str:
        new_value = update.value
        hybrid_time = update.doc_ht.hybrid_time
        if not new_value.startswith(old_value):
            self.error(hybrid_time, update.txn_id,
                       "Bad update for {}: {} => {}".format(key, old_value, new_value))
        return new_value


def main():
    analyzer = AppendAnalyzer()
    processor = DumpProcessor(analyzer)
    processor.process(sys.argv[1])
    processing_time = monotonic() - processor.start_time
    analyzer.analyze()
    print("Processing time: {}".format(processing_time))


if __name__ == '__main__':
    main()
