import { useEffect, useState } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { api } from '../../../redesign/helpers/api';
import { YBLoading } from '../../common/indicators';
import { YBPanelItem } from '../../panels';
import { Link } from 'react-router';
import AlertDetails from './AlertDetails';
import { YBButton } from '../../common/forms/fields';
import { isAvailable } from '../../../utils/LayoutUtils';

import { toast } from 'react-toastify';
import { Label } from 'react-bootstrap';
import { timeFormatter } from '../../../utils/TableFormatters';
import { useSearchParam } from 'react-use';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import './AlertsTable.scss';

const DEFAULT_SORT_COLUMN = 'createTime';
const DEFAULT_SORT_DIRECTION = 'DESC';

const findValueforlabel = (labels, labelToFind) => {
  const label = labels.find((l) => l.name === labelToFind);
  return label ? label.value : '';
};

export default function AlertsTable({ filters, customer }) {
  const [page, setPage] = useState(1);
  const [limit, setLimit] = useState(10);
  const [sortType, setSortType] = useState(DEFAULT_SORT_COLUMN);
  const [sortDirection, setSortDirection] = useState(DEFAULT_SORT_DIRECTION);

  const [alertDetails, setAlertDetails] = useState(null);

  const resetPage = () => setPage(1);

  const queryClient = useQueryClient();

  const { isLoading, data, isFetching } = useQuery(
    ['alerts', (page - 1) * limit, limit, sortType, sortDirection, filters],
    () => api.getAlerts((page - 1) * limit, limit, sortType, sortDirection, filters),
    { keepPreviousData: true }
  );

  const acknowledge = useMutation(
    (alertToAcknowledge) => api.acknowledgeAlert(alertToAcknowledge.uuid),
    {
      onSuccess: async (_, variables) => {
        const resp = await api.getAlert(variables.uuid);

        queryClient.invalidateQueries('alerts');
        toast.success('Acknowledged!');
        if (alertDetails !== null) {
          setAlertDetails(resp);
        }
      },
      onError: () => {
        toast.error('Unable to acknowledge. An Error Occured!.');
      }
    }
  );

  useEffect(() => {
    resetPage();
  }, [filters.states, filters.severities, filters.configurationTypes, filters.sourceName]);

  const showDetails = useSearchParam('showDetails');
  const { isLoading: isAlertDetailsLoading } = useQuery(
    [showDetails],
    () => api.getAlert(showDetails),
    {
      enabled: showDetails !== null,
      onSuccess: (data) => {
        setAlertDetails(data);
      }
    }
  );

  if (isLoading || isAlertDetailsLoading) return <YBLoading />;

  if (!data) return 'Unable to load data at the moment. Please try again later';

  const setSortOptions = (sortType, sortDirection) => {
    resetPage();
    const sortColumn = sortType === 'labels' ? 'sourceName' : sortType;
    setSortType(sortColumn);
    setSortDirection(sortDirection.toUpperCase());
  };

  const acknowledgeAlert = () => {
    if (!alertDetails) return;

    acknowledge.mutateAsync(alertDetails);
  };
  return (
    <>
      <YBPanelItem
        className="alerts-table"
        body={
          <>
            {isFetching && <YBLoading />}
            <BootstrapTable
              data={data ? data.entities : []}
              remote={true}
              fetchInfo={{ dataTotalSize: data.totalCount }}
              options={{
                onPageChange: (page) => setPage(page),
                onSizePerPageList: setLimit,
                sizePerPage: limit,
                page: page,
                onSortChange: setSortOptions,
                defaultSortName: DEFAULT_SORT_COLUMN,
                defaultSortOrder: DEFAULT_SORT_DIRECTION.toLowerCase()
              }}
              maxHeight="500px"
              pagination={true}
            >
              <TableHeaderColumn dataField="uuid" isKey={true} hidden={true} />
              <TableHeaderColumn
                dataField="name"
                columnClassName="no-border"
                className="no-border"
                dataAlign="left"
                width={'30%'}
                dataFormat={(cell, row) => (
                  <Link
                    to="#"
                    className="errCodeLink"
                    onClick={(e) => {
                      e.preventDefault();
                      setAlertDetails(row);
                    }}
                  >
                    {cell}
                  </Link>
                )}
                options={{
                  onSortChange: setSortOptions
                }}
                dataSort
              >
                Name
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="labels"
                columnClassName="no-border"
                className="no-border"
                dataAlign="left"
                width={'30%'}
                dataFormat={(cell) => (
                  <>
                    {findValueforlabel(cell, 'source_name')}
                    <Label className="configurationTypeLabel">
                      {findValueforlabel(cell, 'configuration_type')}
                    </Label>
                  </>
                )}
                options={{
                  onSortChange: setSortOptions
                }}
                dataSort
              >
                Source
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="createTime"
                columnClassName="no-border name-column"
                className="no-border"
                dataFormat={timeFormatter}
                width={'20%'}
                dataSort
              >
                Time
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="state"
                columnClassName="no-border name-column"
                className="no-border"
                width={'20%'}
                dataSort
              >
                Status
              </TableHeaderColumn>
              {isAvailable(customer.currentCustomer.data.features, 'alert.list.actions') && (
                <TableHeaderColumn
                  dataField="message"
                  columnClassName="no-border name-column"
                  className="no-border"
                  width={'10%'}
                  tdStyle={{ whiteSpace: 'normal' }}
                  dataFormat={(_, row) => {
                    if (row.state !== 'ACTIVE') {
                      return '';
                    }
                    return (
                      <RbacValidator
                        accessRequiredOn={ApiPermissionMap.ACKNOWLEDGE_ALERT}
                        isControl
                      >
                        <YBButton
                          btnText="Acknowledge"
                          btnStyle="link"
                          btnClass="acknowledge-link-button"
                          onClick={(e) => {
                            e.preventDefault();
                            acknowledge.mutateAsync(row);
                          }}
                          data-testid="Acknowledge-Alert"
                        />
                      </RbacValidator>
                    );
                  }}
                >
                  Action
                </TableHeaderColumn>
              )}
            </BootstrapTable>
          </>
        }
      />
      <AlertDetails
        customer={customer.currentCustomer}
        alertDetails={alertDetails}
        // eslint-disable-next-line eqeqeq
        visible={alertDetails != null}
        onHide={() => {
          setAlertDetails(null);
        }}
        onAcknowledge={acknowledgeAlert}
      />
    </>
  );
}
