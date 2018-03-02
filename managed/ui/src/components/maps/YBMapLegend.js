// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBMapLegendItem } from '.';

export default class YBMapLegend extends Component {
  static defaultProps = {
    type: "Region"
  }

  render() {
    const {regions, type} = this.props;
    const rootRegions = regions;
    const asyncRegions = [{"name": "No read replicas added."}];
    let mapLegendItems = <span/>;
    if (type === "Universe") {
      mapLegendItems = (
        <span>
          <YBMapLegendItem regions={rootRegions} title={"Primary Data"} type="Root"/>
          <YBMapLegendItem regions={asyncRegions} title={"Read Replica"} type="Async"/>
        </span>
      );
    } else if (type === "Region") {
      mapLegendItems = (
        <span>
          <YBMapLegendItem title={"Regions & Availability Zones"} type="Region"/>
        </span>
      );
    }
    return (
      <div className="yb-map-legend">
        {this.props.title && <h4>{this.props.title}</h4>}
        {mapLegendItems}
      </div>
    );
  }
}

