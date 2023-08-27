import { useState, useEffect } from 'react';
// Can't use `useLocation` hook because this component is the child
// of a component that calls withRouter: https://github.com/ReactTraining/react-router/issues/7015
import { withRouter } from 'react-router';
import { useSelector } from 'react-redux';
import { Dropdown, MenuItem, Alert } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Highlighter } from '../../helpers/Highlighter';
import { YBPanelItem } from '../panels';
import { YBButtonLink } from '../common/forms/fields';
import { useLiveQueriesApi, filterBySearchTokens } from './helpers/queriesHelper';
import { YBLoadingCircleIcon } from '../common/indicators';
import { getProxyNodeAddress } from '../../utils/UniverseUtils';
import { QuerySearchInput } from './QuerySearchInput';
import { QueryType } from './helpers/constants';
import { QueryInfoSidePanel } from './QueryInfoSidePanel';
import { QueryApi } from '../../redesign/helpers/constants';

import './LiveQueries.scss';

const dropdownColKeys = {
  'Node Name': {
    value: 'nodeName',
    type: 'string'
  },
  'Private IP': {
    value: 'privateIp',
    type: 'string'
  },
  Keyspace: {
    value: 'keyspace',
    type: 'string'
  },
  'DB Name': {
    value: 'dbName',
    type: 'string'
  },
  Query: {
    value: 'query',
    type: 'string'
  },
  'Elapsed Time': {
    value: 'elapsedMillis',
    type: 'number'
  },
  Type: {
    value: 'type',
    type: 'string'
  },
  'Client Host': {
    value: 'clientHost',
    type: 'string'
  },
  'Client Port': {
    value: 'clientPort',
    type: 'string'
  },
  'Session Status': {
    value: 'sessionStatus',
    type: 'string'
  },
  'Client Name': {
    value: 'appName',
    type: 'string'
  }
};

