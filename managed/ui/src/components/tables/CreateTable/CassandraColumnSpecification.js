// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { FieldArray } from 'redux-form';
import KeyColumnList from './KeyColumnList';

export default class CassandraColumnSpecification extends Component {
  render () {
    return (
      <div>
        <hr />
        <Row>
          <Col md={3}>
            <h5 className="no-bottom-margin">Partition Key Columns</h5>
            (In Order)
          </Col>
          <Col md={9}>
            <FieldArray name="partitionKeyColumns" component={KeyColumnList}
                        columnType={"partitionKey"} tables={this.props.tables}/>
          </Col>
        </Row>
        <hr />
        <Row>
          <Col md={3}>
            <h5 className="no-bottom-margin">Clustering Columns</h5>
            (In Order)
          </Col>
          <Col md={9}>
            <FieldArray name="clusteringColumns" component={KeyColumnList} columnType={"clustering"}
                        tables={this.props.tables}/>
          </Col>
        </Row>
        <hr />
        <Row>
          <Col md={3}>
            <h5 className="no-bottom-margin">Other Columns</h5>
          </Col>
          <Col md={9}>
            <FieldArray name="otherColumns" component={KeyColumnList} columnType={"other"}
                        tables={this.props.tables} />
          </Col>
        </Row>
      </div>
    )
  }
}
