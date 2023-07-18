// Copyright (c) YugaByte, Inc.

import { Component } from 'react';

import './stylesheets/YBPanelLegend.scss';

export default class YBPanelLegend extends Component {
  render() {
    const { data } = this.props;
    return (
      <div className="panel-legend">
        {data.map((item, index) => (
          // eslint-disable-next-line react/no-array-index-key
          <div key={index} className="panel-legend-item">
            <span style={{ backgroundColor: item.color }}></span>
            {item.title}
          </div>
        ))}
      </div>
    );
  }
}
