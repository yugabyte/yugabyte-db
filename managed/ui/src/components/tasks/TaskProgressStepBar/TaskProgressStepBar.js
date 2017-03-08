// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { StepProgressBar } from '../../common/indicators';
import { YBPanelItem } from '../../panels';
import {Row, Col} from 'react-bootstrap';
import { ProgressList } from '../../common/indicators';
import './TaskProgressStepBar.scss'

export default class TaskProgressStepBar extends Component {
  static propTypes = {
    progressData: PropTypes.object.isRequired
  }

  render() {
    const { progressData: {percent, details : {taskDetails}}, currentOperation } = this.props;
    var currentTaskDetail = <span/>;
    for (var idx = 0; idx < taskDetails.length; idx ++) {
      if (taskDetails[idx].state === "Running") {
        currentTaskDetail =
          <div>
            <h4>Current Task: {taskDetails[idx].title}</h4>
            <Row className="description-text-container">
            <Col lg={8} className="description-text">
              {taskDetails[idx].description}
            </Col>
            </Row>
          </div>
      }
    }

    return (
      <Row>
        <Col lg={8}>
        <div className="progress-bar-container">
          <h4>Task Progress</h4>
          <StepProgressBar progressData={this.props.progressData} />
        </div>
        </Col>
        <Col lg={4}>
          {currentTaskDetail}
        </Col>
      </Row>
    );
  }
}
