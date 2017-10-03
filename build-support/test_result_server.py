#!/usr/bin/env python
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# Simple HTTP server which receives test results from the build slaves and
# stores them in a MySQL database. The test logs are also stored in an S3 bucket.
#
# Configuration here is done via environment variables:
#
# MySQL config:
#   MYSQLHOST - host running mysql
#   MYSQLUSER - username
#   MYSQLPWD  - password
#   MYSQLDB   - mysql database
#
# S3 config:
#   AWS_ACCESS_KEY     - AWS access key
#   AWS_SECRET_KEY     - AWS secret key
#   TEST_RESULT_BUCKET - bucket to store results in (eg 'kudu-test-results')
#
# If the AWS credentials are not configured, falls back to using Boto's
# default configuration (http://boto.cloudhackers.com/en/latest/boto_config_tut.html)
#
# Installation instructions:
#   You probably want to run this inside a virtualenv to avoid having
#   to install python modules systemwide. For example:
#     $ virtualenv ~/flaky-test-server-env/
#     $ . ~/flaky-test-server-env/bin/activate
#     $ pip install boto
#     $ pip install jinja2
#     $ pip install cherrypy
#     $ pip install MySQL-python

import boto
import cherrypy
import gzip
import itertools
from jinja2 import Template
import logging
import MySQLdb
import os
import parse_test_failure
from StringIO import StringIO
import threading
import uuid