const LiveQueriesComponent = ({ location }) => {
  const [type, setType] = useState('YSQL');
  const [searchTokens, setSearchTokens] = useState([]);
  const [selectedRow, setSelectedRow] = useState([]);
  const currentUniverse = useSelector((state) => state.universe.currentUniverse);
  const universeUUID = currentUniverse?.data?.universeUUID;
  const universePaused = currentUniverse?.data?.universeDetails?.universePaused;
  const { ycqlQueries, ysqlQueries, loading, errors, getLiveQueries } = useLiveQueriesApi({
    universeUUID
  });
  const isYSQL = type === 'YSQL';

  useEffect(() => {
    if (location.search) {
      if ('nodeName' in location.query && location.query.nodeName.toLowerCase() !== 'all') {
        setSearchTokens([
          {
            label: 'Node Name',
            key: 'nodeName',
            value: location.query['nodeName']
          }
        ]);
      }
    }
  }, [location.search, location.query]);

  // Need to close the details side panel if refetching
  useEffect(() => {
    if (loading && selectedRow.length) {
      setSelectedRow([]);
    }
  }, [loading]); // eslint-disable-line react-hooks/exhaustive-deps

  const getTserverLink = (cell, row) => {
    const tserverPort = currentUniverse?.data?.universeDetails?.communicationPorts?.tserverHttpPort;
    const href = getProxyNodeAddress(universeUUID, row.privateIp, tserverPort);

    return (
      <a href={href} title={cell} target="_blank" rel="noopener noreferrer">
        {cell}
      </a>
    );
  };

  // For overriding Bootstrap toolbar elements and inserting
  // custom CSS classes
  const renderTableToolbar = ({ components }) => {
    return <div className="toolbar-container">{components.searchPanel}</div>;
  };

  const renderCustomSearchPanel = ({ placeholder, search, clearBtnClick }) => {
    return (
      <QuerySearchInput
        id="live-query-search-bar"
        columns={dropdownColKeys}
        placeholder={placeholder}
        searchTerms={searchTokens}
        onSearch={search}
        onClear={clearBtnClick}
        onSubmitSearchTerms={setSearchTokens}
      />
    );
  };

  /**
   * We truncate the query text to 50 characters because highlight.js will
   * cut off excess text and we don't need to display the full statement when
   * the user can instead open the query in the side panel. Not truncating has
   * caused performance issues when large BATCH queries were received. Each query
   * statement contained over 10kB of text, causing highlight.js to create roughly
   * 4000 additional DOM nodes, so having three or more entries caused the page to
   * completely freeze up due to re-renders and layout calculations.
   *
   * @param {String} cell A YSQL or YCQL query statement
   */
  const getQueryStatement = (cell) => {
    const truncatedText = cell.length > 50 ? `${cell.substring(0, 50)}...` : cell;
    return (
      <div className="query-container">
        <Highlighter type="sql" text={truncatedText} element="pre" />
      </div>
    );
  };

  const handleRowSelect = (row, isSelected) => {
    if (isSelected) {
      setSelectedRow([row.id]);
    } else if (!isSelected && row.id === selectedRow[0]) {
      setSelectedRow([]);
    }
    return true;
  };

  const displayedQueries = isYSQL
    ? filterBySearchTokens(ysqlQueries, searchTokens, dropdownColKeys)
    : filterBySearchTokens(ycqlQueries, searchTokens, dropdownColKeys);

  let failedQueries = null;
  if (isYSQL) {
    if (errors.ysql > 0) {
      const percentFailed = parseFloat(errors.ysql) / (errors.ysql + ysqlQueries.length);
      failedQueries = (
        <Alert bsStyle={percentFailed > 0.8 ? 'danger' : 'warning'}>
          Number of failed queries: {errors.ysql}/{errors.ysql + ysqlQueries.length}
        </Alert>
      );
    }
  } else if (errors.ycql > 0) {
    const percentFailed = parseFloat(errors.ycql) / (errors.ycql + ycqlQueries.length);
    failedQueries = (
      <Alert bsStyle={percentFailed > 0.8 ? 'danger' : 'warning'}>
        Number of failed queries: {errors.ycql}/{errors.ycql + ycqlQueries.length}
      </Alert>
    );
  }

  return (
    <div className="live-queries">
      <YBPanelItem
        header={
          <div className="live-queries__container-title clearfix spacing-top">
            <div className="pull-left">
              <h2 className="content-title pull-left" data-testid="LiveQueries-Header">
                Live Queries
                {loading && !universePaused && (
                  <span className="live-queries__loading-indicator">
                    <YBLoadingCircleIcon size="small" />
                  </span>
                )}
              </h2>
            </div>
            {failedQueries}
            <div className="pull-right">
              <YBButtonLink
                btnIcon="fa fa-refresh"
                btnClass="btn btn-default refresh-btn"
                onClick={getLiveQueries}
              />
              <div>
                <div className="live-queries__dropdown-label">Show live queries</div>
                <Dropdown id="queries-filter-dropdown" pullRight={true}>
                  <Dropdown.Toggle>
                    <i className="fa fa-database"></i>&nbsp;
                    {type}
                  </Dropdown.Toggle>
                  <Dropdown.Menu>
                    <MenuItem key="YCQL" active={!isYSQL} onClick={() => setType('YCQL')}>
                      YCQL
                    </MenuItem>
                    <MenuItem key="YSQL" active={isYSQL} onClick={() => setType('YSQL')}>
                      YSQL
                    </MenuItem>
                  </Dropdown.Menu>
                </Dropdown>
              </div>
            </div>
          </div>
        }
        body={
          <div className="live-queries__table">
            <BootstrapTable
              data={displayedQueries}
              search
              searchPlaceholder="Filter by column or query text"
              multiColumnSearch
              selectRow={{
                mode: 'checkbox',
                clickToSelect: true,
                onSelect: handleRowSelect,
                selected: selectedRow
              }}
              options={{
                clearSearch: true,
                toolBar: renderTableToolbar,
                searchPanel: renderCustomSearchPanel
              }}
            >
              <TableHeaderColumn dataField="id" isKey={true} hidden={true} />
              <TableHeaderColumn
                dataField="nodeName"
                width="200px"
                dataFormat={getTserverLink}
                dataSort
              >
                Node Name
              </TableHeaderColumn>
              <TableHeaderColumn dataField="privateIp" width="200px" dataSort>
                Private IP
              </TableHeaderColumn>
              <TableHeaderColumn dataField={isYSQL ? 'dbName' : 'keyspace'} width="120px" dataSort>
                {isYSQL ? 'DB Name' : 'Keyspace'}
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="query"
                width="350px"
                dataSort
                dataFormat={getQueryStatement}
              >
                Query
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="elapsedMillis"
                width="150px"
                dataFormat={(cell) => `${cell} ms`}
                dataSort
              >
                Elapsed Time (ms)
              </TableHeaderColumn>
              <TableHeaderColumn dataField={isYSQL ? 'appName' : 'type'} width="200px" dataSort>
                {isYSQL ? 'Client Name' : 'Type'}
              </TableHeaderColumn>
              <TableHeaderColumn dataField="clientHost" width="150px" dataSort>
                Client Host
              </TableHeaderColumn>
              <TableHeaderColumn dataField="clientPort" width="100px" dataSort>
                Client Port
              </TableHeaderColumn>
            </BootstrapTable>
          </div>
        }
      />
      <QueryInfoSidePanel
        onHide={() => setSelectedRow([])}
        queryData={displayedQueries.find((x) => selectedRow.length && x.id === selectedRow[0])}
        queryType={QueryType.LIVE}
        queryApi={isYSQL ? QueryApi.YSQL : QueryApi.YCQL}
        visible={selectedRow.length}
      />
    </div>
  );
};

export const LiveQueries = withRouter(LiveQueriesComponent);
