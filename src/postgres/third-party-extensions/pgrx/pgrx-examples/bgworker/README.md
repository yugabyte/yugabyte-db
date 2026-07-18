A simple Background Worker that uses SPI (connected to a local database named `postgres`) in a 
transaction.

In order to use this bgworker with pgrx, you'll need to edit the proper `postgresql.conf` file in
"${PGRX_HOME}/data-$PGVER/postgresql.conf" and add this line to the end:

```
shared_preload_libraries = 'bgworker.so'
```

Background workers **must** be initialized in the extension's `_PG_init()` function, and can **only**
be started if loaded through the `shared_preload_libraries` configuration setting.