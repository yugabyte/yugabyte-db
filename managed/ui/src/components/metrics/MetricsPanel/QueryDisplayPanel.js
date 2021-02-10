// Copyright (c) YugaByte, Inc.

import React, { Fragment } from 'react';
import { Link } from 'react-router';
import { maxBy } from 'lodash';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useSlowQueriesApi } from '../../queries/queriesHelper';
import { Highlighter } from '../../../helpers/Highlighter';
import './MetricsPanel.scss';

const GRAPH_COL_WIDTH = 200;
export const QueryDisplayPanel = ({ universeUUID }) => {
  const { ysqlQueries, loading, errors } = useSlowQueriesApi({
    universeUUID
  }); 
  
  // Get top 5 queries by total_time descending
  const topQueries = ysqlQueries.sort((a, b) => b.total_time - a.total_time).slice(0, 5);

  // Iterate through the data and find the highest mean latency and highest max time
  const highestExecTime = maxBy(topQueries, 'total_time')?.total_time;
  const highestMaxTime = maxBy(topQueries, 'max_time')?.max_time;

  const getTimeBarFormat = (num) => {
    return (
      <div>
        {num.toFixed(1)} ms
        <span className="metric-bar" style={{ width: num / highestExecTime * GRAPH_COL_WIDTH }}></span>
      </div>
    );
  }

  const getMeanBarWhiskersFormat = (num, row) => {
    const leftPixel = (row.min_time / highestMaxTime * GRAPH_COL_WIDTH) + 100;
    const widthPixel = (row.max_time - row.min_time) / highestMaxTime * GRAPH_COL_WIDTH;
    return (
      <div>
        {num.toFixed(1)} ms
        <span className="metric-bar" style={{ width: num / highestMaxTime * GRAPH_COL_WIDTH }}></span>
        <div className="whiskers-plot" style={{ width: `${widthPixel}px`, left: `${leftPixel}px`}}><span className="line"></span></div>
      </div>
    );
  }

  const getQueryStatement = (query) => {
    const truncatedText = query.length > 80 ? `${query.substring(0, 80)}...` : query;
    return <Highlighter type="sql" text={truncatedText} element="pre" />
  }

  if (!topQueries || !topQueries.length) {
    return null;
  }

  return (
    <div className="query-display-panel">
      <Link to={`/universes/${universeUUID}/queries?tab=slow-queries`} className="query-display-panel__link">
        Top SQL Statements <i className="fa fa-chevron-right" />
      </Link>
      <BootstrapTable
        data={topQueries}
        bodyContainerClass="top-queries-table"
      >
        <TableHeaderColumn dataField="queryid" isKey={true} hidden={true} />
        <TableHeaderColumn
          dataField="query"
          width="400px"
          dataFormat={getQueryStatement}
        >
          Statement Template
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField="total_time"
          width="300px"
          dataFormat={getTimeBarFormat}
        >
          Total Exec Time
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField="mean_time"
          width="300px"
          dataFormat={getMeanBarWhiskersFormat}
        >
          Mean Latency
        </TableHeaderColumn>
        <TableHeaderColumn dataField="datname" width="200px">
          Database
        </TableHeaderColumn>              
      </BootstrapTable>
    </div>
  );
}
