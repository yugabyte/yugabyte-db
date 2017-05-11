// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBMapLegendItem } from '.';

export default class YBMapLegend extends Component {
  render() {
    const {regions} = this.props;
    var rootRegions = regions;
    var asyncRegions = [{"name": "No async replicas added."}];
    var cacheRegions = [{"name": "No caches added."}];
    return (
      <div className="yb-map-legend">
        {this.props.title && <h4>{this.props.title}</h4>}
        <YBMapLegendItem regions={rootRegions} title={"Primary Data"} type="Root"/>
        <YBMapLegendItem regions={asyncRegions} title={"Async Replica"} type="Async"/>
        <YBMapLegendItem regions={cacheRegions} title={"Remote Cache"} type="Cache"/>
      </div>
    )
  }
}
