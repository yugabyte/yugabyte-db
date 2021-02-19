import React from 'react';
import { YBModal } from '../forms/fields';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Link } from 'react-router';
import './AssociatedUniverse.scss';


function AssociatedUniverse(props) {
  const { closeUniverseListModal, universeList } = props;

  /**
   * Returns the decorated status.
   * @param universeStatus status of universe.
   */
  const modifyStatus = (universeStatus) => {

    // TODO: Modify this to accomodate for pause status.
    switch(universeStatus) {
      case 'Ready':
        return(<div className="universe-status good"><i className="fa fa-check-circle" />{universeStatus && <span>{universeStatus}</span>}</div>)
      case 'Error':
        return(<div className="universe-status bad"><i className="fa fa-warning" />{universeStatus && <span>{universeStatus}</span>}</div>)
    }
  }

  /**
   * Returns the router link of universe.
   * @param universeName - Name of the universe.
   */
  const getUniverseLink = (universeName) => { 
    return (<div><Link to="/universes">{universeName}</Link></div>);
  }
  return (
    <YBModal
      visible={true}
      onHide={closeUniverseListModal}
      showCancelButton={true}
      title={'Universes using this certificate'}
    >
      <div>
        <BootstrapTable data={universeList} className="backup-list-table middle-aligned-table">
          <TableHeaderColumn dataField="universeUUID" hidden={true} isKey={true}>
            UUID
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="universeName"
            columnClassName="no-border name-column"
            className="no-border"
            width="80%"
            dataFormat={getUniverseLink}
          >
            Universe Name
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="universeStatus"
            columnClassName="no-border name-column"
            className="no-border"
            dataFormat={modifyStatus}
          >
            Status
          </TableHeaderColumn>
        </BootstrapTable>
      </div>
    </YBModal>
  );
}

export default AssociatedUniverse;
