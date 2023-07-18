// Copyright (c) YugaByte, Inc.

import { Component, Fragment } from 'react';
import { Row, Col } from 'react-bootstrap';
import {
  YBFormSelect,
  YBFormInput,
  YBAddRowButton,
  YBModal,
  YBFormDropZone,
  YBTextInputWithLabel
} from '../../../common/forms/fields';
import YBInfoTip from '../../../common/descriptors/YBInfoTip';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { connect as formikConnect, getIn, Field, ErrorMessage, FieldArray } from 'formik';
import { REGION_METADATA, REGION_DICT } from '../../../../config';
import _ from 'lodash';

const ISSUER_TYPE = [
  {
    label: 'None',
    value: 'NONE'
  },
  {
    label: 'Issuer',
    value: 'ISSUER'
  },
  {
    label: 'Cluster Issuer',
    value: 'CLUSTER'
  }
];

class AddRegionList extends Component {
  constructor(props) {
    super(props);
    this.state = {
      regionIndex: undefined,
      isEditingRegion: false,
      showZoneForm: true
    };
  }

  addRegionModal = (arrayPush) => {
    const { showModal, formik } = this.props;
    const newRegionIndex = formik.values.regionList.length;
    arrayPush({
      regionCode: '',
      isValid: false,
      zoneList: [
        {
          zoneLabel: '',
          storageClasses: '',
          namespace: '',
          kubeDomain: '',
          zoneOverrides: '',
          zonePodAddressTemplate: '',
          issuerType: 'NONE'
        }
      ]
    });
    showModal('addRegionConfig');
    this.setState({
      regionIndex: newRegionIndex,
      showZoneForm: true,
      isEditingRegion: false
    });
  };

  hideModal = (arrayHelpers) => {
    const { regionIndex, isEditingRegion, showZoneForm } = this.state;
    const { formik } = this.props;
    const currentRegion = formik.values.regionList[regionIndex];
    const zoneArr = currentRegion.zoneList;

    // Editing region and the zone form is open. Delete any info entered in the form
    if (isEditingRegion && showZoneForm) {
      const removeLastArr = zoneArr.slice(0, zoneArr.length - 1);

      // If we remove the last zone, don't allow 0 zone region to be added
      if (removeLastArr.length) {
        arrayHelpers.replace(regionIndex, {
          ...currentRegion,
          zoneList: zoneArr.slice(0, zoneArr.length - 1)
        });
      } else {
        arrayHelpers.remove(regionIndex);
      }
    } else if (!isEditingRegion) {
      arrayHelpers.remove(regionIndex);
    }
    this.props.closeModal();
    this.setState({
      regionIndex: undefined,
      isEditingRegion: false,
      showZoneForm: true
    });
  };

  resetPopup = (arrayRemove) => {
    const { closeModal } = this.props;
    const { regionIndex } = this.state;
    arrayRemove(regionIndex);
    closeModal();
    this.setState({
      regionIndex: undefined,
      isEditingRegion: true
    });
  };

  zoneConfigFormatter = (cell, row) => {
    if (row.zoneKubeConfig?.name) {
      return row.zoneKubeConfig.name;
    } else {
      return null;
    }
  };

  // eslint-disable-next-line react/display-name
  actionFormatter = (zoneArrayHelpers) => (cell, row, formatExtraData, rowIdx) => {
    const { showZoneForm, regionIndex } = this.state;
    const vals = zoneArrayHelpers.form.values;
    if (showZoneForm) {
      return (
        <i
          className="fa fa-times fa-fw delete-row-btn"
          onClick={() => zoneArrayHelpers.remove(rowIdx)}
        />
      );
    } else {
      return (
        <i
          className="fa fa-times fa-fw delete-row-btn"
          onClick={() => {
            if (rowIdx === 0 && vals.regionList[regionIndex].zoneList.length === 1) {
              zoneArrayHelpers.replace(rowIdx, {
                zoneLabel: '',
                storageClasses: '',
                namespace: '',
                kubeDomain: '',
                zoneOverrides: '',
                zonePodAddressTemplate: '',
                issuerType: 'NONE'
              });
              this.setState({
                showZoneForm: true
              });
            } else {
              zoneArrayHelpers.remove(rowIdx);
            }
          }}
        />
      );
    }
  };

