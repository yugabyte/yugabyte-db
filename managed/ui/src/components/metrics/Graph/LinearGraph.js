// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';

import { TimelineMax, Power2 } from "gsap/all";
import './Graph.scss';

const colorObj = {
  green: {
    r: 51,
    g: 158,
    b: 52,
  },
  yellow: {
    r: 240,
    g: 204,
    b: 98,
  },
  red: {
    r: 233,
    g: 80,
    b: 63,
  },
};

export default class LinearGraph extends Component {
  static propTypes = {
    value: PropTypes.number.isRequired
  }

  constructor(props) {
    super(props);
    this.state = {
    };

    this.initTween = new TimelineMax();
    this.colorTween = new TimelineMax();
    this.bar = null;
    this.label = null;
  }

  calcColor = (progress, value) => {
    let color = "#339e34";
    let colorPercentage = 0;
    if (value > .3 && value < .6 ) {
      colorPercentage = 10*value/3 - 1;
      color = "rgb(" 
        + (colorObj.green.r * (1 - colorPercentage) + colorObj.yellow.r * colorPercentage) + ", "
        + (colorObj.green.g* (1 - colorPercentage) + colorObj.yellow.g * colorPercentage) + ", "
        + (colorObj.green.b * (1 - colorPercentage) + colorObj.yellow.b * colorPercentage) + ")";
    }
    if (value >= .6 && value < .8 ) {
      colorPercentage = 5*value - 3;
      color = "rgb(" 
        + (colorObj.yellow.r * (1 - colorPercentage) + colorObj.red.r * colorPercentage) + ", "
        + (colorObj.yellow.g* (1 - colorPercentage) + colorObj.red.g * colorPercentage) + ", "
        + (colorObj.yellow.b * (1 - colorPercentage) + colorObj.red.b * colorPercentage) + ")";
    }
    if (value >= .8) {
      color = "#E9503F";
    }
    return color;
  }

  componentDidMount() {
    this.initTween.to(
      this.bar, 
      2, 
      {
        width: this.props.value + '%', 
        onUpdate: () => {
          this.colorTween.set(this.bar, { css: { backgroundColor: this.calcColor(this.initTween.progress(), this.props.value/100)}});
        },
        ease: Power2.easeOut
      }
    );
  }

  render() {
    return (
      <div id={this.props.metricKey} className="graph-container">
        <div className="graph graph-linear" ref={ div => this.bar = div } />
        <label ref={ div => this.label = div } >{this.props.value}%</label>
      </div>
    );
  }
}
