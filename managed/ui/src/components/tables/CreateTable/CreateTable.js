// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { withRouter } from 'react-router';
import { normalizeToPositiveInt, isValidObject } from '../../../utils/ObjectUtils';
import { Field } from 'redux-form';
import { YBInputField, YBButton, YBRadioButton } from '../../common/forms/fields';
import tableIcon from '../images/table.png';
import './CreateTables.scss';
import { YBConfirmModal } from '../../modals';
import CassandraColumnSpecification from './CassandraColumnSpecification';
import { YBPanelItem } from '../../panels';

class CreateTable extends Component {
  constructor(props) {
    super(props);
    this.state = {nextTab: ''};
  }

  radioClicked = event => {
    this.setState({'activeTable': event.target.value});
  };

  createTable = values => {
    const {universe: {currentUniverse}} = this.props;
    this.props.submitCreateTable(currentUniverse.data, values);
  };

  componentWillReceiveProps(nextProps) {
    if (this.props.location.query !== nextProps.location.query && nextProps.location.query.tab !== "tables") {
      if (nextProps.anyTouched && nextProps.dirty) {
        this.setState({nextTab: nextProps.location.query.tab});
        this.props.showCancelCreateModal();
        const currentLocation = this.props.location;
        currentLocation.query = {tab: "tables"};
        this.props.router.push(currentLocation);
      } else {
        this.props.showListTables();
      }
    }
  }

  hideCancelCreateModal = () => {
    this.props.hideModal();
  };

  confirmCancelCreateModal = () => {
    this.props.hideModal();
    this.props.showListTables();
    if (isValidObject(this.state.nextTab) && this.state.nextTab.length > 0) {
      const currentLocation = this.props.location;
      currentLocation.query = { tab: this.state.nextTab };
      this.props.router.push(currentLocation);
    }
  };

  cancelCreateTableForm = () => {
    if (this.props.anyTouched && this.props.dirty) {
      this.props.showCancelCreateModal();
    } else {
      this.props.showListTables();
    }
  };

  render() {
    const {handleSubmit} = this.props;
    const onFormSubmit = handleSubmit(this.createTable);
    const cassandraLabel = <div><img src={tableIcon} alt="Cassandra-compatible (YCQL)" className="table-type-logo"/>YCQL</div>;
    const tableNameRegex = /^[a-zA-Z0-9_]*$/;
    const tableNameTest = (value, previousValue) => tableNameRegex.test(value) ? value : previousValue;
    return (
      <YBPanelItem
        header={
          <h2 className="content-title">Create Table</h2>
        }
        body={
          <div>
            <form name="CreateTableForm" onSubmit={onFormSubmit}>
              <Row className="create-table-name-container">
                <Col md={6} lg={3}>
                  <div className="form-right-aligned-labels">
                    <Field name="keyspace" component={YBInputField} className={`table-name-cell`}
                           label="Keyspace" placeHolder={"Keyspace"} normalize={tableNameTest} />
                  </div>
                </Col>
                <Col md={6} lg={3}>
                  <div className="form-right-aligned-labels">
                    <Field name="tableName" component={YBInputField} className={`table-name-cell`}
                           label="Name" placeHolder={"Table Name"} normalize={tableNameTest} />
                  </div>
                </Col>
                <Col md={6} lg={3}>
                  <div className="form-right-aligned-labels">
                    <div className="form-group">
                      <label className="form-item-label">
                        Type
                      </label>
                      <div className="yb-field-group">
                        <YBRadioButton name="type" key="cassandra" label={cassandraLabel}
                                       fieldValue="cassandra" checkState={true} />
                      </div>
                    </div>
                  </div>
                </Col>
                <Col md={6} lg={3}>
                  <div className="form-right-aligned-labels">
                    <Field name="ttlInSeconds" component={YBInputField}
                           className={`table-name-cell`} label="TTL (sec)"
                           placeHolder={"optional"} normalize={normalizeToPositiveInt}/>
                  </div>
                </Col>
              </Row>
              <CassandraColumnSpecification {...this.props} />
              <div className="form-action-button-container clearfix">
                <YBButton btnText="Create" btnClass={`pull-right btn btn-orange`}
                          btnType="submit"/>
                <YBButton btnText="Cancel" btnClass={`pull-right btn btn-default`}
                          onClick={this.cancelCreateTableForm}/>
              </div>
            </form>
            <YBConfirmModal visibleModal={this.props.universe.visibleModal} title="Create Table"
                             name="cancelCreate" onConfirm={this.confirmCancelCreateModal}
                             hideConfirmModal={this.hideCancelCreateModal} currentModal="cancelCreate"
                             confirmLabel={"Yes"} cancelLabel={"No"}>
              <div>You haven't finished creating a table yet. All your changes will be lost.</div>
              <div>Are you sure you want to exit?</div>
            </YBConfirmModal>
          </div>
        }
      />
    );
  }
}

export default withRouter(CreateTable);


