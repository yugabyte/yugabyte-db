// Copyright (c) YugabyteDB, Inc.

import { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Field, FieldArray } from 'redux-form';
import { YBInputField, YBButton, YBSelect } from '../../../common/forms/fields';
import { isDefinedNotNull } from '../../../../utils/ObjectUtils';

class OnPremListRegionsAndZones extends Component {
  UNSAFE_componentWillMount() {
    const { fields } = this.props;
    if (fields.length === 0) {
      this.props.fields.push({});
    }
  }

  addRegionZoneTypeRow = (e) => {
    if (this.props.isEditProvider) {
      this.props.fields.push({ isBeingEdited: true });
    } else {
      this.props.fields.push({});
    }
    e.preventDefault();
  };

  removeRegionZoneTypeRow(idx) {
    this.props.fields.remove(idx);
  }

  isFieldReadOnly = (fieldIdx) => {
    const { fields, isEditProvider } = this.props;
    return (
      isEditProvider &&
      (!isDefinedNotNull(fields.get(fieldIdx).isBeingEdited) || !fields.get(fieldIdx).isBeingEdited)
    );
  };

  render() {
    const { fields } = this.props;
    const self = this;
    // TODO Replace this with API lookup to local DB City to LatLong Conversion
    const onPremRegionLocations = [
      <option value="" key={0}>
        Select
      </option>,
      <option value="-29, 148" key={1}>
        Australia
      </option>,
      <option value="-22, -43" key={2}>
        Brazil
      </option>,
      <option value="31.2, 121.5" key={3}>
        China
      </option>,
      <option value="46, 25" key={4}>
        EU East
      </option>,
      <option value="48, 3" key={5}>
        EU West
      </option>,
      <option value="36, 139" key={6}>
        Japan
      </option>,
      <option value="-43, 171" key={7}>
        New Zealand
      </option>,
      <option value="14, 101" key={8}>
        SE Asia
      </option>,
      <option value="18.4, 78.4" key={9}>
        South Asia
      </option>,
      <option value="36.8, -79" key={10}>
        US East
      </option>,
      <option value="48, -118" key={11}>
        US North
      </option>,
      <option value="28, -99" key={12}>
        US South
      </option>,
      <option value="37, -121" key={13}>
        US West
      </option>,
      <option value="55, -3" key={14}>
        EU West - UK
      </option>,
      <option value="41, 29" key={15}>
        EU East - Istanbul
      </option>,
      <option value="50.1, 8.7" key={16}>
        EU Central - Frankfurt
      </option>,
      <option value="59.3, 18" key={17}>
        EU North - Stockholm
      </option>,
      <option value="45.5, 9.2" key={18}>
        EU South - Milan
      </option>,
      <option value="26, 50.5" key={19}>
        Middle East - Bahrain
      </option>,
      <option value="-26.2, 28.04" key={20}>
        South Africa - Johannesburg
      </option>
    ];
    return (
      <div>
        {fields.map((fieldItem, fieldIdx) => {
          const isReadOnly = self.isFieldReadOnly(fieldIdx);
          return (
            // eslint-disable-next-line react/no-array-index-key
            <Row key={`region-zone-${fieldIdx}`}>
              <Col lg={1}>
                {fields.length > 1 ? (
                  <i
                    className="fa fa-minus-circle on-prem-row-delete-btn yb-orange"
                    onClick={self.removeRegionZoneTypeRow.bind(self, fieldIdx)}
                  />
                ) : null}
              </Col>
              <Col lg={3}>
                <Field
                  name={`${fieldItem}.code`}
                  component={YBInputField}
                  isReadOnly={isReadOnly}
                />
              </Col>
              <Col lg={3}>
                <Field
                  name={`${fieldItem}.location`}
                  component={YBSelect}
                  options={onPremRegionLocations}
                  readOnlySelect={isReadOnly}
                />
              </Col>
              <Col lg={5}>
                <Field
                  name={`${fieldItem}.zones`}
                  component={YBInputField}
                  isReadOnly={isReadOnly}
                />
              </Col>
            </Row>
          );
        })}
        <Row>
          <Col lg={1}>
            <i
              className="fa fa-plus-circle fa-2x on-prem-row-add-btn"
              onClick={this.addRegionZoneTypeRow}
            />
          </Col>
          <Col lg={3}>
            <a className="on-prem-add-link" onClick={this.addRegionZoneTypeRow} href="/">
              Add Region
            </a>
          </Col>
        </Row>
      </div>
    );
  }
}

export default class OnPremRegionsAndZones extends Component {
  componentDidMount() {
    document.getElementById('onprem-region-form').scrollIntoView(false);
  }

  createOnPremRegionsAndZones = (vals) => {
    this.props.setOnPremRegionsAndZones(vals);
  };

  render() {
    const { handleSubmit, switchToJsonEntry, isEditProvider } = this.props;
    return (
      <div id="onprem-region-form" className="on-prem-provider-form-container">
        <form name="onPremConfigForm" onSubmit={handleSubmit(this.createOnPremRegionsAndZones)}>
          <div className="on-prem-form-text">
            Add one or more regions, each with one or more availability zones.
          </div>
          <div className="form-field-grid">
            <Row>
              <Col lg={3} lgOffset={1}>
                Region Name
              </Col>
              <Col lg={3}>Location</Col>
              <Col lg={5}>
                Zone Names <span className="row-head-subscript">Comma Separated</span>
              </Col>
            </Row>
            <div className="on-prem-form-grid-container">
              <FieldArray
                name="regionsZonesList"
                component={OnPremListRegionsAndZones}
                isEditProvider={this.props.isEditProvider}
              />
            </div>
          </div>
          <div className="form-action-button-container">
            {isEditProvider ? (
              <YBButton
                btnText={'Cancel'}
                btnClass={'btn btn-default save-btn cancel-btn'}
                onClick={this.props.cancelEdit}
              />
            ) : (
              <span />
            )}
            {switchToJsonEntry}
            <YBButton btnText={'Finish'} btnType={'submit'} btnClass={'btn btn-default save-btn'} />
            <YBButton
              btnText={'Previous'}
              btnClass={'btn btn-default back-btn'}
              onClick={this.props.prevPage}
            />
          </div>
        </form>
      </div>
    );
  }
}