class TRServer(object):
  def __init__(self):
    self.thread_local = threading.local()
    self.ensure_table()
    self.s3 = self.connect_s3()
    self.s3_bucket = self.s3.get_bucket(os.environ["TEST_RESULT_BUCKET"])

  def connect_s3(self):
    access_key = os.environ.get("AWS_ACCESS_KEY")
    secret_key = os.environ.get("AWS_SECRET_KEY")
    s3 = boto.connect_s3(access_key, secret_key)
    logging.info("Connected to S3 with access key %s" % access_key)
    return s3


  def upload_to_s3(self, key, fp, filename):
    k = boto.s3.key.Key(self.s3_bucket)
    k.key = key
    # The Content-Disposition header sets the filename that the browser
    # will use to download this.
    # We have to cast to str() here, because boto will try to escape the header
    # incorrectly if you pass a unicode string.
    k.set_metadata('Content-Disposition', str('inline; filename=%s' % filename))
    k.set_contents_from_string(fp.read(),
                               reduced_redundancy=True)

  def connect_mysql(self):
    if hasattr(self.thread_local, "db") and \
          self.thread_local.db is not None:
      return self.thread_local.db

    host = os.environ["MYSQLHOST"]
    user = os.environ["MYSQLUSER"]
    pwd = os.environ["MYSQLPWD"]
    db = os.environ["MYSQLDB"]
    self.thread_local.db = MySQLdb.connect(host, user, pwd, db)
    self.thread_local.db.autocommit(True)
    logging.info("Connected to MySQL at %s" % host)
    return self.thread_local.db

  def execute_query(self, query, *args):
    """ Execute a query, automatically reconnecting on disconnection. """
    # We'll try up to 3 times to reconnect
    MAX_ATTEMPTS = 3

    # Error code for the "MySQL server has gone away" error.
    MYSQL_SERVER_GONE_AWAY = 2006

    attempt_num = 0
    while True:
      c = self.connect_mysql().cursor(MySQLdb.cursors.DictCursor)
      attempt_num = attempt_num + 1
      try:
        c.execute(query, *args)
        return c
      except MySQLdb.OperationalError as err:
        if err.args[0] == MYSQL_SERVER_GONE_AWAY and attempt_num < MAX_ATTEMPTS:
          logging.warn("Forcing reconnect to MySQL: %s" % err)
          self.thread_local.db = None
          continue
        else:
          raise


  def ensure_table(self):
    c = self.execute_query("""
      CREATE TABLE IF NOT EXISTS test_results (
        id int not null auto_increment primary key,
        timestamp timestamp not null default current_timestamp,
        build_id varchar(100),
        revision varchar(50),
        build_config varchar(100),
        hostname varchar(255),
        test_name varchar(100),
        status int,
        log_key char(40),
        INDEX (revision),
        INDEX (test_name),
        INDEX (timestamp)
      );""")

  @cherrypy.expose
  def index(self):
    return "Welcome to the test result server!"

  @cherrypy.expose
  def add_result(self, **kwargs):
    args = {}
    args.update(kwargs)

    # Only upload the log if it's provided.
    if 'log' in kwargs:
      log = kwargs['log']
      s3_id = uuid.uuid1()
      self.upload_to_s3(s3_id, log.file, log.filename)
    else:
      s3_id = None
    args['log_key'] = s3_id

    logging.info("Handling report: %s" % repr(args))

    self.execute_query(
      "INSERT INTO test_results(build_id, revision, build_config, hostname, test_name, status, log_key) "
      "VALUES (%(build_id)s, %(revision)s, %(build_config)s, %(hostname)s, %(test_name)s,"
      "%(status)s, %(log_key)s)",
      args)
    return "Success!\n"

  @cherrypy.expose
  def download_log(self, key):
    expiry = 60 * 60 * 24 # link should last 1 day
    k = boto.s3.key.Key(self.s3_bucket)
    k.key = key
    raise cherrypy.HTTPRedirect(k.generate_url(expiry))

  @cherrypy.expose
  def diagnose(self, key):
    k = boto.s3.key.Key(self.s3_bucket)
    k.key = key
    log_text_gz = k.get_contents_as_string()
    log_text = gzip.GzipFile(fileobj=StringIO(log_text_gz)).read().decode('utf-8')
    summary = parse_test_failure.extract_failure_summary(log_text)
    if not summary:
      summary = "Unable to diagnose"
    template = Template("""
      <h1>Diagnosed failure</h1>
      <code><pre>{{ summary|e }}</pre></code>
      <h1>Full log</h1>
      <code><pre>{{ log_text|e }}</pre></code>
    """)
    return self.render_container(template.render(summary=summary, log_text=log_text))

  def recently_failed_html(self):
    """ Return an HTML report of recently failed tests """
    c = self.execute_query(
      "SELECT * from test_results WHERE status != 0 "
      "AND timestamp > NOW() - INTERVAL 1 WEEK "
      "ORDER BY timestamp DESC LIMIT 50")
    failed_tests = c.fetchall()

    prev_date = None
    for t in failed_tests:
      t['is_new_date'] = t['timestamp'].date() != prev_date
      prev_date = t['timestamp'].date()

    template = Template("""
    <h1>50 most recent failures</h1>
    <table class="table">
      <tr>
        <th>test</th>
        <th>config</th>
        <th>exit code</th>
        <th>rev</th>
        <th>machine</th>
        <th>time</th>
        <th>build</th>
      </tr>
      {% for run in failed_tests %}
        {% if run.is_new_date %}
          <tr class="new-date">
            <th colspan="7">{{ run.timestamp.date()|e }}</th>
          </tr>
        {% endif %}
        <tr>
          <td><a href="/test_drilldown?test_name={{ run.test_name |urlencode }}">
              {{ run.test_name |e }}
              </a></td>
          <td>{{ run.build_config |e }}</td>
          <td>{{ run.status |e }}
            {% if run.log_key %}
              <a href="/download_log?key={{ run.log_key |urlencode }}">failure log</a> |
              <a href="/diagnose?key={{ run.log_key |urlencode }}">diagnose</a>
            {% endif %}
          </td>
          <td>{{ run.revision |e }}</td>
          <td>{{ run.hostname |e }}</td>
          <td>{{ run.timestamp |e }}</td>
          <td>{{ run.build_id |e }}</td>
        </tr>
      {% endfor %}
    </table>
    """)
    return template.render(failed_tests=failed_tests)

  def flaky_report_html(self):
    """ Return an HTML report of recently flaky tests """
    c = self.execute_query(
              """SELECT
                   test_name,
                   DATEDIFF(NOW(), timestamp) AS days_ago,
                   SUM(IF(status != 0, 1, 0)) AS num_failures,
                   COUNT(*) AS num_runs
                 FROM test_results
                 WHERE timestamp > NOW() - INTERVAL 1 WEEK
                 GROUP BY test_name, days_ago
                 HAVING num_failures > 0
                 ORDER BY test_name""")
    rows = c.fetchall()

    results = []
    for test_name, test_rows in itertools.groupby(rows, lambda r: r['test_name']):
      # Convert to list so we can consume it multiple times
      test_rows = list(test_rows)

      # Compute summary for last 7 days and last 2 days
      runs_7day = sum(r['num_runs'] for r in test_rows)
      failures_7day = sum(r['num_failures'] for r in test_rows)
      runs_2day = sum(r['num_runs'] for r in test_rows if r['days_ago'] < 2)
      failures_2day = sum(r['num_failures'] for r in test_rows if r['days_ago'] < 2)

      # Compute a sparkline (percentage failure for each day)
      sparkline = [0 for x in xrange(8)]
      for r in test_rows:
        if r['num_runs'] > 0:
          percent = float(r['num_failures']) / r['num_runs'] * 100
        else:
          percent = 0
        sparkline[7 - r['days_ago']] = percent

      # Add to results list for tablet.
      results.append(dict(test_name=test_name,
                          runs_7day=runs_7day,
                          failures_7day=failures_7day,
                          runs_2day=runs_2day,
                          failures_2day=failures_2day,
                          sparkline=",".join("%.2f" % p for p in sparkline)))

    return Template("""
    <h1>Flaky rate over last week</h1>
    <table class="table" id="flaky-rate">
      <tr>
       <th>test</th>
       <th>failure rate (7-day)</th>
       <th>failure rate (2-day)</th>
       <th>trend</th>
      </tr>
      {% for r in results %}
      <tr>
        <td><a href="/test_drilldown?test_name={{ r.test_name |urlencode }}">
              {{ r.test_name |e }}
            </a></td>
        <td>{{ r.failures_7day |e }} / {{ r.runs_7day }}
            ({{ "%.2f"|format(r.failures_7day / r.runs_7day * 100) }}%)
        </td>
        <td>{{ r.failures_2day |e }} / {{ r.runs_2day }}
            {% if r.runs_2day > 0 %}
            ({{ "%.2f"|format(r.failures_2day / r.runs_2day * 100) }}%)
            {% endif %}
        </td>
        <td><span class="inlinesparkline">{{ r.sparkline |e }}</span></td>
      </tr>
      {% endfor %}
    </table>
    <script type="text/javascript">
      $(function() {
        $('.inlinesparkline').sparkline('html', {
           'height': 25,
            'width': '40px',
            'chartRangeMin': 0,
            'tooltipFormatter': function(sparkline, options, fields) {
              return String(7 - fields.x) + "d ago: " + fields.y + "%"; }
        });
      });
    </script>
    """).render(results=results)

  @cherrypy.expose
  def list_failed_tests(self, build_pattern, num_days):
    num_days = int(num_days)
    c = self.execute_query(
              """SELECT DISTINCT
                   test_name
                 FROM test_results
                 WHERE timestamp > NOW() - INTERVAL %(num_days)s DAY
                   AND status != 0
                   AND build_id LIKE %(build_pattern)s""",
              dict(build_pattern=build_pattern,
                   num_days=num_days))
    cherrypy.response.headers['Content-Type'] = 'text/plain'
    return "\n".join(row['test_name'] for row in c.fetchall())

  @cherrypy.expose
  def test_drilldown(self, test_name):

    # Get summary statistics for the test, grouped by revision
    c = self.execute_query(
              """SELECT
                   revision,
                   MIN(timestamp) AS first_run,
                   SUM(IF(status != 0, 1, 0)) AS num_failures,
                   COUNT(*) AS num_runs
                 FROM test_results
                 WHERE timestamp > NOW() - INTERVAL 1 WEEK
                   AND test_name = %(test_name)s
                 GROUP BY revision
                 ORDER BY first_run DESC""",
              dict(test_name=test_name))
    revision_rows = c.fetchall()

    # Convert to a dictionary, by revision
    rev_dict = dict( [(r['revision'], r) for r in revision_rows] )

    # Add an empty 'runs' array to each revision to be filled in below
    for r in revision_rows:
      r['runs'] = []

    # Append the specific info on failures
    c.execute("SELECT * from test_results "
              "WHERE timestamp > NOW() - INTERVAL 1 WEEK "
              "AND test_name = %(test_name)s "
              "AND status != 0",
              dict(test_name=test_name))
    for failure in c.fetchall():
      rev_dict[failure['revision']]['runs'].append(failure)

    return self.render_container(Template("""
    <h1>{{ test_name |e }} flakiness over recent revisions</h1>
    {% for r in revision_rows %}
      <h4>{{ r.revision }} (Failed {{ r.num_failures }} / {{ r.num_runs }})</h4>
      {% if r.num_failures > 0 %}
        <table class="table">
          <tr>
            <th>time</th>
            <th>config</th>
            <th>exit code</th>
            <th>machine</th>
            <th>build</th>
          </tr>
          {% for run in r.runs %}
            <tr {% if run.status != 0 %}
                  style="background-color: #faa;"
                {% else %}
                  style="background-color: #afa;"
                {% endif %}>
              <td>{{ run.timestamp |e }}</td>
              <td>{{ run.build_config |e }}</td>
              <td>{{ run.status |e }}
                {% if run.log_key %}
                  <a href="/download_log?key={{ run.log_key |urlencode }}">failure log</a> |
                  <a href="/diagnose?key={{ run.log_key |urlencode }}">diagnose</a>
                {% endif %}
              </td>
              <td>{{ run.hostname |e }}</td>
              <td>{{ run.build_id |e }}</td>
            </tr>
          {% endfor %}
        </table>
      {% endif %}
    {% endfor %}
    """).render(revision_rows=revision_rows, test_name=test_name))

  @cherrypy.expose
  def index(self):
    body = self.flaky_report_html()
    body += "<hr/>"
    body += self.recently_failed_html()
    return self.render_container(body)

  def render_container(self, body):
    """ Render the "body" HTML inside of a bootstrap container page. """
    template = Template("""
    <!DOCTYPE html>
    <html>
      <head><title>Kudu test results</title>
      <link rel="stylesheet" href="//maxcdn.bootstrapcdn.com/bootstrap/3.2.0/css/bootstrap.min.css" />
      <style>
        .new-date { border-bottom: 2px solid #666; }
        #flaky-rate tr :nth-child(1) { width: 70%; }

        /* make sparkline data not show up before loading */
        .inlinesparkline { color: #fff; }
        /* fix sparkline tooltips */
        .jqstooltip {
          -webkit-box-sizing: content-box;
          -moz-box-sizing: content-box;
          box-sizing: content-box;
        }
      </style>
    </head>
    <body>
      <script src="//ajax.googleapis.com/ajax/libs/jquery/1.11.1/jquery.min.js"></script>
      <script src="//maxcdn.bootstrapcdn.com/bootstrap/3.2.0/js/bootstrap.min.js"></script>
      <script src="https://cdnjs.cloudflare.com/ajax/libs/jquery-sparklines/2.1.2/jquery.sparkline.min.js"></script>
      <div class="container-fluid">
      {{ body }}
      </div>
    </body>
    </html>
    """)
    return template.render(body=body)

if __name__ == "__main__":
  logging.basicConfig(level=logging.INFO)
  cherrypy.config.update(
    {'server.socket_host': '0.0.0.0'} )
  cherrypy.quickstart(TRServer())
