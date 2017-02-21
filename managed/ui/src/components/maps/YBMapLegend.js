// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBMapLegendItem } from '.';

export default class YBMapLegend extends Component {
  render() {
    const {regions} = this.props;
    var rootRegions = regions;
    var asyncRegions = [{"name": "No Async Replicas Added."}];
    var cacheRegions = [{"name": "No Caches Added."}];
    return (
      <div className="map-legend-container">
        {this.props.title && <h4>{this.props.title}</h4>}
        <YBMapLegendItem regions={rootRegions} title={"Root Data"} type="Root"/>
        <YBMapLegendItem regions={asyncRegions} title={"Async Replica"} type="Async"/>
        <YBMapLegendItem regions={cacheRegions} title={"Remote Cache"} type="Cache"/>
      </div>
    )
  }
}
