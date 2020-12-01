// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import moment from 'moment';
import _ from 'lodash';
import { isDefinedNotNull, isNonEmptyArray, isNullOrEmpty } from '../../../utils/ObjectUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { YBPanelItem } from '../../panels';
import { YBLoading } from '../../common/indicators';
import { YBResourceCount } from '../../common/descriptors';
import { MetricsPanel } from '../../metrics';

import './Replication.scss';
import {Row, Col, Alert} from 'react-bootstrap';
import YBToggle from "../../common/forms/fields/YBToggle";
import {change, Field} from "redux-form";
import {YBButton, YBNumericInput} from "../../common/forms/fields";

const GRAPH_TYPE = 'replication';
const METRIC_NAME = 'tserver_async_replication_lag_micros';
const MILLI_IN_MIN = 60000.0;
const MILLI_IN_SEC = 1000.0;
const ALERT_NAME = "Replication Lag Alert";
const ALERT_AUTO_DISMISS_MS = 4000;
const ALERT_TEMPLATE = "REPLICATION_LAG";

export default class Replication extends Component {
  constructor(props) {
    super(props);
    this.isComponentMounted = false;
    this.state = {
      graphWidth: 840,
      showNotification: false,
      alertDefinitionChanged: false,
      enableAlert: props.initialValues.enableAlert,
      threshold: props.initialValues.value
    };
    this.toggleEnableAlert = this.toggleEnableAlert.bind(this);
    this.shouldShowSaveButton = this.shouldShowSaveButton.bind(this);
    this.changeThreshold = this.changeThreshold.bind(this);
  }

  static defaultProps = {
    title: 'Replication'
  };

  static propTypes = {
    universe: PropTypes.object.isRequired
  };

  componentDidMount() {
    const { graph } = this.props;
    const { alertDefinition } = this.state;
    this.queryMetrics(graph.graphFilter);
    this.getAlertDefinition();
    if (isDefinedNotNull(alertDefinition)) {
      const value = Replication.extractValueFromQuery(alertDefinition.query);
      if (value !== null) this.updateFormField("value", value);
      this.updateFormField("enableAlert", alertDefinition.isActive);
    }

    this.isComponentMounted = true;
  }

  static getDerivedStateFromProps(props, state) {
    const { universe: { alertDefinition }} = props;
    if (getPromiseState(alertDefinition).isSuccess() && isNullOrEmpty(state.alertDefinition)) {
      state.enableAlert = alertDefinition.data.isActive;
      state.alertDefinition = alertDefinition.data;
      state.threshold = Replication.extractValueFromQuery(alertDefinition.data.query);
    } else if (getPromiseState(alertDefinition).isLoading()) {
      state.alertDefinition = null;
    }

    return state;
  }

  componentDidUpdate(prevProps, prevState) {
    const { enableAlert, threshold } = this.state;
    this.updateFormField("value", threshold);
    this.updateFormField("enableAlert", enableAlert);
  }

  componentWillUnmount() {
    const { resetMasterLeader } = this.props;
    resetMasterLeader();
    this.isComponentMounted = false;
  }

  static extractValueFromQuery(query) {
    const result = query.match(/[0-9]+(.)?[0-9]*$/g);
    if (result.length === 1) return result[0];
    else return null;
  }

  createAlertForm = (values) => {
    const { universe: { currentUniverse }} = this.props;
    const formData = {
      name: ALERT_NAME,
      isActive: values.enableAlert,
      template: ALERT_TEMPLATE,
      value: values.value
    };
    const universeUUID = currentUniverse.data.universeUUID;
    if (values.enableAlert) {
      this.props.createAlertDefinition(universeUUID, formData);
      this.setState({
        showNotification: true,
        alertDefinition: null,
        alertDefinitionChanged: false
      });
      setTimeout(() => {
        if (this.isComponentMounted) {
          this.setState({
            showNotification: false
          });
        }
      }, ALERT_AUTO_DISMISS_MS);
    }
  };

  editAlertForm = (values) => {
    const formData = {
      name: ALERT_NAME,
      isActive: values.enableAlert,
      template: ALERT_TEMPLATE,
      value: values.value
    };
    const alertDefinitionUUID = this.state.alertDefinition.uuid;
    this.props.updateAlertDefinition(alertDefinitionUUID, formData);
    this.setState({
      showNotification: true,
      alertDefinition: null,
      alertDefinitionChanged: false
    });
    setTimeout(() => {
      if (this.isComponentMounted) {
        this.setState({
          showNotification: false
        });
      }
    }, ALERT_AUTO_DISMISS_MS);
  };

