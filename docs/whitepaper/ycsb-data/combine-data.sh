#!/bin/bash

prepend_lines() {
  tail --lines=+2 $1 | perl -p -e "s,^,$2,g;"
}

printf "dist\tsys\tworkload\ttime\ttput\n"
for dist in zipfian uniform ; do
  for sys in hbase kudu ; do
    prepend_lines $dist-$sys/load-100M.log.tsv "${dist}\t${sys}\tload\t"
    prepend_lines $dist-$sys/run-workloada.log.tsv "${dist}\t${sys}\ta\t"
    prepend_lines $dist-$sys/run-workloadb.log.tsv "${dist}\t${sys}\tb\t"
    prepend_lines $dist-$sys/run-workloadc.log.tsv "${dist}\t${sys}\tc\t"
    prepend_lines $dist-$sys/run-workloadd.log.tsv "latest\t${sys}\td\t"
  done
done
