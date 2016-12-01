// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Grid, Row, Col } from 'react-bootstrap';
import { ProgressGauge, ProgressList } from '../common/indicators';
import { YBPanelItem } from '../panels';

export default class TaskProgressWidget extends Component {
  static propTypes = {
    progressData: PropTypes.object.isRequired
  }

  render() {
    const { details: { taskDetails }, percent } = this.props.progressData;
    const progressDetailsMap = taskDetails.map(function(taskInfo, idx) {
      return { name: taskInfo.title, type: taskInfo.state }
    });

    return (
      <YBPanelItem name="Task Progress">
        <Grid id="taskProgressPanel">
          <Row>
            <Col md={8}>
              <ProgressList items={progressDetailsMap} />
            </Col>
            <Col md={4}>
              <ProgressGauge maxValue={100} value={percent} title={"Overall Progress"} animate={true} />
            </Col>
          </Row>
        </Grid>
      </YBPanelItem>
    );
  }
}