  getAlertDefinition = () => {
    const { getAlertDefinition, universe: { currentUniverse }, alertDefinition } = this.props;
    const universeUUID = currentUniverse.data.universeUUID;
    if (alertDefinition === null) getAlertDefinition(universeUUID, ALERT_NAME);
  }

  queryMetrics = (graphFilter) => {
    const { universe: { currentUniverse }} = this.props;
    const universeDetails = getPromiseState(currentUniverse).isSuccess()
      ? currentUniverse.data.universeDetails
      : 'all';
    const params = {
      metrics: [METRIC_NAME],
      start: graphFilter.startMoment.format('X'),
      end: graphFilter.endMoment.format('X'),
      nodePrefix: universeDetails.nodePrefix
    };
    this.props.queryMetrics(params, GRAPH_TYPE);
  };

  updateFormField = (field, value) => {
    this.props.dispatch(change('replicationLagAlertForm', field, value));
  };

  closeAlert = () => {
    this.setState({ showNotification: false });
  };

  shouldShowSaveButton = () => {
    const { alertDefinition, alertDefinitionChanged, enableAlert } = this.state;
    return (isDefinedNotNull(alertDefinition) && alertDefinitionChanged) || enableAlert;
  }

  toggleEnableAlert(event) {
    const { alertDefinition, enableAlert, threshold } = this.state;
    const currentValue = event.target.checked;
    let hasChanged;
    if (isDefinedNotNull(alertDefinition)) {
      if (currentValue === false && alertDefinition.isActive === false) {
        hasChanged = false;
      } else {
        const existingThreshold = Replication.extractValueFromQuery(alertDefinition.query);
        hasChanged = currentValue !== alertDefinition.isActive ||
          parseFloat(existingThreshold) !== parseFloat(threshold);
      }
    } else {
      hasChanged = currentValue !== enableAlert;
    }

    this.setState({
      enableAlert: currentValue,
      alertDefinitionChanged: hasChanged
    });
    this.updateFormField("enableAlert", currentValue);
  }

  changeThreshold(value) {
    const { alertDefinition, enableAlert, threshold } = this.state;
    let hasChanged;
    if (isDefinedNotNull(alertDefinition)) {
      const existingThreshold = Replication.extractValueFromQuery(alertDefinition.query);
      hasChanged = enableAlert !== alertDefinition.isActive ||
        parseFloat(existingThreshold) !== parseFloat(value);
    } else {
      hasChanged = parseFloat(threshold) !== parseFloat(value);
    }

    this.setState({
      alertDefinitionChanged: hasChanged,
      threshold: value
    });
    this.updateFormField("value", value);
  }

