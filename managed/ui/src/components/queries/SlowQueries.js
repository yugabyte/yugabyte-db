import React, { useState, useEffect, useRef, useLayoutEffect, useMemo } from 'react';
import { withRouter } from 'react-router';
import clsx from 'clsx';
import { useSelector } from 'react-redux';
import { Dropdown, MenuItem, Alert } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useSlowQueriesApi, filterBySearchTokens } from './helpers/queriesHelper';
import { QueryInfoSidePanel } from './QueryInfoSidePanel';
import { Highlighter } from '../../helpers/Highlighter';
import { YBPanelItem } from '../panels';
import { YBLoadingCircleIcon } from '../common/indicators';
import { YBCheckBox } from '../common/forms/fields';
import { QuerySearchInput } from './QuerySearchInput';

const dropdownColKeys = {
  Query: {
    value: 'query',
    type: 'string'
  },
  Database: {
    value: 'datname',
    type: 'string'
  },
  User: {
    value: 'rolname',
    type: 'string'
  },
  Count: {
    value: 'calls',
    type: 'number'
  },
  'Total Time': {
    value: 'total_time',
    type: 'number'
  },
  Rows: {
    value: 'rows',
    type: 'number'
  },
  'Avg Time': {
    value: 'mean_time',
    type: 'number',
    display: 'Avg Exec Time (ms)'
  },
  'Min Time': {
    value: 'min_time',
    type: 'number',
    display: 'Min Exec Time (ms)'
  },
  'Max Time': {
    value: 'max_time',
    type: 'number',
    display: 'Max Exec Time (ms)'
  },
  'StdDev Time': {
    value: 'stddev_time',
    type: 'number',
    display: 'Std Dev Time'
  },
  'Temp Tables': {
    value: 'local_blks_written',
    type: 'number',
    display: 'Temp Tables RAM'
  }
};

const initialQueryColumns = {
  Query: { disabled: true },
  Database: {},
  User: {},
  'Total Time': {},
  Count: {},
  Rows: {}
};

const PANEL_STATE = {
  INITIAL: 0,
  MINIMIZED: 1,
  MAXIMIZED: 2
};

