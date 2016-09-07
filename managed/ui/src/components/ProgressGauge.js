// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
var GaugeJS = require('../vendors/gauge.js/gauge.min.js');

export default class ProgressGauge extends Component {
  static propTypes = {
    maxValue: PropTypes.number.isRequired,
    value: PropTypes.number.isRequired,
    title: PropTypes.string,
    width: PropTypes.number,
    height: PropTypes.number,
    options: PropTypes.object,
    type: React.PropTypes.oneOf(['Gauge', 'Donut']),
    animate: React.PropTypes.bool
  };

  static defaultProps = {
    title: "",
    animate: false,
    width: 200,
    height: 150,
    type: "Gauge",
    options: {
      pointer: {
        length: 0.70
      },
      colorStart: '#1ABB9C',
    }
  }

  componentDidMount() {
    const { type, options, maxValue, value, animate } = this.props;
    var gauge;
    if ( type === 'Donut' ) {
      gauge = new GaugeJS.Donut(this.refs.gaugeCanvas).setOptions(options);
    } else {
      gauge = new GaugeJS.Gauge(this.refs.gaugeCanvas).setOptions(options);
    }
    gauge.maxValue = maxValue;
    gauge.animationSpeed = animate ? 10 : 1;
    gauge.set(value);
  }

  render() {
    const { width, height, value } = this.props;

    return (
      <div className="progess-gauge">
        <h4 className="gauge-title">{this.props.title}</h4>
        <canvas width={width} height={height} ref="gaugeCanvas" className="gauge-canvas" />
        <h4 className="gauge-text">{value}%</h4>
      </div>
    );
  }
}
