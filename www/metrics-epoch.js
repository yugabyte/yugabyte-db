// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

var charts = [];
var last_value = [];
var last_update = Date.now();
var num_charts = 0;

// TODO allow setting this from the UI, e.g., add a pick-and-choose
// dialog box, or let a user click an 'x' next to a metric to delete
// it from the view.
var metrics_uri = makeMetricsUri(urlParams());

var MAX_SCALE_FACTOR = 1;

// Technically we can just pass the string through as-is without
// having to put it all into a map and re-encode it. However, this
// makes it easier to add new UI functionality (e.g., pick and
// choose).
function urlParams() {
  var query_params = {};
  var query = window.location.search.substring(1);
  var query_vars = query.split('&');
  for (var i = 0; i < query_vars.length; i++) {
    var entry_pair = query_vars[i].split('=');
    if (entry_pair.length == 1) {
      query_params[decodeURIComponent(entry_pair[0])] = true;
    } else {
      query_params[decodeURIComponent(entry_pair[0])] =
        decodeURIComponent(entry_pair[1]);
    }
  }
  return query_params;
}

function makeMetricsUri(url_params) {
  var BASE_METRICS_URI = "/jsonmetricz";

  url_params['detailed_metrics'] = '*';

  var components = [];
  for (var key in url_params) {
    var value = url_params[key];
    var component = encodeURIComponent(key);
    if (typeof value === 'string') {
      component += '=' + encodeURIComponent(value);
    }
    components.push(component);
  }

  var ret = BASE_METRICS_URI;
  if (components.length > 0) {
    ret += '?' + components.join('&');
  }
  return ret;
}

function createChart(name, label, type, initial_data, raw_data) {
  var span_id = 'chart_' + num_charts++;

  var div = d3.select('#metrics').append('div');
  div.attr('class', 'metrics-container');

  var name_elem = div.append('div');
  name_elem.attr('class', 'metric-name');
  name_elem.text(name);

  var display_elem = div.append('div');
  display_elem.attr('id', span_id);
  display_elem.attr('class', 'epoch category10');
  display_elem.attr('style', 'height: 100px;');

  var label_elem = div.append('div');
  label_elem.attr('class', 'metric-label');
  label_elem.html(label);

  div.append('hr');

  var data = [
    {
      'label': "foo",
      'values': [ initial_data ]
    }
  ];

  if (type == 'histogram') {
    charts[name] = $('#' + span_id).epoch({
      type: 'time.heatmap',
      data: data,
      axes: ['right', 'bottom', 'left'],
      bucketRange: [raw_data['min'], raw_data['percentile_99_9']],
      buckets: 20,
      ticks: { 'time': 3 },
    });
  } else {
    charts[name] = $('#' + span_id).epoch({
      type: 'time.line',
      data: data,
      axes: ['right', 'bottom', 'left'],
      ticks: { 'y': 3, 'time': 3 },
    });
  }
}

function updateCharts(json, error) {
  if (error) return console.warn(error);
  if (! json) {
    // Unclear why, but sometimes we seem to get a null
    // here.
    console.warn("null JSON response");
    setTimeout(function() {
      d3.json(metrics_uri, updateCharts);
    }, 1000);
    return;
  }

  var now = Date.now();
  var delta_time = now - last_update;
  var extra = {};

  json['metrics'].forEach(function(m) {
    var extra = m;
    var name = m['name'];
    var type = m['type'];
    var unit = m['unit'];
    var label = m['description'];

    var data =  { 'time': now / 1000 };

    var first_run = false;
    if (!(name in charts)) {
      first_run = true;
      last_value[name] = 0;
    }

    // For counters, we display a rate
    if (type == "counter") {
      label += "<br>(shown: rate in " + unit + " / second)";
      var cur_value = m['value'];
      if (first_run) {
        last_value[name] = cur_value;
        // display value is 0 to start
      } else {
        var delta_value = cur_value - last_value[name];
        last_value[name] = cur_value;
        var rate = delta_value / delta_time * 1000;

        data['y'] = rate;
      }

    // For charts, we simply display the value.
    } else if (type == "gauge") {
      data['y'] = m['value'];

    } else if (type == "histogram") {
      // For histograms, we display a heat map

      if (first_run) {
        last_value[name] = {};
      }

      var values = m['values'];
      var counts = m['counts'];
      if (! values) { values = []; }
      if (! counts) { counts = []; }
      var hist = {};
      var prev = last_value[name];
      for (var i = 0; i < values.length; i++) {
        var value = values[i];
        var count = counts[i];
        hist[value] = count;
      }
      last_value[name] = $.extend({}, hist);

      for (value in hist) {
        if (value in prev) {
          hist[value] -= prev[value];
        }
      }
      data['histogram'] = hist;
    } else {
      // For non-special-cased stuff just print the value field as well, if available.
      if ("value" in m) {
        data['y'] = m['value'];
      }
    }

    if (first_run) {
      createChart(name, label, type, data, extra);
    } else {
      charts[name].push([data]);
    }
  });

  last_update = now;

  setTimeout(function() {
    d3.json(metrics_uri, updateCharts);
  }, 1000);
}

function initialize() {
  d3.json(metrics_uri, updateCharts);
}
