import React, { useEffect, useState } from 'react';
import clsx from 'clsx';
import { withRouter } from 'react-router';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { DropdownButton, MenuItem } from 'react-bootstrap';

import { YBMenuItem } from '../../UniverseDetail/compounds/YBMenuItem';
import { YBButton, YBModal } from '../../../common/forms/fields';

import { ybFormatDate, YBTimeFormats } from '../../../../redesign/helpers/DateUtils';
import { formatBytes } from '../../../xcluster/ReplicationUtils';

import './ThirdStep.scss';

const statusElementsIcons = {
  Success: (
    <span className="status success">
      Ready <i className="fa fa-check" />
    </span>
  ),
  Failed: (
    <span className="status failed">
      Creation Failed <i className="fa fa-exclamation-circle" />
    </span>
  ),
  Running: (
    <span className="status creating">
      Creating <i className="fa fa-spinner fa-spin" />
    </span>
  )
};

const getActions = (
  uuid,
  row,
  handleViewLogs,
  handleDeleteBundle,
  isConfirmDeleteOpen,
  setIsConfirmDeleteOpen,
  handleDownloadBundle,
  creatingBundle,
  setDeleteBundleObj
) => {
  const isReady = row.status === 'Success';
  return (
    <>
      <DropdownButton
        id={row.bundleUUID}
        key={row.bundleUUID}
        noCaret
        drop={'start'}
        title={
          <span className="dropdown-text">
            <i className="fa fa-ellipsis-h" />
          </span>
        }
        pullRight
        className="support-action-dropdown"
      >
        {isReady && (
          <MenuItem
            value="Download"
            onClick={() => {
              handleDownloadBundle(row.bundleUUID);
            }}
          >
            <i className="fa fa-download" /> Download
          </MenuItem>
        )}
        {!isReady && (
          <MenuItem
            value="View logs"
            onClick={() => {
              handleViewLogs('/logs');
            }}
          >
            <i className="fa fa-file" /> View logs
          </MenuItem>
        )}
        <YBMenuItem
          disabled={creatingBundle}
          value="Delete"
          onClick={() => {
            setIsConfirmDeleteOpen(true);
            setDeleteBundleObj(row);
          }}
        >
          <i className="fa fa-trash" /> Delete
        </YBMenuItem>
      </DropdownButton>
    </>
  );
};

const ConfirmDeleteModal = ({ createdOn, closeModal, confirmDelete }) => {
  return (
    <YBModal
      visible
      title={'Delete Support Bundle'}
      onHide={() => {
        closeModal();
      }}
      cancelLabel="Close"
      showCancelButton
      submitLabel="Delete"
      onFormSubmit={confirmDelete}
      className="support-bundle-confirm-delete"
    >
      You are about to delete the support bundle that was created on{' '}
      <span className="created-on-date">{createdOn}</span>. This can not be undone
    </YBModal>
  );
};

export const ThirdStep = withRouter(
  ({ onCreateSupportBundle, handleDeleteBundle, handleDownloadBundle, supportBundles, router }) => {
    const [creatingBundle, setCreatingBundle] = useState(
      supportBundles &&
        Array.isArray(supportBundles) &&
        supportBundles.find((supportBundle) => supportBundle.status === 'Running') !== undefined
    );
    const [isConfirmDeleteOpen, setIsConfirmDeleteOpen] = useState(false);
    const [deleteBundleObj, setDeleteBundleObj] = useState({});

    useEffect(() => {
      if (
        supportBundles &&
        Array.isArray(supportBundles) &&
        supportBundles.find((supportBundle) => supportBundle.status === 'Running') !== undefined
      ) {
        setCreatingBundle(true);
      } else {
        setCreatingBundle(false);
      }
    }, [supportBundles, setCreatingBundle]);

    return (
      <>
        {isConfirmDeleteOpen && (
          <ConfirmDeleteModal
            closeModal={() => {
              setIsConfirmDeleteOpen(false);
            }}
            createdOn={ybFormatDate(deleteBundleObj.creationDate)}
            confirmDelete={() => {
              handleDeleteBundle(deleteBundleObj.bundleUUID);
              setDeleteBundleObj({});
              setIsConfirmDeleteOpen(false);
            }}
          />
        )}
        <div className="universe-support-bundle-step-three">
          {creatingBundle && (
            <div className="creating-bundle">
              <span>
                <i className="fa icon fa-spinner fa-spin" />
                Creating bundle. Depending on the size of the bundle this may take a few minutes.
              </span>
              <i onClick={() => setCreatingBundle(false)} className="fa fa-close" />
            </div>
          )}

          <div className="create-bundle">
            <YBButton
              variant="outline-dark"
              onClick={onCreateSupportBundle}
              btnText={
                <>
                  <i className="fa fa-plus create-bundle-icon" aria-hidden="true" />
                  Create Support Bundle
                </>
              }
            />
          </div>
          <div className={clsx('selection-area', { 'create-bundle-close': !creatingBundle })}>
            {supportBundles && Array.isArray(supportBundles) && (
              <BootstrapTable data={supportBundles}>
                <TableHeaderColumn
                  dataField="creationDate"
                  dataFormat={(creationDate) =>
                    ybFormatDate(creationDate, YBTimeFormats.YB_DATE_ONLY_TIMESTAMP)
                  }
                  isKey={true}
                  className={'node-name-field'}
                  columnClassName={'node-name-field'}
                >
                  Date Created
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="expirationDate"
                  dataFormat={(expirationDate) =>
                    ybFormatDate(expirationDate, YBTimeFormats.YB_DATE_ONLY_TIMESTAMP)
                  }
                  className={'yb-node-status-cell'}
                  columnClassName={'yb-node-status-cell'}
                >
                  Expiration Date
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="status"
                  dataFormat={(status) => {
                    return statusElementsIcons[status];
                  }}
                >
                  Status
                </TableHeaderColumn>

                <TableHeaderColumn
                  dataField="sizeInBytes"
                  dataFormat={(sizeInBytes) => {
                    if (sizeInBytes === 0) {
                      return '-';
                    }

                    return formatBytes(sizeInBytes);
                  }}
                  className={'node-name-field'}
                  columnClassName={'node-name-field'}
                >
                  Size
                </TableHeaderColumn>

                <TableHeaderColumn
                  dataField="bundleUUID"
                  dataFormat={(bundleUUID, row) => {
                    return getActions(
                      bundleUUID,
                      row,
                      router.push,
                      handleDeleteBundle,
                      isConfirmDeleteOpen,
                      setIsConfirmDeleteOpen,
                      handleDownloadBundle,
                      row.status === 'Running',
                      setDeleteBundleObj
                    );
                  }}
                />
              </BootstrapTable>
            )}
          </div>
        </div>
      </>
    );
  }
);
