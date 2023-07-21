// Copyright (c) YugaByte, Inc.

import { Row, Col } from 'react-bootstrap';

import { UniverseReadWriteMetrics } from '../../metrics';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';

export const CellResourcesPanel = (props) => {
  const {
    universe: { universeUUID, readData, writeData }
  } = props;
  let averageReadRate = Number(0).toFixed(2);
  let averageWriteRate = Number(0).toFixed(2);

  if (isNonEmptyObject(readData)) {
    const readMetricArray = readData.y;
    const sum = readMetricArray.reduce(function (a, b) {
      return parseFloat(a) + parseFloat(b);
    });
    averageReadRate = (sum / readMetricArray.length).toFixed(2);
  }

  if (isNonEmptyObject(writeData)) {
    const writeMetricArray = writeData.y;
    const sum = writeMetricArray.reduce(function (a, b) {
      return parseFloat(a) + parseFloat(b);
    });
    averageWriteRate = (sum / writeMetricArray.length).toFixed(2);
  }
  return (
    <Row>
      <Col md={5}>
        <div className="cell-chart-container">
          <UniverseReadWriteMetrics
            {...props}
            graphIndex={`${universeUUID}-read`}
            readData={readData}
            writeData={writeData}
          />
        </div>
      </Col>
      <Col md={7} className="cell-read-write">
        <div className="cell-read-write-row">
          <span className="legend-square read-color" />
          <span className="metric-label-type">Read </span>
          <span className="label-type-identifier">ops/sec</span>
          <span className="cell-read-write-value">
            {averageReadRate}
            <span className="metric-value-label">&nbsp;avg</span>
          </span>
        </div>
        <div className="cell-read-write-row">
          <span className="legend-square write-color" />
          <span className="metric-label-type">Write </span>
          <span className="label-type-identifier">ops/sec</span>
          <span className="cell-read-write-value">
            {averageWriteRate}
            <span className="metric-value-label">&nbsp;avg</span>
          </span>
        </div>
      </Col>
    </Row>
  );
};
