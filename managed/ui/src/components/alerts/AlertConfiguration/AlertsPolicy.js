import { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Field } from 'redux-form';
import { YBInputField, YBSelect } from '../../common/forms/fields';
import '../CreateAlerts.scss';

const required = (value) => {
  if (value === undefined || value === null || value === '') {
    return 'This field is required.';
  }
  return undefined;
};

const MAX_SEVERITY_ALLOWED = 2;

const detectDuplicateSeverity = (_, values) => {
  const distinctValue = new Set(
    values.ALERT_METRICS_CONDITION_POLICY.map((field) => field._SEVERITY)
  );
  return distinctValue.size !== values.ALERT_METRICS_CONDITION_POLICY.length
    ? 'Duplicate severity is not allowed'
    : undefined;
};

export class AlertsPolicy extends Component {
  /**
   * Constant option for severity types
   * TODO: Source and values of actual list may differ.
   */
  severityTypes = [
    <option key={0}>Select Severity</option>,
    <option key={1} value="SEVERE">
      Severe
    </option>,
    <option key={2} value="WARNING">
      Warning
    </option>
  ];

  /**
   * Constant option for eqality type of policy.
   * TODO: Source and values of actual list may differ.
   */
  conditionTypes = [
    <option key={0}>Select Condition</option>,
    <option key={1} value="GREATER_THAN">
      Greater Than
    </option>,
    <option key={2} value="LESS_THAN">
      Less Than
    </option>,
    <option key={3} value="NOT_EQUAL">
      Not Equal
    </option>
  ];

  /**
   * Add a new row in field array.
   * @param {Event} e
   */
  addRow = (e) => {
    const metric = this.props.currentMetric;
    this.props.fields.push({
      _CONDITION: metric.thresholds[Object.keys(metric.thresholds)[0]].condition
    });
    e.preventDefault();
  };

  /**
   * Remove the element from array based on index.
   * @param {Number} instanceTypeIdx
   */
  removeRow = (instanceTypeIdx) => {
    this.props.fields.remove(instanceTypeIdx);
  };

  render() {
    const { fields, currentMetric, isReadOnly } = this.props;

    return (
      <div className="condition-row-container">
        <Row className="marginBottom">
          <Col lg={2}>Severity</Col>
          <Col lg={2}>Condition</Col>
          <Col lg={5}>Threshold</Col>
        </Row>
        {fields.map((instanceTypeItem, instanceTypeIdx) => (
          // eslint-disable-next-line react/no-array-index-key
          <Row key={instanceTypeIdx}>
            <Col lg={2}>
              <Field
                name={`${instanceTypeItem}_SEVERITY`}
                component={YBSelect}
                insetError={true}
                readOnlySelect={isReadOnly}
                validate={[required, detectDuplicateSeverity]}
                options={this.severityTypes}
              />
            </Col>
            <Col lg={2}>
              <Field
                name={`${instanceTypeItem}_CONDITION`}
                component={YBSelect}
                insetError={true}
                readOnlySelect={isReadOnly || currentMetric?.thresholdConditionReadOnly}
                validate={required}
                options={this.conditionTypes}
              />
            </Col>
            <Col lg={1}>
              <Field
                name={`${instanceTypeItem}_THRESHOLD`}
                component={YBInputField}
                isReadOnly={isReadOnly || currentMetric?.thresholdReadOnly}
                validate={required}
              />
            </Col>
            <Col lg={1}>
              <div className="flex-container">
                <p className="percent-text">{currentMetric?.thresholdUnitName}</p>
                {!isReadOnly && fields.length > 1 && !currentMetric?.thresholdReadOnly ? (
                  <i
                    className="fa fa-remove on-prem-row-delete-btn"
                    onClick={() => this.removeRow(instanceTypeIdx)}
                  />
                ) : null}
              </div>
            </Col>
          </Row>
        ))}
        {!isReadOnly &&
        currentMetric?.name &&
        fields.length < MAX_SEVERITY_ALLOWED &&
        !currentMetric.thresholdReadOnly ? (
          <Row>
            <Col lg={2}>
              <a href="# " className="on-prem-add-link" onClick={this.addRow}>
                <i className="fa fa-plus-circle fa-2x on-prem-row-add-btn" onClick={this.addRow} />
                Add Severity
              </a>
            </Col>
          </Row>
        ) : null}
      </div>
    );
  }
}
