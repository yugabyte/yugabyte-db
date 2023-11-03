#!/bin/bash -x

PGPASSWORD=incorrect_password psql -h localhost -p 6432 -U frontend_auth_plain -c "SELECT 1" scram_db >/dev/null 2>&1 && {
        echo "ERROR: successfully auth with incorrect password and plain password in config"
        ody-stop
        exit 1
}

PGPASSWORD=correct_password psql -h localhost -p 6432 -U frontend_auth_plain -c "SELECT 1" scram_db >/dev/null 2>&1 || {
        echo "ERROR: failed auth with correct password and plain password in config"
        ody-stop
        exit 1
}

PGPASSWORD=incorrect_password psql -h localhost -p 6432 -U frontend_auth_scram_secret -c "SELECT 1" scram_db >/dev/null 2>&1 && {
        echo "ERROR: successfully auth with incorrect password and scram secret in config"
        ody-stop
        exit 1
}

PGPASSWORD=correct_password psql -h localhost -p 6432 -U frontend_auth_scram_secret -c "SELECT 1" scram_db >/dev/null 2>&1 || {
        echo "ERROR: failed auth with correct password and scram secret in config"
        ody-stop
        exit 1
}
