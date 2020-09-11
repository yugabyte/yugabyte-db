```
set_transaction ::= SET TRANSACTION transaction_mode [ ... ]

transaction_mode ::= read_write_mode | isolation_level

read_write_mode ::= READ ONLY | READ WRITE

isolation_level ::= ISOLATION LEVEL { READ UNCOMMITTED
                                      | READ COMMITTED
                                      | REPEATABLE READ
                                      | SERIALIZABLE }
```