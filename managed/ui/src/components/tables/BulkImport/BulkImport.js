// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Field } from 'redux-form';
import { YBModal, YBTextInputWithLabel } from '../../common/forms/fields';
import { isValidObject, trimString, normalizeToPositiveInt } from '../../../utils/ObjectUtils';

export default class BulkImport extends Component {

  constructor(props) {
    super(props);
    this.confirmBulkImport = this.confirmBulkImport.bind(this)
  }

  confirmBulkImport(values) {
    const {
      currentTableDetail: { tableDetails: { tableName, keyspace }, tableUUID, universeUUID },
      universeDetails
    } = this.props;
    let instanceCount = values["instanceCount"] === undefined ?
      universeDetails.userIntent.numNodes * 8 :
      values["instanceCount"];
    const payload = {
      "tableName": tableName,
      "keyspace": keyspace,
      "s3Bucket": values["s3Bucket"],
      "instanceCount": instanceCount
    };
    this.props.onHide();
    this.props.bulkImport(universeUUID, tableUUID, payload);
  }

  render() {
    const { visible, onHide, currentTableDetail, handleSubmit, universeDetails } = this.props;

    if (isValidObject(currentTableDetail.tableDetails) && isValidObject(universeDetails)
      && isValidObject(universeDetails.userIntent)) {
      let tableName = currentTableDetail.tableDetails.tableName;
      let keyspace = currentTableDetail.tableDetails.keyspace;
      let s3label = "S3 Bucket with data to be loaded into " + keyspace + "." + tableName;

      return (
        <div className="universe-apps-modal">
          <YBModal title={"Bulk Import into " + keyspace + "." + tableName}
                   visible={visible}
                   onHide={onHide}
                   showCancelButton={true}
                   cancelLabel={"Cancel"}
                   onFormSubmit={handleSubmit(this.confirmBulkImport)}>
            <Field name="s3Bucket" component={YBTextInputWithLabel} label={s3label}
                   placeHolder="s3://foo.bar.com/bulkload/" normalize={trimString}/>
            <Field name="instanceCount"
                   component={YBTextInputWithLabel}
                   label={"Number of task instances for EMR job"}
                   placeHolder={universeDetails.userIntent.numNodes * 8}
                   normalize={normalizeToPositiveInt} />
          </YBModal>
        </div>
      );
    }
    return <span />;
  }
}
