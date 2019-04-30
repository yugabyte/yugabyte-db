<h2>Cluster Install Directories</h2>
Cluster data is installed in <code>HOME/yugabyte-data/</code> by default
<h3>Node Directories</h3>
<p><code>yugabyte-data/node-#/</code> directory created for node #</p>
<p>Contains:
<li><code>yugabyte-data/node-#/disk-#/</code></li>
<li><code>initdb.log</code></li>
<li><code>cluster_config.json</code></li>
</p>
<h3>Disk Directories</h3>
<p><code>yugabyte-data/node-#/disk-#/</code> directory created for each disk</p>
<p>Contains:
<li><code>yugabyte-data/node-#/disk-#/pg_data/</code> (directory for PostgreSQL data)</li>
<li><code>yugabyte-data/node-#/disk-#/yg-data/</code> (directory for YugaByte data)</li>
</p>
<h2>Logs</h2>
<p>Master logs:<p>
<p><pre><code>yugabyte-data/node-#/disk-#/yg-data/master/logs</code></pre></p>
<p>Tserver logs:</p>
<p><pre><code>yugabyte-data/node-#/disk-#/yg-data/tserver/logs</code></pre></p>
