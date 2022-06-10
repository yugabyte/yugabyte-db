import React, { Component } from 'react';
import { Row, Col, ButtonGroup, DropdownButton, MenuItem }
 from 'react-bootstrap';
import './AlertDetails.scss';
import { isNonAvailable } from '../../../utils/LayoutUtils';
import { getSeverityLabel } from './AlertUtils';
import moment from 'moment';

import prometheusIcon from '../../metrics/images/prometheus-icon.svg';

const findValueforlabel = (labels, labelToFind) => {
  const label = labels.find((l) => l.name === labelToFind);
  return label ? label.value : '';
};

const getSourceName = (alertDetails) => {
  const source_name = findValueforlabel(alertDetails.labels, 'source_name');
  if (alertDetails.configurationType !== 'UNIVERSE') {
    return source_name;
  }
  const universeUUID = findValueforlabel(alertDetails.labels, 'source_uuid');

  return (
    <a target="_blank" rel="noopener noreferrer" className="universeLink" href={`/universes/${universeUUID}`}>
      {source_name}
    </a>
  );
};


const getAlertExpressionLink = (alertDetails) => {
  if (!alertDetails.alertExpressionUrl) {
    // Just in case we get alert without Prometheus URL.
    // Shouldn't happen though.
    return "";
  }
  var url = new URL(alertDetails.alertExpressionUrl);
  url.hostname = window.location.hostname;
  return (
    <a
      target="_blank"
      rel="noopener noreferrer"
      href={url.href}>
      <img
        className="prometheus-link-icon"
        alt="Alert expression graph in Prometheus"
        src={prometheusIcon}
        width="25"
      />
    </a>
  );
};

export default class AlertDetails extends Component {
  shouldComponentUpdate(nextProps) {
    const { visible, alertDetails } = this.props;

    return (
      visible !== nextProps.visible ||
      nextProps.alertDetails !== alertDetails ||
      (alertDetails != null && nextProps.alertDetails.uuid !== alertDetails.uuid)
    );
  }

  render() {
    const { customer, onHide, alertDetails, onAcknowledge } = this.props;
    const isReadOnly = isNonAvailable(
      customer.data.features, 'alert.list.actions');

    if (!alertDetails) return null;

    return (
      <div id="universe-tab-panel-pane-queries" className={'alert-details'}>
        <div className={`side-panel`}>
          <div className="side-panel__header">
            <span className="side-panel__icon--close" onClick={onHide}>
              <i className="fa fa-close" />
            </span>
            <div className="side-panel__title">Alert Details</div>
          </div>
          <div className="side-panel__content">
            <div className='panel-highlight'>
              <Row>
                <Col className="alert-label no-left-padding" xs={10} md={10} lg={10}>
                  <h6 className="alert-label-header">Source</h6>
                  <div className="alert-label-value">{getSourceName(alertDetails)}</div>
                </Col>
                <Col lg={2} md={2} xs={2}>
                {getSeverityLabel(alertDetails.severity)}
                </Col>
              </Row>
              <Row>
                <Col className="alert-label no-left-padding" xs={10} md={10} lg={10}>
                  <h6 className="alert-label-header">Name</h6>
                  <div title={alertDetails.name} className="alert-label-value">
                    {alertDetails.name}
                  </div>
                </Col>
                <Col lg={2} md={2} xs={2}>
                {getAlertExpressionLink(alertDetails)}
                </Col>
              </Row>
              <Row>
                <Col
                  lg={12}
                  className="alert-label no-left-padding no-margin-bottom"
                >
                  <h6 className="alert-label-header">DESCRIPTION</h6>
                  <div className="alert-label-message">{alertDetails.message}</div>
                </Col>
              </Row>
            </div>
            <div className='panel-highlight marginTop'>
            <Row>
              <Col lg={12} className="no-padding">
                <Row>
                  <Col className="alert-label no-padding" xs={6} md={6} lg={6}>
                    <h6 className="alert-label-header">Start</h6>
                    <div label={alertDetails.createTime} className="alert-label-value">
                      {moment(alertDetails.createTime).toString()}
                    </div>
                  </Col>
                  <Col className="alert-label no-padding" xs={6} md={6} lg={6}>
                    <h6 className="alert-label-header">End</h6>
                    <div label={alertDetails.acknowledgedTime} className="alert-label-value">
                      {alertDetails.resolvedTime ? moment(alertDetails.resolvedTime).toString() : '-'}
                    </div>
                  </Col>
                </Row>
              </Col>
            </Row>
            <Row className="marginTop">
              <Col lg={6} className="alert-label no-padding">
                <h5 className="alert-label-header">status</h5>

                <div className="alert-label-value">{alertDetails.state}</div>
              </Col>
              {alertDetails.state === 'ACTIVE' && !isReadOnly && (
                <Col lg={6} className="no-padding">
                  <ButtonGroup>
                    <DropdownButton id="alert-mark-as-button" title="Mark as">
                      <MenuItem
                        eventKey="1"
                        onClick={(e) => {
                          e.stopPropagation();
                          onAcknowledge();
                        }}
                      >
                        Acknowledged
                      </MenuItem>
                    </DropdownButton>
                  </ButtonGroup>
                </Col>
              )}
            </Row>
            </div>
            <Row className="marginTop">
              <Col lg={12} className="no-left-padding">
                <h5>History</h5>
                <div className="alert-history">
                  <ul>
                    {alertDetails.resolvedTime && (
                      <li className="alert-history-item">
                        <div className="content">Alert resolved on {alertDetails.resolvedTime}</div>
                      </li>
                    )}
                    {alertDetails.acknowledgedTime && (
                      <li className="alert-history-item">
                        <div className="content">
                          Alert acknowledged on {alertDetails.acknowledgedTime}
                        </div>
                      </li>
                    )}
                    {alertDetails.createTime && (
                      <li className="alert-history-item">
                        <div className="content">
                          <div className="timeline-flow">
                            Alert Triggered on {alertDetails.createTime}
                          </div>
                        </div>
                      </li>
                    )}
                  </ul>
                </div>
              </Col>
            </Row>
          </div>
        </div>
      </div>
    );
  }
}
