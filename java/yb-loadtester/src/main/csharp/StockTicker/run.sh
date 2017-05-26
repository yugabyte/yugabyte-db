#!/bin/sh
mono code/StockTicker.exe $@ &
PID=$!
wait $PID
