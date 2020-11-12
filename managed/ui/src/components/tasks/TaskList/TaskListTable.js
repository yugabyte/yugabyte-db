// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Link } from 'react-router';
import { YBPanelItem } from '../../panels';
import { timeFormatter, successStringFormatter } from '../../../utils/TableFormatters';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';

import './TasksList.scss';

export default class TaskListTable extends Component {
  static defaultProps = {
    title: 'Tasks',
    pagination: {}
  };
  static propTypes = {
    taskList: PropTypes.array.isRequired,
    overrideContent: PropTypes.object,
    isCommunityEdition: PropTypes.bool
  };

  state = {
    dropdownOpen: false,
    currentPageNumber: 1
  };

  changePageSize = (curr, size, onChange) => {
    this.setState({ dropdownOpen: false }, () => {
      if (curr !== size) {
        onChange(size);
        this.props.queryCustomerTasks(1, size).then(() => {
          this.setState({ currentPageNumber: 1 });
        });
      }
    });
  };

  renderPaginationPanel = (props) => {
    const { pagination, queryCustomerTasks } = this.props;
    const currentPage = this.state.currentPageNumber;
    const pageSize = props.sizePerPage;
    const pageStartIndex = props.pageStartIndex;
    const rangeArray = [];
    let numResults = 'No results';
    let totalPages = props.totalPages;
    if (isNonEmptyObject(pagination)) {
      numResults = `${pagination.items} Results`;
      totalPages = Math.ceil(parseFloat(pagination.items) / pageSize);
    }
    const startIndex =
      currentPage > 5 && totalPages > 10 ? Math.min(totalPages - 10, currentPage - 5) : 1;
    for (let i = startIndex; i <= totalPages && i < startIndex + 10; i++) {
      rangeArray.push(i);
    }

    return (
      <div className="pagination-panel">
        <div className="pagination-panel__results">{numResults}</div>
        <div className="pagination-panel__page-size">
          <span>Show: </span>
          <span class="dropdown react-bs-table-sizePerPage-dropdown">
            <button
              class="btn btn-default btn-secondary dropdown-toggle"
              id="pageDropDown"
              onClick={() => this.setState({ dropdownOpen: !this.state.dropdownOpen })}
            >
              {props.sizePerPage}
              <span className="caret"></span>
            </button>
            <ul
              className={this.state.dropdownOpen ? 'dropdown-menu show open' : 'dropdown-menu'}
              role="menu"
              aria-labelledby="pageDropDown"
            >
              <li
                role="presentation"
                className="dropdown-item"
                onClick={() => this.changePageSize(pageSize, 10, props.changeSizePerPage)}
              >
                <a role="menuitem" tabIndex="-1" href="#" data-page="10">
                  10
                </a>
              </li>
              <li
                role="presentation"
                className="dropdown-item"
                onClick={() => this.changePageSize(pageSize, 25, props.changeSizePerPage)}
              >
                <a role="menuitem" tabIndex="-1" href="#" data-page="25">
                  25
                </a>
              </li>
              <li
                role="presentation"
                className="dropdown-item"
                onClick={() => this.changePageSize(pageSize, 50, props.changeSizePerPage)}
              >
                <a role="menuitem" tabIndex="-1" href="#" data-page="50">
                  50
                </a>
              </li>
              <li
                role="presentation"
                className="dropdown-item"
                onClick={() => this.changePageSize(pageSize, 100, props.changeSizePerPage)}
              >
                <a role="menuitem" tabIndex="-1" href="#" data-page="100">
                  100
                </a>
              </li>
            </ul>
          </span>
        </div>
        <ul className="pagination">
          {(currentPage > pageStartIndex ||
            (currentPage !== pageStartIndex && pagination.previous)) && (
            <li
              className="page-item"
              title="previous page"
              onClick={(ev) => {
                queryCustomerTasks(currentPage - 1, pageSize).then(() => {
                  this.setState({ currentPageNumber: currentPage - 1 });
                });
                ev.preventDefault();
              }}
            >
              <a href="#" className="page-link">
                &lt;
              </a>
            </li>
          )}
          {rangeArray.map((val) => (
            <li
              key={`page-${val}`}
              className={currentPage === val ? 'page-item active' : 'page-item'}
              title={val}
              onClick={(ev) => {
                if (val !== currentPage) {
                  queryCustomerTasks(val, pageSize).then(() => {
                    this.setState({ currentPageNumber: val });
                  });
                  ev.preventDefault();
                }
              }}
            >
              <a href="#" className="page-link">
                {val}
              </a>
            </li>
          ))}
          {(currentPage < totalPages || pagination.next) && (
            <li
              className="page-item"
              title="next page"
              onClick={(ev) => {
                queryCustomerTasks(currentPage + 1, pageSize).then(() => {
                  this.setState({ currentPageNumber: currentPage + 1 });
                });
                ev.preventDefault();
              }}
            >
              <a href="#" className="page-link">
                &gt;
              </a>
            </li>
          )}
        </ul>
      </div>
    );
  };

  render() {
    const { taskList, title, overrideContent, isCommunityEdition } = this.props;

    function nameFormatter(cell, row) {
      return <span>{row.title.replace(/.*:\s*/, '')}</span>;
    }

    function typeFormatter(cell, row) {
      return (
        <span>
          {row.type} {row.target}
        </span>
      );
    }

    const taskDetailLinkFormatter = function (cell, row) {
      if (row.status === 'Failure') {
        return <Link to={`/tasks/${row.id}`}>See Details</Link>;
      } else {
        return <span />;
      }
    };
    const tableBodyContainer = { marginBottom: '1%', paddingBottom: '1%' };
    const options = {
      paginationPanel: this.renderPaginationPanel
    };
    return (
      <YBPanelItem
        header={<h2 className="task-list-header content-title">{title}</h2>}
        body={
          isCommunityEdition ? (
            overrideContent
          ) : (
            <BootstrapTable
              data={taskList}
              bodyStyle={tableBodyContainer}
              options={options}
              pagination={true}
              search
              multiColumnSearch
              
              searchPlaceholder="Filter on page"
            >
              <TableHeaderColumn dataField="id" isKey={true} hidden={true} />
              <TableHeaderColumn
                dataField="type"
                dataFormat={typeFormatter}
                columnClassName="no-border name-column"
                className="no-border"
              >
                Type
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="title"
                dataFormat={nameFormatter}
                dataSort
                columnClassName="no-border name-column"
                className="no-border"
              >
                Name
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="percentComplete"
                dataSort
                columnClassName="no-border name-column"
                className="no-border"
                dataFormat={successStringFormatter}
              >
                Status
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="createTime"
                dataFormat={timeFormatter}
                dataSort
                columnClassName="no-border "
                className="no-border"
                dataAlign="left"
              >
                Start Time
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="completionTime"
                dataFormat={timeFormatter}
                dataSort
                columnClassName="no-border name-column"
                className="no-border"
              >
                End Time
              </TableHeaderColumn>
              <TableHeaderColumn dataField="id" dataFormat={taskDetailLinkFormatter} dataSort>
                Notes
              </TableHeaderColumn>
            </BootstrapTable>
          )
        }
      />
    );
  }
}