const SlowQueriesComponent = () => {
  const [selectedRow, setSelectedRow] = useState([]);
  const [panelState, setPanelState] = useState(PANEL_STATE.INITIAL);
  const [searchTokens, setSearchTokens] = useState([]);
  let initialColumns = initialQueryColumns;
  try {
    const cachedColumns = localStorage.getItem('__yb_slow_query_columns__');
    // If error is thrown from JSON.parse(), default value will still be set
    initialColumns = cachedColumns ? JSON.parse(cachedColumns) : initialQueryColumns;
  } catch (e) {
    console.warn('Invalid column header data detected, defaulting to initial values.');
  }
  const [columns, setColumns] = useState(initialColumns);
  const currentUniverse = useSelector((state) => state.universe.currentUniverse);
  const universeUUID = currentUniverse?.data?.universeUUID;
  const { ysqlQueries, loading, errors } = useSlowQueriesApi({
    universeUUID
  });
  const [hideQueryAlert, setQueryAlert] = useState(localStorage.getItem('__yb_close_query_info__'));

  const handleRowSelect = (row, isSelected) => {
    if (isSelected) {
      setSelectedRow([row.queryid]);
    } else if (!isSelected && row.queryid === selectedRow[0]) {
      setSelectedRow([]);
    }
    return true;
  };

  // Need to close the details side panel if refetching
  useEffect(() => {
    if (loading && selectedRow.length) {
      setSelectedRow([]);
    }
  }, [loading]);

  const getQueryStatement = (cell) => {
    const truncatedText = cell.length > 200 ? `${cell.substring(0, 200)}...` : cell;
    return (
      <div className="query-container">
        <Highlighter type="sql" text={truncatedText} element="pre" />
      </div>
    );
  };

  const renderCustomSearchPanel = ({ placeholder, search, clearBtnClick }) => {
    return (
      <QuerySearchInput
        id="slow-query-search-bar"
        columns={dropdownColKeys}
        placeholder={placeholder}
        searchTerms={searchTokens}
        onSearch={search}
        onClear={clearBtnClick}
        onSubmitSearchTerms={setSearchTokens}
      />
    );
  };

  // For overriding Bootstrap toolbar elements and inserting
  // custom CSS classes
  const renderTableToolbar = ({ components }) => {
    return <div className="toolbar-container">{components.searchPanel}</div>;
  };

  const handleChangeColumnDisplay = (ev, col) => {
    const newCols = { ...columns };
    if (ev.target.checked) {
      newCols[col] = {};
    } else {
      delete newCols[col];
    }
    setColumns(newCols);
    localStorage.setItem('__yb_slow_query_columns__', JSON.stringify(newCols));
  };

  const formatMillisNumber = (cell) => {
    if (!Number.isInteger(cell)) {
      return Number(cell).toFixed(3);
    }
    return cell;
  };

  const handleCloseAlert = () => {
    setQueryAlert(true);
    localStorage.setItem('__yb_close_query_info__', true);
  };

  const displayedQueries = filterBySearchTokens(ysqlQueries, searchTokens, dropdownColKeys);

  const tableColHeaders = [
    <TableHeaderColumn key="query-id-key" dataField="queryid" isKey={true} hidden={true} />,
    ...Object.keys(columns).map((keyName) => {
      if (keyName === 'Query') {
        return (
          <TableHeaderColumn
            key="query-statement-key"
            dataField="query"
            dataSort
            dataAlign="start"
            width="300px"
            dataFormat={getQueryStatement}
          >
            Query
          </TableHeaderColumn>
        );
      } else {
        const dataFormat =
          dropdownColKeys[keyName].type === 'number' ? formatMillisNumber : undefined;
        return (
          <TableHeaderColumn
            key={`query-${dropdownColKeys[keyName].value}-key`}
            dataField={dropdownColKeys[keyName].value}
            dataSort
            dataAlign="start"
            width="100px"
            dataFormat={dataFormat}
          >
            {dropdownColKeys[keyName].display || keyName}
          </TableHeaderColumn>
        );
      }
    })
  ];

  return (
    <div className="slow-queries">
      <YBPanelItem
        header={
          <div className="slow-queries__container-title clearfix spacing-top">
            <div className="slow-queries__title-container">
              <h2 className="content-title">
                Slow Queries
                {loading && (
                  <span className="slow-queries__loading-indicator">
                    <YBLoadingCircleIcon size="small" />
                  </span>
                )}
              </h2>
              <div
                onClick={() => setPanelState(PANEL_STATE.MAXIMIZED)}
                className={clsx('left-panel-fab', panelState === PANEL_STATE.MINIMIZED && 'load')}
              >
                <span className="panel-open-icon">
                  <i className="fa fa-bars" />
                </span>
              </div>
            </div>
            {errors.message ? (
              <Alert bsStyle="danger">
                Error: {errors.message}. Have you set up the database user correctly?
              </Alert>
            ) : (
              !hideQueryAlert && (
                <Alert bsStyle="info">
                  Slow query logging for YCQL is not yet supported.
                  <span className="alert-close" onClick={handleCloseAlert}>
                    <i className="fa fa-times" />
                  </span>
                </Alert>
              )
            )}
          </div>
        }
        leftPanel={
          <div
            className={clsx(
              'left-panel',
              panelState === PANEL_STATE.MINIMIZED && 'minimized',
              panelState === PANEL_STATE.MAXIMIZED && 'maximized'
            )}
          >
            <span className="panel-close-icon" onClick={() => setPanelState(PANEL_STATE.MINIMIZED)}>
              <i className="fa fa-window-minimize" />
            </span>
            <div className="slow-queries__column-selector">
              <h5>Column Display</h5>
              <ul>
                {Object.keys(dropdownColKeys).map((keyName, index) => {
                  const isChecked = keyName in columns;
                  const disabled = isChecked && columns[keyName].disabled;
                  return (
                    <li key={`column-${keyName}-${index}`} className={disabled ? 'disabled' : ''}>
                      <YBCheckBox
                        checkState={isChecked}
                        disabled={disabled}
                        onClick={(e) => {
                          if (!disabled) handleChangeColumnDisplay(e, keyName);
                        }}
                      />
                      {dropdownColKeys[keyName].display || keyName}
                    </li>
                  );
                })}
              </ul>
            </div>
          </div>
        }
        bodyClassName={clsx(
          panelState === PANEL_STATE.MINIMIZED && 'expand',
          panelState === PANEL_STATE.MAXIMIZED && 'shrink'
        )}
        body={
          <div className="slow-queries__table">
            <BootstrapTable
              data={displayedQueries}
              pagination
              search
              searchPlaceholder="Filter by query text"
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
              {tableColHeaders}
            </BootstrapTable>
          </div>
        }
      />
      <QueryInfoSidePanel
        visible={selectedRow.length}
        onHide={() => setSelectedRow([])}
        data={ysqlQueries.find((x) => selectedRow.length && x.queryid === selectedRow[0])}
      />
    </div>
  );
};

export const SlowQueries = withRouter(SlowQueriesComponent);
