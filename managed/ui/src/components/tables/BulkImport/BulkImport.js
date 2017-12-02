// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Field } from 'redux-form';
import { YBModal, YBTextInputWithLabel } from '../../common/forms/fields';
import { trimString, normalizeToPositiveInt, isDefinedNotNull, isNonEmptyObject }
  from '../../../utils/ObjectUtils';
import { getPrimaryCluster } from "../../../utils/UniverseUtils";

export default class BulkImport extends Component {

  constructor(props) {
    super(props);
    this.confirmBulkImport = this.confirmBulkImport.bind(this);
  }

  confirmBulkImport(values) {
    const {
      currentTableDetail: { tableDetails: { tableName, keyspace }, tableUUID, universeUUID },
      universeDetails: { clusters },
      onHide,
      bulkImport
    } = this.props;
    const primaryCluster = getPrimaryCluster(clusters);
    const instanceCount = isDefinedNotNull(values["instanceCount"]) ?
      values["instanceCount"] :
      primaryCluster.userIntent.numNodes * 8;
    const payload = {
      "tableName": tableName,
      "keyspace": keyspace,
      "s3Bucket": values["s3Bucket"],
      "instanceCount": instanceCount
    };
    onHide();
    bulkImport(universeUUID, tableUUID, payload);
  }

  render() {
    const { visible, onHide, currentTableDetail, handleSubmit, universeDetails } = this.props;

    if (isNonEmptyObject(currentTableDetail.tableDetails) && isNonEmptyObject(universeDetails)) {
      const primaryCluster = getPrimaryCluster(universeDetails.clusters);
      if (!isNonEmptyObject(primaryCluster)) {
        return <span />;
      }
      const tableName = currentTableDetail.tableDetails.tableName;
      const keyspace = currentTableDetail.tableDetails.keyspace;
      const s3label = "S3 Bucket with data to be loaded into " + keyspace + "." + tableName;

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
                   placeHolder={primaryCluster.userIntent.numNodes * 8}
                   normalize={normalizeToPositiveInt} />
          </YBModal>
        </div>
      );
    }
    return <span />;
  }
}
