// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Grid, Row, Col } from 'react-bootstrap';
import ProgressGauge from './ProgressGauge';
import ProgressList from './ProgressList';
import YBPanelItem from './YBPanelItem';

export default class TaskProgessPanel extends Component {
  static contextTypes = {
    router: PropTypes.object
  }

  componentDidMount() {
    const { universe: { universeTasks } } = this.props;
    if (!this.props.universe.loading &&
        universeTasks !== undefined &&
        universeTasks.length > 0) {
      // TODO, currently we only show on of the tasks, we need to
      // implement a way to show all the tasks against a universe
      this.props.fetchTaskProgress(universeTasks[0].id);
    }
  }

  componentWillUnmount() {
    this.props.resetTaskProgress();
  }

  render() {
    const { universe: { universeTasks }, tasks: { taskProgressData} } = this.props;
    if (this.props.universe.loading || universeTasks === undefined ||
        this.props.tasks.loading || taskProgressData.length === 0) {
      return <div className="container">Loading...</div>;
    } else if (taskProgressData.status === "Success" ||
               taskProgressData.status === "Failure") {
      // TODO: Better handle/display the success/failure case
      return <span />;
    }
    const { details: { taskDetails }, percent } = taskProgressData
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
