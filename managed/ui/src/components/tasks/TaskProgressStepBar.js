// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { StepProgressBar } from '../common/indicators';
import { YBPanelItem } from '../panels';

export default class TaskProgressStepBar extends Component {
  static propTypes = {
    progressData: PropTypes.object.isRequired
  }

  render() {
    return (
      <YBPanelItem name="Task Progress">
        <div className="progress-bar-container">
          <StepProgressBar progressData={this.props.progressData} />
        </div>
      </YBPanelItem>
    );
  }
}
