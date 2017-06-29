// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { StepProgressBar } from '../../common/indicators';
import { isValidObject } from '../../../utils/ObjectUtils';
import { Row, Col } from 'react-bootstrap';
import './TaskProgressStepBar.scss'

export default class TaskProgressStepBar extends Component {
  static propTypes = {
    progressData: PropTypes.object.isRequired
  }

  render() {
    const { progressData: { details }} = this.props;
    if (!isValidObject(details)) {
      return <span />;
    }

    let currentTaskDetail = details.taskDetails
      .filter((taskDetail) => taskDetail.state === "Running")
      .map((taskDetail, idx) =>
        <div key={`taskdetail-{idx}`}>
          <h4>Current Task: {taskDetail.title}</h4>
          <Row className="description-text-container">
          <Col lg={8} className="description-text">
            {taskDetail.description}
          </Col>
          </Row>
        </div>);

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
