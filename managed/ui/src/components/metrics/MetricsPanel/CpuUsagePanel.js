// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import { Graph } from '../';
import {isNonEmptyArray} from 'utils/ObjectUtils';
import { YBResourceCount } from 'components/common/descriptors';
import './MetricsPanel.scss';

export default class CpuUsagePanel extends Component {
  static propTypes = {
  }

  render() {
    const usage = {
      system: undefined,
      user: undefined
    };

    try {
      if (isNonEmptyArray(this.props.metric.data)) {
        usage.system = parseFloat(this.props.metric.data.find((item)=>item.name==="system").y[0]);
        usage.user = parseFloat(this.props.metric.data.find((item)=>item.name==="user").y[0]);
      }    
    } catch (err) {
      console.log("CPU metric processing failed with: "+err);
    }
    const value = usage.system ? Math.round((usage.system + usage.user) * 10 ) / 1000 : 0;
    return (
      <div className="metrics-padded-panel cpu-usage-panel">
        { isNaN(usage.system) 
          ? 
            <Fragment>
              <Graph type={"semicircle"} value={0} />
              <div className="centered text-light text-lightgray">No Data</div>
            </Fragment>
          : 
            <Fragment>
              <Graph type={"semicircle"} value={value} />
              <YBResourceCount size={value*100} kind="% used" inline={true} />
            </Fragment>
        }
      </div>
    );
  }
}
