#!/bin/bash -x

/usr/bin/odyssey /scram/config.conf

/scram/test_scram_backend.sh
/scram/test_scram_frontend.sh

ody-stop
