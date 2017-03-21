// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { isValidObject, isValidArray } from '../../../utils/ObjectUtils';
import './TableSchema.scss';

export default class TableSchema extends Component {
  render() {
    const {tableInfo: {tableDetails}} = this.props;
    var partitionKeyRows = [];
    var clusteringKeyRows = [];
    var otherKeyRows = [];
    if (isValidObject(tableDetails) && isValidArray(tableDetails.columns)) {
      tableDetails.columns.forEach(function(item){
        if (item.isPartitionKey) {
          partitionKeyRows.push(item);
        } else if (item.isClusteringKey) {
          clusteringKeyRows.push(item);
        } else {
          otherKeyRows.push(item);
        }
      });
    }
    return (
      <div>
        <Row className={"schema-definition-header"}>
          <Col lg={3}></Col>
          <Col lg={9}>
            <Row>
              <Col lg={4}>Name</Col>
              <Col lg={4}>Type</Col>
              <Col lg={3}>Order</Col>
            </Row>
          </Col>
        </Row>
        <hr />
        <Row>
          <Col lg={3}>
            <h5 className="no-bottom-margin">Partition Key Columns</h5>
          </Col>
          <Col lg={9}>
             <SchemaRowDefinition rows={partitionKeyRows}/>
          </Col>
        </Row>
        <hr />
        <Row>
          <Col lg={3}>
            <h5 className="no-bottom-margin">Clustering Columns</h5>
          </Col>
          <Col lg={9}>
            <SchemaRowDefinition rows={clusteringKeyRows}/>
          </Col>
        </Row>
        <hr />
        <Row className="other-column-container">
          <Col lg={3}>
            <h5 className="no-bottom-margin">Other Columns</h5>
          </Col>
          <Col lg={9}>
             <SchemaRowDefinition rows={otherKeyRows}/>
          </Col>
        </Row>
      </div>
    )
  }
}

class SchemaRowDefinition extends Component {
  render() {
    const {rows} = this.props;
    var rowEntries = <span/>;
    if (isValidArray(rows)) {
      rowEntries = rows.map(function(item, idx){
        return (
        <Row key={idx}>
          <Col lg={4}>{item.name}</Col>
          <Col lg={4}>{item.type}</Col>
          <Col lg={3}>{item.isClusteringKey ? 'ASC' : ''}</Col>
        </Row>
        )
      });
    }
    return (
      <div>
        {rowEntries}
      </div>
    )
  }
}