  render() {
    const {
      title,
      universe: { currentUniverse },
      graph: { metrics }
    } = this.props;
    const {
      alertDefinition,
      alertDefinitionChanged,
      enableAlert,
      showNotification
    } = this.state;
    const alertDefinitionExists = isDefinedNotNull(alertDefinition);
    const submitAction = alertDefinitionExists ? this.editAlertForm : this.createAlertForm;
    const showSaveButton = alertDefinitionExists ? alertDefinitionChanged : enableAlert;

    const universeDetails = currentUniverse.data.universeDetails;
    const nodeDetails = universeDetails.nodeDetailsSet;
    if (!isNonEmptyArray(nodeDetails)) {
      return <YBLoading />;
    }
    let latestStat = null;
    let latestTimestamp = null;
    let showMetrics = false;
    let aggregatedMetrics = {};
    if (_.get(metrics, `${GRAPH_TYPE}.${METRIC_NAME}.layout.yaxis.alias`, null)) {
      // Get alias
      const metricAliases = metrics[GRAPH_TYPE][METRIC_NAME].layout.yaxis.alias;
      const committedLagName = metricAliases['async_replication_committed_lag_micros'];
      aggregatedMetrics = { ...metrics[GRAPH_TYPE][METRIC_NAME] };
      const replicationNodeMetrics = metrics[GRAPH_TYPE][METRIC_NAME].data.filter(
        (x) => x.name === committedLagName
      );
      if (replicationNodeMetrics.length) {
        // Get max-value and avg-value metric array
        let avgArr = null,
          maxArr = null;
        replicationNodeMetrics.forEach((metric) => {
          if (!avgArr && !maxArr) {
            avgArr = metric.y.map((v) => parseFloat(v) / replicationNodeMetrics.length);
            maxArr = [...metric.y];
          } else {
            metric.y.forEach((y, idx) => {
              avgArr[idx] = parseFloat(avgArr[idx]) + parseFloat(y) / replicationNodeMetrics.length;
              if (parseFloat(y) > parseFloat(maxArr[idx])) {
                maxArr[idx] = parseFloat(y);
              }
            });
          }
        });
        const firstMetricData = replicationNodeMetrics[0];
        aggregatedMetrics.data = [
          {
            ...firstMetricData,
            name: `Max ${committedLagName}`,
            y: maxArr
          },
          {
            ...firstMetricData,
            name: `Avg ${committedLagName}`,
            y: avgArr
          }
        ];
        latestStat = avgArr[avgArr.length - 1];
        latestTimestamp = firstMetricData.x[firstMetricData.x.length - 1];
        showMetrics = true;
      }
    }

    let infoBlock = <span />;
    let recentStatBlock = null;
    if (latestStat != null) {
      if (parseInt(latestStat) === 0) {
        infoBlock = (
          <div className="info success">
            <i className="fa fa-check-circle icon"></i>
            <div>
              <h4>Cluster is caught up!</h4>
            </div>
          </div>
        );
      }
      let resourceNumber = <YBResourceCount size={latestStat} kind="ms" inline={true} />;
      if (latestStat > MILLI_IN_MIN) {
        resourceNumber = (
          <YBResourceCount size={(latestStat / MILLI_IN_MIN).toFixed(4)} kind="min" inline={true} />
        );
      } else if (latestStat > MILLI_IN_SEC) {
        resourceNumber = (
          <YBResourceCount size={(latestStat / MILLI_IN_SEC).toFixed(4)} kind="s" inline={true} />
        );
      }
      recentStatBlock = (
        <div className="metric-block">
          <h3>Current Replication Lag</h3>
          {resourceNumber}
          <div className="metric-attribute">as of {moment(latestTimestamp).fromNow()}</div>
        </div>
      );
    }

    // TODO: Make graph resizeable
    return (
      <div>
        {showNotification &&
          <Alert bsStyle={'success'} onDismiss={this.closeAlert}>
            <h4>{'Success'}</h4>
            <p>{'Saved Alert Definition'}</p>
          </Alert>
        }

        <YBPanelItem
          header={
            <div className="container-title clearfix spacing-top">
              <div className="pull-left">
                <h2 className="task-list-header content-title pull-left">{title}</h2>
              </div>
              {showMetrics &&
              <div className="pull-right" style={{width: 200, "paddingRight": "15px"}}>
                <form name="replicationLagAlertForm" onSubmit={this.props.handleSubmit(submitAction)}>
                  <Row>
                    <Col>
                      <Field
                        name="enableAlert"
                        component={YBToggle}
                        infoContent={"Notify if average lag is above threshold"}
                        infoTitle="Alert"
                        onToggle={this.toggleEnableAlert}
                        label="Alert"
                      />
                    </Col>
                  </Row>
                  <Row>
                    {(enableAlert || showSaveButton)  &&
                    <YBPanelItem
                      body={
                        <div>
                          {enableAlert &&
                          <Row>
                            <Col xs={10}>
                              <Field
                                name="value"
                                component={YBNumericInput}
                                onChange={this.changeThreshold}
                                label="Threshold (ms)"
                              />
                            </Col>
                          </Row>
                          }
                          {showSaveButton &&
                          <Row>
                            <Col sm={1} className="pull-left">
                              <YBButton
                                btnClass="btn btn-orange"
                                btnText={'Save'}
                                btnType={'submit'}
                              />
                            </Col>
                          </Row>
                          }
                        </div>
                      }
                    />
                    }
                  </Row>
                </form>
              </div>
              }
            </div>
          }
          body={
            <div className="replication-content">
              {infoBlock}
              <div className="replication-content-stats">{recentStatBlock}</div>
              {!showMetrics && <div className="no-data">No data to display.</div>}
              {showMetrics && metrics[GRAPH_TYPE] && (
                <div className="graph-container">
                  <MetricsPanel
                    metricKey={METRIC_NAME}
                    metric={aggregatedMetrics}
                    className={'metrics-panel-container'}
                    width={this.state.graphWidth}
                    height={540}
                  />
                </div>
              )}
            </div>
          }
        />
      </div>
    );
  }
}