  addZone = (arrayPush) => {
    arrayPush({
      zoneLabel: '',
      storageClasses: '',
      namespace: '',
      kubeDomain: '',
      zoneOverrides: '',
      zonePodAddressTemplate: '',
      issuerType: 'NONE'
    });
    if (!this.state.showZoneForm) {
      this.setState({
        showZoneForm: true
      });
    }
  };

  render() {
    const { modal, showModal, closeModal, formik } = this.props;
    const { regionIndex, showZoneForm } = this.state;
    const { regionList } = formik.values;
    const currentRegion = regionList[regionIndex];
    const zoneIndex = currentRegion?.zoneList?.length ? currentRegion.zoneList.length - 1 : 0;
    const nonEditingZones = currentRegion?.zoneList
      ? currentRegion.zoneList?.slice(0, zoneIndex)
      : [];
    const regionOptions = REGION_METADATA.map((region) => ({
      value: region.code,
      label: region.name
    }));
    regionList.forEach((r) => {
      if (r.regionCode.value in REGION_DICT) {
        const regionsArrIndex = REGION_DICT[r.regionCode.value].index;
        regionOptions[regionsArrIndex].disabled = true;
      }
    });
    const touchedZones = _.get(formik.touched, `regionList[${regionIndex}].zoneList[${zoneIndex}]`);
    const errorZones = _.get(formik.errors, `regionList[${regionIndex}].zoneList[${zoneIndex}]`);
    const disableRegionSubmit =
      !!formik.errors.regionList &&
      (_.get(formik.errors, `regionList[${regionIndex}].regionCode`) ||
        /**
         * Logic Table for Disabling Submit button
         * -----------------------------
         * Touched   |  T  | F | F | * |
         * ----------+-----+---+---+----
         * Error     |  T  | T | T | F |
         * ----------+-----+---+---+----
         * Zones > 1 | T/F | T | F | * |
         * ----------+-----+---+---+----
         * Disabled? |  T  | F | T | F |
         * -----------------------------
         *
         * Touched = if any zone form fields have been clicked on
         * Error = if any fields have errors as defined by Yup validation
         * Zones > 1 = number of zones (including blank zones), clicking on 'add zone' creates
         *  a new zone even though the user has not filled out the form
         * Disabled? row determines whether 'Add Region' button should be disabled
         */
        (!touchedZones && errorZones && currentRegion.zoneList.length <= 1) ||
        (touchedZones && errorZones));
    const displayedRegions = regionList.filter((v) => v.isValid);

    const isZoneValid = (zoneInfo) => {
      if (!zoneInfo.zoneLabel) return false;

      if (['CLUSTER', 'ISSUER'].includes(zoneInfo.issuerType) && !zoneInfo.issuerName) return false;

      if (['ISSUER'].includes(zoneInfo.issuerType) && !zoneInfo.namespace) return false;

      return true;
    };

    return (
      <Row>
        <Col lg={7}>
          <h4 className="regions-form-title">Regions</h4>
          <FieldArray
            name="regionList"
            render={(arrayHelpers) => (
              <Fragment>
                {modal.showModal && modal.visibleModal === 'addRegionConfig' && (
                  <YBModal
                    formName={'addRegionConfig'}
                    visible={true}
                    submitLabel={'Add Region'}
                    showCancelButton={true}
                    disableSubmit={disableRegionSubmit}
                    title="Add new region"
                    className="add-zone-form"
                    onHide={() => this.hideModal(arrayHelpers)}
                    onFormSubmit={() => {
                      if (!currentRegion.zoneList[currentRegion.zoneList.length - 1].zoneLabel) {
                        regionList[regionIndex].zoneList.pop();
                      }
                      arrayHelpers.replace(regionIndex, {
                        ...regionList[regionIndex],
                        isValid: true
                      });
                      closeModal();
                    }}
                  >
                    <div className="form-field-grid">
                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Region</div>
                        </Col>
                        <Col lg={7}>
                          <Field
                            name={`regionList[${regionIndex}].regionCode`}
                            component={YBFormSelect}
                            options={regionOptions}
                          />
                        </Col>
                      </Row>
                    </div>

                    {currentRegion?.regionCode && (
                      <FieldArray
                        name={`regionList[${regionIndex}].zoneList`}
                        render={(zoneArrayHelpers) => {
                          const { zoneList } = zoneArrayHelpers.form.values.regionList[regionIndex];
                          const zoneInfo = zoneList[zoneIndex];
                          return (
                            <Fragment>
                              <h4>Specify zones</h4>
                              <div className="divider"></div>
                              <div className="form-field-grid">
                                {(!showZoneForm || currentRegion.zoneList.length > 1) && (
                                  <BootstrapTable
                                    data={showZoneForm ? nonEditingZones : currentRegion.zoneList}
                                    trClassName="zone-bs-table"
                                  >
                                    <TableHeaderColumn
                                      dataField="zoneLabel"
                                      isKey={true}
                                      columnClassName="no-border"
                                      className="no-border"
                                      dataAlign="left"
                                    >
                                      LABEL
                                    </TableHeaderColumn>
                                    <TableHeaderColumn
                                      dataField="storageClasses"
                                      columnClassName="no-border name-column"
                                      className="no-border"
                                    >
                                      STORAGE CLASS
                                    </TableHeaderColumn>
                                    <TableHeaderColumn
                                      dataField="namespace"
                                      columnClassName="no-border name-column"
                                      className="no-border"
                                    >
                                      NAMESPACE
                                    </TableHeaderColumn>
                                    <TableHeaderColumn
                                      dataField="kubeDomain"
                                      columnClassName="no-border name-column"
                                      className="no-border"
                                    >
                                      DNS DOMAIN
                                    </TableHeaderColumn>
                                    <TableHeaderColumn
                                      dataField="zoneKubeConfig.name"
                                      dataFormat={this.zoneConfigFormatter}
                                      columnClassName="no-border name-column"
                                      className="no-border"
                                    >
                                      ZONE CONFIG
                                    </TableHeaderColumn>
                                    <TableHeaderColumn
                                      dataField="actions"
                                      dataFormat={this.actionFormatter(zoneArrayHelpers)}
                                      width="40px"
                                    />
                                  </BootstrapTable>
                                )}
                                {showZoneForm && (
                                  <div className="zone-form-wrapper">
                                    <Row className="config-provider-row">
                                      <Col lg={3}>
                                        <div className="form-item-custom-label">Zone</div>
                                      </Col>
                                      <Col lg={7}>
                                        <Field
                                          name={`regionList[${regionIndex}].zoneList[${zoneIndex}].zoneLabel`}
                                          placeholder="Zone Label"
                                          component={YBFormInput}
                                          className={'kube-provider-input-field'}
                                        />
                                        {typeof getIn(
                                          formik.errors,
                                          `regionList[${regionIndex}].zoneList`
                                        ) === 'string' ? (
                                          <div className="input-feedback">
                                            <ErrorMessage
                                              name={`regionList[${regionIndex}].zoneList`}
                                            />
                                          </div>
                                        ) : null}
                                      </Col>
                                    </Row>
                                    <Row className="config-provider-row">
                                      <Col lg={3}>
                                        <div className="form-item-custom-label">
                                          Storage Classes
                                        </div>
                                      </Col>
                                      <Col lg={7}>
                                        <Field
                                          name={`regionList[${regionIndex}].zoneList[${zoneIndex}].storageClasses`}
                                          placeholder="Storage Class for this Zone"
                                          component={YBFormInput}
                                          className={'kube-provider-input-field'}
                                        />
                                      </Col>
                                      <Col lg={1} className="config-zone-tooltip">
                                        <YBInfoTip
                                          title="Storage Classes"
                                          content={
                                            "Default is '' (not specified). Will attempt to use Kubernetes cluster default."
                                          }
                                        />
                                      </Col>
                                    </Row>
                                    <Row className="config-provider-row">
                                      <Col lg={3}>
                                        <div className="form-item-custom-label">Namespace</div>
                                      </Col>
                                      <Col lg={7}>
                                        <Field
                                          name={`regionList[${regionIndex}].zoneList[${zoneIndex}].namespace`}
                                          placeholder="Namespace for this Zone"
                                          component={YBFormInput}
                                          className={'kube-provider-input-field'}
                                        />
                                      </Col>
                                      <Col lg={1} className="config-zone-tooltip">
                                        <YBInfoTip
                                          title="Namespace"
                                          content={
                                            'An existing namespace into which pods in this zone will be deployed.'
                                          }
                                        />
                                      </Col>
                                    </Row>
                                    <Row className="config-provider-row">
                                      <Col lg={3}>
                                        <div className="form-item-custom-label">
                                          Cluster DNS Domain
                                        </div>
                                      </Col>
                                      <Col lg={7}>
                                        <Field
                                          name={`regionList[${regionIndex}].zoneList[${zoneIndex}].kubeDomain`}
                                          placeholder="Cluster DNS Domain for this Zone"
                                          component={YBFormInput}
                                          className={'kube-provider-input-field'}
                                        />
                                      </Col>
                                      <Col lg={1} className="config-zone-tooltip">
                                        <YBInfoTip
                                          title="Cluster DNS Domain"
                                          content={
                                            'The dns domain name used in the Kubernetes cluster (default "cluster.local")'
                                          }
                                        />
                                      </Col>
                                    </Row>
                                    <Row className="config-provider-row">
                                      <Col lg={3}>
                                        <div className="form-item-custom-label">Kube Config</div>
                                      </Col>
                                      <Col lg={7}>
                                        <Field
                                          name={`regionList[${regionIndex}].zoneList[${zoneIndex}].zoneKubeConfig`}
                                          component={YBFormDropZone}
                                          title={'Upload Kube Config file'}
                                        />
                                      </Col>
                                    </Row>

                                    <Row className="config-provider-row">
                                      <Col lg={3}>
                                        <div className="form-item-custom-label">Overrides</div>
                                      </Col>
                                      <Col lg={7}>
                                        <Field
                                          name={`regionList[${regionIndex}].zoneList[${zoneIndex}].zoneOverrides`}
                                          placeholder="Optional Zone Config Overrides"
                                          component={YBFormInput}
                                          componentClass="textarea"
                                          className={'kube-provider-input-field'}
                                        />
                                      </Col>
                                    </Row>

                                    <Row className="config-provider-row">
                                      <Col lg={3}>
                                        <div className="form-item-custom-label">
                                          Pod Address Template
                                        </div>
                                      </Col>
                                      <Col lg={7}>
                                        <Field
                                          name={`regionList[${regionIndex}].zoneList[${zoneIndex}].zonePodAddressTemplate`}
                                          placeholder="Pod address template for this Zone"
                                          component={YBFormInput}
                                          className={'kube-provider-input-field'}
                                        />
                                      </Col>
                                      <Col lg={1} className="config-zone-tooltip">
                                        <YBInfoTip
                                          title="Pod Address Template (optional)"
                                          content={
                                            'Use this setting for multi-cluster setups like Istio or MCS to generate the ' +
                                            'correct pod addresses. Supported fields are {pod_name}, {service_name}, ' +
                                            '{namespace}, and {cluster_domain}.'
                                          }
                                        />
                                      </Col>
                                    </Row>

                                    <h5>cert-manager</h5>

                                    <Row className="config-provider-row issuer-row">
                                      <Col lg={3}>
                                        <div className="form-item-custom-label">Issuer Type</div>
                                      </Col>
                                      <Col lg={7}>
                                        <Row className="issuer-radio-field-c">
                                          {ISSUER_TYPE.map(({ label, value }) => (
                                            <Col
                                              key={`issuer-type-${value}`}
                                              className="issuer-radio-field"
                                            >
                                              <Field
                                                name={`regionList[${regionIndex}].zoneList[${zoneIndex}].issuerType`}
                                                type="radio"
                                                component="input"
                                                value={value}
                                                checked={value === zoneInfo.issuerType}
                                              />
                                              &nbsp;&nbsp;{label}
                                            </Col>
                                          ))}
                                        </Row>
                                      </Col>
                                    </Row>

                                    {zoneInfo.issuerType !== 'NONE' && (
                                      <Row className="config-provider-row">
                                        <Col lg={3}>
                                          <div className="form-item-custom-label">
                                            {zoneInfo.issuerType === 'CLUSTER'
                                              ? 'Cluster Issuer Name'
                                              : 'Issuer Name'}
                                          </div>
                                        </Col>
                                        <Col lg={7}>
                                          <Field
                                            name={`regionList[${regionIndex}].zoneList[${zoneIndex}].issuerName`}
                                            component={YBFormInput}
                                            className={'kube-provider-input-field'}
                                          />
                                        </Col>
                                      </Row>
                                    )}
                                  </div>
                                )}

                                <YBAddRowButton
                                  btnText="Add zone"
                                  className={'btn'}
                                  onClick={() => {
                                    //touch fields so that validation fires
                                    zoneArrayHelpers.form.setFieldTouched(
                                      `regionList[${regionIndex}].zoneList[${zoneIndex}].zoneLabel`
                                    );
                                    zoneArrayHelpers.form.setFieldTouched(
                                      `regionList[${regionIndex}].zoneList[${zoneIndex}].issuerName`
                                    );
                                    zoneArrayHelpers.form.setFieldTouched(
                                      `regionList[${regionIndex}].zoneList[${zoneIndex}].namespace`
                                    );
                                    //validate form
                                    zoneArrayHelpers.form.validateForm();

                                    (isZoneValid(zoneInfo) || !showZoneForm) &&
                                      this.addZone(zoneArrayHelpers.push);
                                  }}
                                />
                              </div>
                            </Fragment>
                          );
                        }}
                      />
                    )}
                  </YBModal>
                )}
                <ul className="config-region-list">
                  {displayedRegions.length > 0 && (
                    <li className="header-row">
                      <Fragment>
                        <div>Name</div>
                        <div>Zones</div>
                        <div>{/* empty block for spacing */}</div>
                      </Fragment>
                    </li>
                  )}
                  {displayedRegions.map((region, index) => (
                    <li
                      // eslint-disable-next-line react/no-array-index-key
                      key={index}
                      onClick={() => {
                        // Regions edit popup handler
                        showModal('addRegionConfig');
                        const nonZeroZones = region.zoneList.length;
                        this.setState({
                          regionIndex: index,
                          isEditingRegion: nonZeroZones,
                          showZoneForm: !nonZeroZones
                        });
                      }}
                    >
                      <div>
                        <Field
                          name={`regionList[${index}].regionCode.label`}
                          type="text"
                          component={YBTextInputWithLabel}
                          isReadOnly={true}
                          input={{
                            value: region.regionCode.label
                          }}
                        />
                      </div>
                      <div>
                        {regionList[index].zoneList &&
                          regionList[index].zoneList.length +
                            (regionList[index].zoneList.length > 1 ? ' zones' : ' zone')}
                      </div>
                      <div>
                        <button
                          type="button"
                          className="delete-provider"
                          onClick={(e) => {
                            arrayHelpers.remove(index);
                            e.stopPropagation();
                          }}
                        >
                          <i className="fa fa-times fa-fw delete-row-btn" />
                        </button>
                      </div>
                    </li>
                  ))}
                </ul>
                <button
                  type="button"
                  className={'btn btn-default btn-add-region'}
                  onClick={() => this.addRegionModal(arrayHelpers.push)}
                >
                  <div className="btn-icon">
                    <i className="fa fa-plus"></i>
                  </div>
                  Add region
                </button>
              </Fragment>
            )}
          />
        </Col>
      </Row>
    );
  }
}

export default formikConnect(AddRegionList);
