// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import { LinearGraph } from '../';
import {isNonEmptyArray} from 'utils/ObjectUtils';
import { YBResourceCount } from 'components/common/descriptors';
import './MetricsPanel.scss';

export default class DiskUsagePanel extends Component {
  static propTypes = {
  }

  render() {
    const space = {
      free: undefined,
      used: undefined,
      size: undefined
    };

    if (isNonEmptyArray(this.props.metric.data)) {
      const freeArray = this.props.metric.data.find((item)=>item.name==="free").y;
      const sizeArray = this.props.metric.data.find((item)=>item.name==="size").y;
      const reducer = arr => arr.reduce( ( p, c ) => parseFloat(p) + parseFloat(c), 0 ) / arr.length;
      space.free = reducer(freeArray);
      space.size = reducer(sizeArray);
      space.used = space.size - space.free;
    }
    const value = space.size ? Math.round(space.used * 1000 / space.size) / 10 : 0;
    return (
      <div className="metrics-padded-panel disk-usage-panel">
        { isNaN(space.size) 
          ? 
            <Fragment>
              <YBResourceCount size={"No Data"} />
              <span className="gray-text metric-subtitle">{"Data is unavailable"} </span>
              <LinearGraph value={0}/>
            </Fragment>
          : 
            <Fragment>
              <YBResourceCount size={Math.round(space.used * 10)/10} unit="GB used" />
              <span className="gray-text metric-subtitle">{Math.round(space.free)} GB free out of {Math.round(space.size)} </span>
              <LinearGraph value={value} base="used"/>
            </Fragment>
        }
      </div>
    );
  }
}
