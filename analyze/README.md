# PostgreSQL Audit Log Analyzer

The PostgreSQL Audit extension (`pgaudit`) provides detailed session and/or object audit logging via the standard PostgreSQL logging facility.  However, logs are not the ideal place to store audit information.  The PostgreSQL Audit Log Analyzer (`pgaudit_analyze`) reads audit entries from the PostgreSQL logs and loads them into a database schema to aid in analysis and auditing.

## Installing

* Install pgaudit following the instructions included with the extension.

* Update the log settings in postgresql.conf as follows:
```
log_destination = 'csvlog'
logging_collector = on
log_connections = on
```
The log files must end with `.csv` and follow a naming convention that ensures files will sort alphabetically with respect to creation time.  Log location is customizable when calling `pgaudit_analyze`.

* Install `pgaudit_analyze`:

Copy the bin and lib directories to any location you prefer but make sure there are in the same directory.

* Execute audit.sql in the database you want to audit as postgres:
```
psql -U postgres -f sql/audit.sql <db name>
```
## Running
```
./pgaudit_analyze --daemon /path/to/log/files
```
## Testing

Regression tests are located at test/test.pl.  You may need to set `--pgsql-bin` depending on your local configuration.

A `Vagrantfile` has been included in the test directory which gives the exact steps needed to get the regression rests running on `CentOS 7`.  After logging on to the `vagrant` box simply run:
```
/pgaudit/analyze/test/test.pl
```
## Caveats

* The pgaudit.logon table contains the logon information for users of the database.  If a user is renamed they must also be renamed in this table or the logon history will be lost.

* Reads and writes to the pgaudit schema by the user running `pgaudit_analyze` are never logged.

## Authors

The PostgreSQL Audit Log Analyzer was written by David Steele <david.steele@crunchydata.com>.
