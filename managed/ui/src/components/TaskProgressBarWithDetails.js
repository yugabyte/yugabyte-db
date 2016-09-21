// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import {  ProgressBar } from 'react-bootstrap';
import ProgressList from './ProgressList';
import { Row, Col } from 'react-bootstrap';

export default class TaskProgressBarWithDetails extends Component {
  static propTypes = {
    progressData: PropTypes.object.isRequired
  }


  render() {
    const { progressData: {percent, details : {taskDetails}}, currentOperation } = this.props;
    const progressDetailsMap = taskDetails.map(function(taskInfo, idx) {
      return { name: taskInfo.title, type: taskInfo.state }
    });

    return (
      <Row className="yb-progress-bar-container">
        <Col lg={5} className="yb-left">
          <ProgressBar now={percent} />
          <div><i className="fa fa-clock-o" aria-hidden="true"> {currentOperation} </i></div>
          <div>{percent}% completed</div>
        </Col>
        <Col lg={7}>
          <ProgressList items={progressDetailsMap} />
        </Col>
      </Row>
    );
  }
}
