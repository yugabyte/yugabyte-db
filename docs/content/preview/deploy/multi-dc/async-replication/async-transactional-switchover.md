---
title: Unplanned failover with transactional xCluster replication
headerTitle: Planned switchover
linkTitle: Switchover
description: Switchover using transactional (active-standby) replication between universes
headContent: Switch application traffic to the standby universe
menu:
  preview:
    parent: async-replication-transactional
    identifier: async-transactional-switchover
    weight: 40
type: docs
---

Planned switchover is the process of switching write access from the source (active) universe to the target (standby) universe without losing any data (zero RPO). Planned failover is typically performed during a maintenance window.

## Manual switchover

Assuming A is the source (active) universe and B is the target (standby), use the following steps to perform a planned failover.

### Stop and drain the universe

In the first stage, you drain pending updates and ensure that the replication lag is zero, thus guaranteeing zero RPO. You wait for pending updates to propagate to B by running the `wait_for_replication_drain` command on A.

Proceed as follows:

1. Stop the application traffic to ensure no more data changes are attempted.

1. Obtain the `stream_ids` for A:

    ```sh
    ./bin/yb-admin \
        -master_addresses <A_master_addresses> \
        -certs_dir_name <dir_name>  \
        list_cdc_streams
    ```

    Expect output similar to the following:

    ```output.json
    CDC Streams:
    streams {
      stream_id: "56bf794172da49c6804cbab59b978c7e"
      table_id: "000033e8000030008000000000004000"
      options {
        key: "record_type"
        value: "CHANGE"
      }
      options {
        key: "checkpoint_type"
        value: "IMPLICIT"
      }
      options {
        key: "record_format"
        value: "WAL"
      }
      options {
        key: "source_type"
        value: "XCLUSTER"
      }
      options {
        key: "state"
        value: "ACTIVE"
      }
    }
    ```

1. Run the following script to get the current time on A:

    ```python
    python3 checkTime.py

    import datetime 
    print(datetime.datetime.now().strftime("%s%f"))
    print(datetime.datetime.now())
    ```

    ```output
    1665548814177072
    2023-02-16 12:20:22.245984
    ```

1. Run the following command for each ACTIVE replication `stream_id` reported on A using the output of the two previous commands, in the form of a comma-separated list of the stream IDs and the timestamp:

    ```sh
    ./bin/yb-admin \
        -master_addresses <A_master_addresses> \
        -certs_dir_name <dir_name> \
        wait_for_replication_drain 56bf794172da49c6804cbab59b978c7e,..,..<<comma_separated_list_of_stream_ids>> 1665548814177072
    ```

    ```output
    All replications are caught-up.
    ```

    Note that this step may take some time.

    If you get output similar to the following, then it is not yet safe to switch universe B (current standby) to active:

    ```output
    Found undrained replications:
    - Under Stream b7306a8068484634801fd925a41f17f6:
      - Tablet: 04dd056e79a14f48aac3bdf196546fd0
      - Tablet: ebc07bd926b647a5ba8cea41421ec7ca
      - Tablet: 3687d4d51cda4f0394b4c36aa66f4ffd
      - Tablet: 318745aee32c463c81d4242924d679a2
    ```

1. Wait until xCluster safe time on B is greater than the current time obtained on A.

1. Use `change_xcluster_role` to promote B (current standby) to ACTIVE.

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> \ 
        -certs_dir_name <dir_name> \
        change_xcluster_role ACTIVE
    ```

1. Delete the replication from A to B:

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> \
        delete_universe_replication <A_universe_uuid>_<replication_name>
    ```

### Set up replication

In the second stage, set up replication from the new active (B) universe as follows:

