// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Field } from 'redux-form';
import { isNonEmptyArray } from 'utils/ObjectUtils';
import { YBSelectWithLabel } from '../../common/forms/fields';

export default class CollectionSubtypeRow extends Component {

  static getTypeOptions(initValue, displayValue, dataTypes) {
    let options = [<option key={initValue} value={initValue}>{displayValue}</option>];
    if (isNonEmptyArray(dataTypes)) {
      options = options.concat(dataTypes.map((item, idx) => {
        return <option key={idx} value={item}>{item}</option>
      }));
    }
    return options;
  }

  render() {
    const {tables: {columnDataTypes: {collections, primitives}}, item, columnType} = this.props;
    let showKeySubtype = collections.indexOf(columnType) > -1;
    let showValueSubtype = columnType === "MAP";

    if (showKeySubtype) {
      let keyTypes = CollectionSubtypeRow.getTypeOptions("Key Type", "Key Type", primitives);
      let valueTypes = CollectionSubtypeRow.getTypeOptions("Value Type", "Value Type", primitives);

      return (
        <Row>
          <Col md={5} />
          <Col md={showValueSubtype ? 3 : 4}>
            <Field name={`${item}.keyType`} options={keyTypes} component={YBSelectWithLabel}
                   placeHolder={"Key Type"} />
          </Col>
          {showValueSubtype ?
            <Col md={3}>
              <Field name={`${item}.valueType`} options={valueTypes}
                     component={YBSelectWithLabel} placeHolder={"Value Type"} />
            </Col> :
            <span />}
        </Row>
      );
    } else {
      return <span />;
    }
  }
}
