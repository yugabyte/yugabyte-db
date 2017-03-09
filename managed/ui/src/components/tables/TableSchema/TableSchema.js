// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { DescriptionItem } from '../../common/descriptors';
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
          <Col lg={3}>
            Type
          </Col>
          <Col lg={9}>
            <Row>
              <Col lg={2}>Column Order</Col>
              <Col lg={4}>Name</Col>
              <Col lg={2}>Type</Col>
            </Row>
          </Col>
        </Row>
        <Row>
          <Col lg={3}>
            <DescriptionItem title="Partition Key Column">
              <span>(In Order)</span>
            </DescriptionItem>
          </Col>
          <Col lg={9}>
             <SchemaRowDefinition rows={partitionKeyRows}/>
          </Col>
        </Row>
        <Row>
          <Col lg={3}>
            <DescriptionItem title="Clustering Columns">
              <span>(In Order)</span>
            </DescriptionItem>
          </Col>
          <Col lg={9}>
            <SchemaRowDefinition rows={clusteringKeyRows}/>
          </Col>
        </Row>
        <Row className="other-column-container">
          <Col lg={3}>
            <DescriptionItem title="Other Columns">
              <span/>
            </DescriptionItem>
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
          <Col lg={2}>{item.columnOrder}</Col>
          <Col lg={4}>{item.name}</Col>
          <Col lg={2}>{item.type}</Col>
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
