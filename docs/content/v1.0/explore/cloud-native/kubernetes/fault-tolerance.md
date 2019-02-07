## 1. Setup - create universe and table

If you have a previously running local universe, destroy it using the following.
<div class='copy separator-dollar'>
```sh
$ kubectl delete -f yugabyte-statefulset.yaml
```
</div>

Start a new local cluster - by default, this will create a 3 node universe with a replication factor of 3.
<div class='copy separator-dollar'>
```sh
$ kubectl apply -f yugabyte-statefulset.yaml
```
</div>

Check the Kubernetes dashboard to see the 3 yb-tserver and 3 yb-master pods representing the 3 nodes of the cluster.
<div class='copy separator-dollar'>
```sh
$ minikube dashboard
```
</div>

![Kubernetes Dashboard](/images/ce/kubernetes-dashboard.png)

Connect to cqlsh on node 1.
<div class='copy separator-dollar'>
```sh
$ kubectl exec -it yb-tserver-0 /home/yugabyte/bin/cqlsh
```
</div>
```sh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh>
```

Create a Cassandra keyspace and a table.
<div class='copy separator-gt'>
```sql
cqlsh> CREATE KEYSPACE users;
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh> CREATE TABLE users.profile (id bigint PRIMARY KEY,
	                               email text,
	                               password text,
	                               profile frozen<map<text, text>>);
```
</div>


## 2. Insert data through node 1

Now insert some data by typing the following into cqlsh shell we joined above.
<div class='copy separator-gt'>
```sql
cqlsh> INSERT INTO users.profile (id, email, password, profile) VALUES
  (1000, 'james.bond@yugabyte.com', 'licensed2Kill',
   {'firstname': 'James', 'lastname': 'Bond', 'nickname': '007'}
  );
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh> INSERT INTO users.profile (id, email, password, profile) VALUES
  (2000, 'sherlock.holmes@yugabyte.com', 'itsElementary',
   {'firstname': 'Sherlock', 'lastname': 'Holmes'}
  );
```
</div>

Query all the rows.
<div class='copy separator-gt'>
```sql
cqlsh> SELECT email, profile FROM users.profile;
```
</div>
```sql
 email                        | profile
------------------------------+---------------------------------------------------------------
      james.bond@yugabyte.com | {'firstname': 'James', 'lastname': 'Bond', 'nickname': '007'}
 sherlock.holmes@yugabyte.com |               {'firstname': 'Sherlock', 'lastname': 'Holmes'}

(2 rows)
```


## 3. Read data through another node

Let us now query the data from node 3.
<div class='copy separator-dollar'>
```sh
$ kubectl exec -it yb-tserver-2 /home/yugabyte/bin/cqlsh
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh> SELECT email, profile FROM users.profile;
```
</div>
```sql
 email                        | profile
------------------------------+---------------------------------------------------------------
      james.bond@yugabyte.com | {'firstname': 'James', 'lastname': 'Bond', 'nickname': '007'}
 sherlock.holmes@yugabyte.com |               {'firstname': 'Sherlock', 'lastname': 'Holmes'}

(2 rows)
```
```{.sh .copy .separator-gt}
cqlsh> exit;
```

## Step 4. Verify one node failure has no impact

This cluster was created with replication factor 3 and hence needs only 2 replicas to make consensus. Therefore, it is resilient to 1 failure without any data loss. Let us simulate node 3 failure.
<div class='copy separator-dollar'>
```sh
$ kubectl delete pod yb-tserver-2
```
</div>

Now running the status command should would show that the `yb-tserver-2` pod is `Terminating`.
<div class='copy separator-dollar'>
```sh
$ kubectl get pods
```
</div>
```sh
NAME           READY     STATUS        RESTARTS   AGE
yb-master-0    1/1       Running       0          33m
yb-master-1    1/1       Running       0          33m
yb-master-2    1/1       Running       0          33m
yb-tserver-0   1/1       Running       1          33m
yb-tserver-1   1/1       Running       1          33m
yb-tserver-2   1/1       Terminating   0          33m
```

Now connect to node 2.
<div class='copy separator-dollar'>
```sh
$ kubectl exec -it yb-tserver-1 /home/yugabyte/bin/cqlsh
```
</div>

Let us insert some data to ensure that the loss of a node hasn't impacted the ability of the universe to take writes.
<div class='copy separator-gt'>
```sql
cqlsh> INSERT INTO users.profile (id, email, password, profile) VALUES 
  (3000, 'austin.powers@yugabyte.com', 'imGroovy',
   {'firstname': 'Austin', 'lastname': 'Powers'});
```
</div>

Now query the data. We see that all the data inserted so far is returned and the loss of the node has no impact on data integrity.
<div class='copy separator-gt'>
```sql
cqlsh> SELECT email, profile FROM users.profile;
```
</div>
```sql
 email                        | profile
------------------------------+---------------------------------------------------------------
      james.bond@yugabyte.com | {'firstname': 'James', 'lastname': 'Bond', 'nickname': '007'}
 sherlock.holmes@yugabyte.com |               {'firstname': 'Sherlock', 'lastname': 'Holmes'}
   austin.powers@yugabyte.com |                 {'firstname': 'Austin', 'lastname': 'Powers'}

(3 rows)
```


## 5. Verify that Kubernetes brought back the failed node

We can now check the cluster status to verify that Kubernetes has indeed brought back the `yb-tserver-2` node that had failed before. This is because the replica count currently effective in Kubernetes for the `yb-tserver` StatefulSet is 3 and there were only 2 nodes remaining after 1 node failure. 
<div class='copy separator-dollar'>
```sh
$ kubectl get pods
```
</div>
```sh
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          34m
yb-master-1    1/1       Running   0          34m
yb-master-2    1/1       Running   0          34m
yb-tserver-0   1/1       Running   1          34m
yb-tserver-1   1/1       Running   1          34m
yb-tserver-2   1/1       Running   0          7s
```

YugaByte DB's fault tolerance when combined with Kubernetes's automated operations ensures that planet-scale applications can be run with ease while ensuring extreme data resilience.

## 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.
<div class='copy separator-dollar'>
```sh
$ kubectl delete -f yugabyte-statefulset.yaml
```
</div>
