// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';

import { Power2 } from "gsap/all";
import TweenMax from 'gsap/TweenMax';
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


// Graph componenent's value must be a float number between {0,1} interval.
// Enforce all calculations outside this component.
export default class Graph extends Component {
  static propTypes = {
    value: PropTypes.number.isRequired,
    unit: PropTypes.string,
    type: PropTypes.oneOf(['linear', 'semicircle']), 
  }

  static defaultProps = {
    type: "linear"
  };

  constructor(props) {
    super(props);
    this.state = {
    };

    //this.initTween = new TweenMax();
    this.values = {
      width: 0,
      color: "#339e34"
    };
    this.bar = null;
    this.label = null;
    this.counter = { var: 0 };
  }

  calcColor = (value) => {
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
    const counter = { var: 0 };
    this.initTween = TweenMax.to(
      counter, 
      2, 
      {
        var: 1,
        onUpdate: () => { 
          this.values = {
            width: Math.round(this.props.value * counter.var * 1000) / 10,
            color: this.calcColor(counter.var * this.props.value),
          };
          if (this.props.type === "semicircle") {
            TweenMax.set(this.bar, { css: { stroke: this.values.color, strokeDashoffset: 100 - this.values.width}});
          }

          if (this.props.type === "linear") {
            TweenMax.set(this.bar, { css: { backgroundColor: this.values.color, width: this.values.width+"%"}});
          }
        },
        ease: Power2.easeOut,
        delay: 2
      }
    );
  }

  getGraphByType = (type) => {
    if (type === "semicircle") {
      return (<svg x="0px" y="0px" className={`graph-body`} viewBox="0 0 79.2 46.1">
        <path className="graph-base" d="M7.8,38.8C7.8,21.2,22.1,7,39.6,7s31.8,14.2,31.8,31.8"/>
        <path className="graph-bar" d="M7.8,38.8C7.8,21.2,22.1,7,39.6,7s31.8,14.2,31.8,31.8" ref={ elem => this.bar = elem } />
      </svg>);
    }

    if (type === "linear") {
      return (<div className={`graph-body`} ref={ elem => this.bar = elem } />);
    }

    return null;
  }

  render() {
    const value = this.props.unit === "percent" || this.props.unit === "%" ?  Math.round(this.props.value * 1000) / 10 : this.props.value;
    const unit = this.props.unit 
      ? (this.props.unit === "percent" || this.props.unit === "%" ? "%" : " " + this.props.unit)
      : null;
    return (
      <div id={this.props.metricKey} className={`graph-container graph-${this.props.type || "linear"}`}>
        {this.getGraphByType(this.props.type)}

        {unit && <label ref={ div => this.label = div } >{value}{unit}</label>}
      </div>
    );
  }
}
