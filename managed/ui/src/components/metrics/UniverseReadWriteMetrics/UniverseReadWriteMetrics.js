// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {isValidObject} from '../../../utils/ObjectUtils';

var Plotly = require('plotly.js/lib/core');

export default class UniverseReadWriteMetrics extends Component {
  componentWillReceiveProps(nextProps) {
    const {universe: {iostat_read_count, iostat_write_count}, graphIndex, type} = nextProps;
    var metricData = [];
    if (isValidObject(iostat_read_count)) {
      if (type === "read") {
        metricData = iostat_read_count;
      } else {
        metricData = iostat_write_count;
      }
      if (isValidObject(metricData)) {
        var layout = {
          margin: {l: 0, r: 0, t: 0, b: 0, pad: 0, autoexpand: false},
          xaxis: {showline: false, showgrid: false, zeroline: false},
          yaxis: {showline: false, showgrid: false, zeroline: false},
          showlegend: false,
          autosize: false,
          height: 30,
          width: 100,
          paper_bgcolor: 'rgba(0,0,0,0)',
          plot_bgcolor: 'rgba(0,0,0,0)'
        };
        var metricXData = metricData.x;
        var metricYData = metricData.y;
        var data = [
          {
            x: metricXData,
            y: metricYData,
            mode: 'lines',
            line: {
              color: '#CD6500',
              width: 2
            }
          }
        ];
        Plotly.newPlot(`lineGraph${graphIndex}`, data, layout, {displayModeBar: false});
      }
    }
  }

  render() {
    const {graphIndex} = this.props;
    return (
      <div id={`lineGraph${graphIndex}`}/>
    );
  }
}
