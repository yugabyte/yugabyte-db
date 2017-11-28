// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { DescriptionItem } from './';

import './stylesheets/YBStatsBlock.scss';

export default class YBStatsBlock extends Component {
  render() {
    const {value, label} = this.props;
    return (
      <div className="tile_stats_count text-center">
        <DescriptionItem>
          <div className="count">
            {value}
          </div>
        </DescriptionItem>
        <span className="count_top">
          {label}
        </span>
      </div>
    );
  }
}
