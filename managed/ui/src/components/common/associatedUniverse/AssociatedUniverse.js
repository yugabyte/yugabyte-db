import { YBModal } from '../forms/fields';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Link } from 'react-router';
import './AssociatedUniverse.scss';

/**
 * Returns the decorated status.
 * @param universeStatus status of universe.
 */
const modifyStatus = (item, row) => {
  // TODO: Modify this logic when status prop is added from backend.
  const universeStatus = row.universePaused
    ? 'Paused'
    : row.updateSucceeded && !row.updateInProgress
    ? 'Ready'
    : 'Error';
  switch (universeStatus) {
    case 'Ready':
      return (
        <div className="universe-status associated-universe-status good">
          <i className="fa fa-check-circle associated-universe-icon" />
          {universeStatus && <span>{universeStatus}</span>}
        </div>
      );
    case 'Error':
      return (
        <div className="universe-status associated-universe-status bad">
          <i className="fa fa-warning associated-universe-icon" />
          {universeStatus && <span>{universeStatus}</span>}
        </div>
      );
    case 'Paused':
      return (
        <div className="universe-status associated-universe-status paused">
          <i className="fa fa-pause-circle-o associated-universe-icon" />
          {universeStatus && <span>{universeStatus}</span>}
        </div>
      );
    default:
      return <div>{universeStatus}</div>;
  }
};

/**
 * Returns the universe name with router link.
 * @param universeName - Name of the universe.
 */
const getUniverseLink = (universeName, row) => {
  return (
    <div>
      <Link to={`/universes/${row.uuid}`}>{universeName}</Link>
    </div>
  );
};

export const AssociatedUniverse = ({ onHide, associatedUniverses, visible, title }) => {
  return (
    <YBModal
      visible={visible}
      onHide={onHide}
      submitLabel="Close"
      onFormSubmit={onHide}
      title={`Universes using ${title}`}
    >
      {associatedUniverses?.length ? (
        <BootstrapTable
          data={associatedUniverses}
          className="backup-list-table middle-aligned-table"
        >
          <TableHeaderColumn dataField="uuid" hidden={true} isKey={true}>
            UUID
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="name"
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
      ) : (
        <>No Associated Universe.</>
      )}
    </YBModal>
  );
};