1. Run `bootstrap_cdc_producer` command on B for all tables involved in replication and capture the bootstrap IDs. This is a preemptive step to be used during the failover workflow while setting up replication from B to A. This prevents the WAL from being garbage collected and lets A catch up.

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> 
        -certs_dir_name <cert_dir> \
        bootstrap_cdc_producer <comma_separated_B_table_ids>
    ```

1. Resume the application traffic on B.

1. Prepare certificates on all A nodes for replication setup:

    - Create a directory for the replication certificate:

        ```sh
        mkdir -p <home>/yugabyte-tls-producer/<B_universe_uuid>_<replicaten_name>
        ```

    - Copy the certificates:

        ```sh
        cp -rf <home>/yugabyte-tls-producer/<B_universe_uuid> <home>/yugabyte-tls-producer/<B_universe_uuid>_<replicaten_name>
        ```

    Repeat these steps on all A nodes.

1. Set up replication from B to A using the bootstrap IDs obtained previously.

    ```sh
    ./bin/yb-admin \
        -master_addresses <A_master_addresses> \
        -certs_dir_name <cert_dir> \
        setup_universe_replication \
        <B_universe_uuid>_<replication_name> \
        <B_master_addresses> \
        <comma_separated_B_table_ids>  \
        <comma_separated_B_bootstrap_ids> transactional
    ```

1. Use `change_xcluster_role` to demote A to STANDBY:

    ```sh
    ./bin/yb-admin \
        -master_addresses <A_master-addresses> \ 
        -certs_dir_name <dir_name> \
        change_xcluster_role STANDBY
    ```

After completing the preceding steps, universe B becomes the new source (active) universe, and universe A becomes the new target (standby) universe. No updates are lost during the process, so the RPO is zero. There is expected to be no data loss in this workflow.

<!--

Planned switchover is the process of switching database role (role reversal) from Primary to Standby and vice versa. This enables application traffic to switch from primary cluster to the standby cluster without losing any data (zero RPO). Planned switchover is typically performed during a maintenance window.

First confirmation step for Planned Faiover is to ensure there is no lag between Primary and Standby. If there is a lag > log_min_seconds_to_retain secs and WALs have GC-ed, then Planned Failover is expected to be unsuccessful. 

Options available in that case are : 


Bootstrapping from A to B.
Unplanned Failover


with xCluster-ctl

Planned Failover support via YBA is slated to be rolled out in a future release. 
In the interim, xCluster-ctl is a standalone script provided to ease the testing of Planned Failover scenario during the PoC phase, especially when a large number of objects are involved. 
xCluster-ctl is *not* designed for production usage and is not supported or maintained by Yugabyte’s customer support, product or development organizations.

Get xcluster-ctl from https://github.com/	hari90/xcluster-ctl.git

python3 xcluster-ctl.py configure // Only required once
python3 xcluster-ctl.py reload_roles

Example output:
$ python3 xcluster-ctl.py reload_roles
[ yb-dev-ybdb-xcluster-1 -> yb-dev-ybdb-xcluster-3 ] 
Portal Config: 
{'url': 'https://52.55.102.221', 'customer_id': 'b452ec1a-c4bf-4e3e-86ff-0d046e4b9c91', 'token': 'eb43036c-f64e-4971-86f6-257fdb96869a'} 
Primary Universe: 
{'universe_name': 'yb-dev-ybdb-xcluster-1', 'master_ips': '['10.37.1.235', '10.37.2.253', '10.37.3.54']', 'tserver_ips': '['10.37.2.253', '10.37.1.235', '10.37.3.54']', 'role': 'ACTIVE'} 
Standby Universe: 
{'universe_name': 'yb-dev-ybdb-xcluster-3', 'master_ips': '['10.37.3.161', '10.37.3.43', '10.37.3.91']', 'tserver_ips': '['10.37.3.91', '10.37.3.161', '10.37.3.43']', 'role': 'STANDBY'} 

Check to see if the Primary and Secondary universes are correct. If not then swap them using
python3 xcluster-ctl.py switch_universe_configs

Example output:
$ python3 xcluster-ctl.py switch_universe_configs
[ yb-dev-ybdb-xcluster-3 -> yb-dev-ybdb-xcluster-1 ] 
Swapping Primary and Standby universes 
[ yb-dev-ybdb-xcluster-1 -> yb-dev-ybdb-xcluster-3 ] 

Perform the failover operation:
python3 xcluster-ctl.py planned_failover

	
Example output:
$ python3 xcluster-ctl.py planned_failover
[ yb-dev-ybdb-xcluster-1 -> yb-dev-ybdb-xcluster-3 ] 
Performing a planned failover from yb-dev-ybdb-xcluster-1 to yb-dev-ybdb-xcluster-3 
Found replication group 1-to-3 with 178 tables 
Has the workload been stopped? (yes/no): yes << stop the application workflow on Primary
Waiting for drain of 178 replication streams... 
.
All replications are caught-up. 
Successfully completed wait for replication drain 
database_name:		test1
xcluster_safe_time:	2023-06-27 23:03:13.651436
database_name:		yugabyte
xcluster_safe_time:	2023-06-27 23:03:13.795260 
Setting yb-dev-ybdb-xcluster-3 to ACTIVE 
Successfully set role to ACTIVE 
Deleting replication 1-to-3 from yb-dev-ybdb-xcluster-1 to yb-dev-ybdb-xcluster-3 
Replication deleted successfully 
Setting yb-dev-ybdb-xcluster-3 to ACTIVE 
Already in ACTIVE role 
Successfully synced Portal 
Successfully deleted replication 
Swapping Primary and Standby universes 
[ yb-dev-ybdb-xcluster-3 -> yb-dev-ybdb-xcluster-1 ] 
Setting up replication from yb-dev-ybdb-xcluster-3 to yb-dev-ybdb-xcluster-1 with bootstrap 
Setting up PITR snapshot schedule for yugabyte on yb-dev-ybdb-xcluster-3 
Successfully created PITR snapshot schedule for yugabyte 
Getting tables for database(s) test3,test8,test6,test5,yugabyte,test2,test10,test7,test9,test1,test4 from yb-dev-ybdb-xcluster-3 
.
Checkpointing 178 tables 
.
Successfully bootstrapped databases. Run setup_replication_with_bootstrap command to complete setup 
Copying cert files to yb-dev-ybdb-xcluster-1 
Replication setup successfully 
Setting up PITR snapshot schedule for yugabyte on yb-dev-ybdb-xcluster-1 
Successfully created PITR snapshot schedule for yugabyte 
Setting yb-dev-ybdb-xcluster-1 role to STANDBY 
Successfully synced Portal 
database_name:		test1
xcluster_safe_time:	2023-06-27 23:04:05.358696
database_name:		yugabyte
xcluster_safe_time:	2023-06-27 23:04:05.450287 
Successfully setup replication 
Successfully failed over from yb-dev-ybdb-xcluster-1 to yb-dev-ybdb-xcluster-3 
40



Manual Switchover steps (in case you decide not to use xCluster-ctl)
Assuming cluster A is the ACTIVE (PRIMARY) Universe and cluster B is the STANDBY Universe, the following steps are required to perform a planned failover:

Ensure there is no lag between Primary and Standby.
Stop the application traffic on A to ensure no more data changes are attempted to B.
Use the following API to drain pending updates and ensure zero replication lag, thus guaranteeing zero-RPO.
Wait for pending updates to propagate to Standby by running the wait_for_replication_drain API on Primary
Run on Primary to obtain the ACTIVE stream_ids :
yb-admin 
-master_addresses <Primary_master_ips> \
-certs_dir_name <dir_name>  \
list_cdc_streams


Example Output:
CDC Streams:
streams {
  stream_id: "56bf794172da49c6804cbab59b978c7e"
  table_id: "000033e8000030008000000000004000"
  options {
    key: "record_type"
    value: "CHANGE"
  }
  options {
    key: "checkpoint_type"
    value: "IMPLICIT"
  }
  options {
    key: "record_format"
    value: "WAL"
  }
  options {
    key: "source_type"
    value: "XCLUSTER"
  }
  options {
    key: "state"
    value: "ACTIVE"
  }
}


Run the following script to get the current time on Primary :
            
python3 checkTime.py


import datetime 
print(datetime.datetime.now().strftime("%s%f"))
print(datetime.datetime.now())


Example Output:
1665548814177072
2023-02-16 12:20:22.245984


Run this command for each ACTIVE replication stream_id reported on Primary using the output of the two previous commands:
Run on Primary :
yb-admin 
-master_addresses <universe_A_master_ips> \ <<Primary Universe
-certs_dir_name <dir_name>  \
wait_for_replication_drain 56bf794172da49c6804cbab59b978c7e,..,..<<comma separated list of streams>> 1665548814177072


Example Output:
All replications are caught-up.


Note: this step may take some time.




Note: If you get the following output, then it is not safe yet to switch universe B (current STANDBY) to PRIMARY. 
Retry the operation on Primary :


yb-admin 
-master_addresses <universe_A_master_ips> \  
-certs_dir_name <dir_name>  \
wait_for_replication_drain \
56bf794172da49c6804cbab59b978c7e


Found undrained replications:
- Under Stream b7306a8068484634801fd925a41f17f6:
  - Tablet: 04dd056e79a14f48aac3bdf196546fd0
  - Tablet: ebc07bd926b647a5ba8cea41421ec7ca
  - Tablet: 3687d4d51cda4f0394b4c36aa66f4ffd
  - Tablet: 318745aee32c463c81d4242924d679a2


Wait until xCluster SafeTime (on Standby) is > time captured in the previous step 3.b

Switch STANDBY to ACTIVE(PRIMARY) mode.

Switch roles : 
Switch Standby to Primary 
Use change_xcluster_role API to Promote B (current Standby)  to Active/PRIMARY role. 


Run on B (standby)
yb-admin
-master_addresses <universe_B_master-ips> \ 
-certs_dir_name <dir_name> \
change_xcluster_role ACTIVE


Delete replication from A->B using YBA UI

Run bootstrap_cdc_producer command at B for all tables involved in replication and capture the bootstrap ids. This is a preemptive step to be used during the Failback workflow while setting up replication from B->A. This will prevent WAL from being GC’ed and let A catchup
	
Run on B (New Active):
yb-admin 
-master_addresses <B_master_ips> 
-certs_dir_name <cert_dir> \
bootstrap_cdc_producer <comma_separated_B_universe_table_ids>


Prepare certificates on all A nodes for replication setup:
Create directory for replication certificate mkdir -p <home>/yugabyte-tls-producer/<B_universe_uuid>_<replicaten_name>
Copy certificates cp -rf <home>/yugabyte-tls-producer/<B_universe_uuid> <home>/yugabyte-tls-producer/<B_universe_uuid>_<replicaten_name>
Do it on all A hosts
Setup replication from B->A with the bootstrap ids from Step 5.


Setup replication (run on A)
yb-admin \
-master_addresses <Primary_master_ips> \
-certs_dir_name <cert_dir> \
 setup_universe_replication \
 <B_universe_uuid>_<replication_name> \
 <B_master_ips> \
 <comma_separated_list_of_table_ids_from_B>  \
 <comma_separated_list_of_bootstrap_ids_from_B> transactional

Use change_xcluster_role API to Demote A to STANDBY role. 
Run on A (ex-Primary)
yb-admin 
-master_addresses <universe_A_master-ips> \ 
-certs_dir_name <dir_name> 
change_xcluster_role STANDBY
Setup YBA Monitoring for Standby
Use this API to import replication metrics into YBA 
Sync xcluster config | YugabyteDB Anywhere APIs
Go to YBA console to verify replication is visible
On Standby, verify xcluster_safe_time is progressing. This is the indicator of dataflow from Primary to Standby.
yb-admin \
-master_addresses <Standby_master_ips>
-certs_dir_name <dir_name> 
get_xcluster_safe_time

$ ./tserver/bin/yb-admin -master_addresses 172.150.21.61:7100,172.150.44.121:7100,172.151.23.23:7100 get_xcluster_safe_time
[
    {
        "namespace_id": "00004000000030008000000000000000",
        "namespace_name": "dr_db",
        "safe_time": "2023-06-07 16:31:17.601527",
        "safe_time_epoch": "1686155477601527"
    }
]


Default time reported in xCluster SafeTime is year 2112. Need to wait until the xCluster SafeTime on Standby advances beyond setup replication clock time.
Resume the application traffic on B.



After completing the steps above, cluster B becomes the new primary cluster, and cluster A becomes the new STANDBY cluster. No updates are lost during the process, so the RPO is zero.
There is expected to be no data loss in this workflow.

-->