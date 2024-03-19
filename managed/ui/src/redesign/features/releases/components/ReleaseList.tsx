import { ChangeEvent, useState } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { DropdownButton, MenuItem, Dropdown } from 'react-bootstrap';
import { useQuery, useQueryClient } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Box, Divider, Tooltip, makeStyles } from '@material-ui/core';
import { Add, Refresh } from '@material-ui/icons';
import clsx from 'clsx';
import { ReleaseDetails } from './ReleaseDetails';
import { DeploymentStatus } from './ReleaseDeploymentStatus';
import { AddReleaseModal } from './ReleaseDialogs/AddReleaseModal';
import { EditArchitectureModal } from './ReleaseDialogs/EditArchitectureModal';
import { EditReleaseTagModal } from './ReleaseDialogs/EditReleaseTagModal';
import { DisableReleaseModal } from './ReleaseDialogs/DisableReleaseModal';
import { DeleteReleaseModal } from './ReleaseDialogs/DeleteReleaseModal';
import { YBButton, YBCheckbox } from '../../../components';
import { YBPanelItem } from '../../../../components/panels';
import { YBSearchInput } from '../../../../components/common/forms/fields/YBSearchInput';
import { RbacValidator } from '../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../rbac/ApiAndUserPermMapping';
import { YBErrorIndicator, YBLoading } from '../../../../components/common/indicators';
import {
  ModalTitle,
  ReleaseArtifacts,
  ReleasePlatformArchitecture,
  ReleaseState,
  ReleaseType,
  Releases
} from './dtos';
import { QUERY_KEY, ReleasesAPI } from '../api';
import { getImportedArchitectures } from '../helpers/utils';
import { isEmptyString, isNonEmptyString } from '../../../../utils/ObjectUtils';

import UnChecked from '../../../../redesign/assets/checkbox/UnChecked.svg';
import Checked from '../../../../redesign/assets/checkbox/Checked.svg';
import InfoMessageIcon from '../../../../redesign/assets/info-message.svg';

const useStyles = makeStyles((theme) => ({
  biggerReleaseText: {
    fontWeight: 400,
    fontFamily: 'Inter',
    fontSize: '13px',
    alignSelf: 'center'
  },
  smallerReleaseText: {
    fontWeight: 400,
    fontFamily: 'Inter',
    fontSize: '11.5px',
    color: theme.palette.grey[900],
    alignSelf: 'center'
  },
  versionText: {
    alignSelf: 'center',
    overflow: 'visible !important'
  },
  alignText: {
    alignItems: 'center'
  },
  releaseTagBox: {
    border: '1px',
    borderRadius: '6px',
    padding: '4px 6px 4px 6px',
    backgroundColor: theme.palette.grey[200],
    maxWidth: 'fit-content',
    marginLeft: theme.spacing(0.5)
  },
  releaseTagText: {
    color: theme.palette.grey[700]
  },
  flexRow: {
    display: 'flex',
    flexDirection: 'row'
  },
  floatBoxLeft: {
    float: 'left'
  },
  floatBoxRight: {
    float: 'right'
  },
  releaseNumUniversesBox: {
    border: '1px',
    borderRadius: '6px',
    padding: '4px 6px 4px 6px',
    marginLeft: theme.spacing(0.5),
    gap: '4px',
    borderStyle: 'solid',
    borderColor: theme.palette.grey[300]
  },
  importedArchitectureBox: {
    border: '1px',
    borderRadius: '6px',
    padding: '4px 6px 4px 6px',
    gap: '4px',
    marginRight: theme.spacing(0.5),
    height: '28px',
    borderStyle: 'solid',
    borderColor: theme.palette.grey[300]
  },
  dropdownValueBox: {
    border: '1px',
    borderRadius: '6px',
    padding: '5px',
    backgroundColor: theme.palette.ybacolors.backgroundGrayDark
  },
  searchInput: {
    minWidth: '384px',
    padding: '0px 8px 0px 8px'
  },
  dropdownValue: {
    fontWeight: 500,
    fontFamily: 'Inter',
    fontSize: '12px'
  },
  checkBox: {
    marginLeft: theme.spacing(1)
  },
  refreshButton: {
    marginRight: theme.spacing(2)
  },
  overrideMuiRefreshIcon: {
    '& .MuiButton-startIcon': {
      marginLeft: 0,
      marginRight: 0
    }
  },
  overrideDropdown: {
    marginLeft: theme.spacing(2),
    '& .btn-default .caret': {
      alignSelf: 'center',
      marginLeft: theme.spacing(1)
    },
    '& .btn': {
      padding: '5px'
    }
  },
  overrideMuiStartIcon: {
    height: '28px',
    '& .MuiButton-startIcon': {
      marginLeft: 0,
      marginRight: 0
    }
  }
}));

const MAIN_ACTION = {
  ADD_ARCHITECTURE: 'Add Architecture'
} as const;

const EDIT_ACTIONS = {
  aarch64: 'Edit VM ARM',
  kubernetes: 'Edit Kubernetes',
  x86_64: 'Edit VM X86'
} as const;

const OTHER_ACTONS = {
  EDIT_RELEASE_TAG: 'Edit Release Tag',
  DISABLE_RELEASE: 'Disable',
  ENABLE_RELEASE: 'Enable',
  DELETE_RELEASE: 'Delete'
} as const;

const MAX_RELEASE_TAG_CHAR = 10;

export const NewReleaseList = () => {
  const helperClasses = useStyles();
  const { t } = useTranslation();
  const queryClient = useQueryClient();

  const [openSidePanel, setOpenSidePanel] = useState<boolean>(false);
  const [showAllowedReleases, setShowAllowedReleases] = useState<boolean>(false);
  const [selectedReleaseDetails, setSelectedReleaseDetails] = useState<Releases | null>(null);
  const [showAddReleaseDialog, setShowAddReleaseDialog] = useState<boolean>(false);
  const [showEditReleaseTagDialog, setShowEditReleaseTagDialog] = useState<boolean>(false);
  const [showDisableReleaseDialog, setShowDisableReleaseDialog] = useState<boolean>(false);
  const [showDeleteReleaseDialog, setShowDeleteReleaseDialog] = useState<boolean>(false);
  const [showEditKubernetesDialog, setShowEditKubernetesDialog] = useState<boolean>(false);
  const [
    releaseArchitecture,
    setReleaseArchitecture
  ] = useState<ReleasePlatformArchitecture | null>(ReleasePlatformArchitecture.X86);
  const releaseSupportKeys = Object.values(ReleaseType);
  const [releaseType, setReleaseType] = useState<string>(releaseSupportKeys[0]);
  const [releaseList, setReleaseList] = useState<Releases[]>([]);
  const [filteredReleaseList, setFilteredReleaseList] = useState<Releases[]>([]);
  const [searchText, setSearchText] = useState<string>('');
  const [modalTitle, setModalTitle] = useState<string>(ModalTitle.ADD_RELEASE);

  const {
    data: releasesData,
    isIdle: isReleasesIdle,
    isLoading: isReleasesLoading,
    isError: isReleasesFetchError,
    refetch: releasesRefetch
  } = useQuery(QUERY_KEY.fetchReleasesList, () => ReleasesAPI.fetchReleasesList(), {
    onSuccess: (data: Releases[]) => {
      setReleaseList(data);
      setFilteredReleaseList(data);
    }
  });

  if (isReleasesFetchError) {
    return <YBErrorIndicator customErrorMessage={t('releases.genericError')} />;
  }
  if (isReleasesLoading || (isReleasesIdle && releasesData === undefined)) {
    return <YBLoading />;
  }

  const formatVersion = (cell: any, row: any) => {
    return (
      <Box className={clsx(helperClasses.flexRow, helperClasses.alignText)}>
        <span className={helperClasses.versionText}>{row.version}</span>
        {isNonEmptyString(row.release_tag) && (
          <>
            <Box className={helperClasses.releaseTagBox}>
              <span
                className={clsx(helperClasses.releaseTagText, helperClasses.smallerReleaseText)}
              >
                {row.release_tag.length > MAX_RELEASE_TAG_CHAR
                  ? `${row.release_tag.substring(0, 10)}...`
                  : row.release_tag}
              </span>
            </Box>
            <span>
              {row.release_tag.length > MAX_RELEASE_TAG_CHAR && (
                <Tooltip title={row.release_tag} arrow placement="top">
                  <img src={InfoMessageIcon} alt="info" />
                </Tooltip>
              )}
            </span>
          </>
        )}
      </Box>
    );
  };

  const formatReleaseSupport = (cell: any, row: any) => {
    return (
      <Box className={helperClasses.biggerReleaseText}>
        {isEmptyString(row.release_type) ? '--' : t(`releases.type.${row.release_type}`)}
      </Box>
    );
  };

  const formatUsage = (cell: any, row: any) => {
    return (
      <Box className={helperClasses.flexRow}>
        <span className={helperClasses.biggerReleaseText}>
          {row.universes?.length > 0 ? 'In Use' : 'Not in Use'}
        </span>
        {row.universes?.length > 0 && (
          <Box className={helperClasses.releaseNumUniversesBox}>
            <span className={helperClasses.smallerReleaseText}>{row.universes.length}</span>
          </Box>
        )}
      </Box>
    );
  };

  const formatImportedArchitecture = (cell: any, row: any) => {
    const architectures = getImportedArchitectures(row.artifacts);
    return (
      <Box className={helperClasses.flexRow}>
        {architectures.length > 0 &&
          architectures.map((architecture: string) => {
            return (
              <Box className={helperClasses.importedArchitectureBox}>
                <span className={helperClasses.smallerReleaseText}>
                  {t(`releases.tags.${architecture === null ? 'kubernetes' : architecture}`)}
                </span>
              </Box>
            );
          })}
        {row.artifacts.length < 3 && (
          <YBButton
            className={helperClasses.overrideMuiStartIcon}
            onClick={() => {
              setSelectedReleaseDetails(row);
              onNewReleaseButtonClick();
              onSetModalTitle(ModalTitle.ADD_ARCHITECTURE);
            }}
            startIcon={<Add />}
            variant="secondary"
          ></YBButton>
        )}
      </Box>
    );
  };

  const formatDeploymentStatus = (cell: any, row: any) => {
    return <DeploymentStatus data={row} />;
  };

  // Possible actions for each release
  const onActionClick = (action: any, releaseDetails: Releases) => {
    setSelectedReleaseDetails(releaseDetails);
    if (action === MAIN_ACTION.ADD_ARCHITECTURE) {
      onNewReleaseButtonClick();
      onSetModalTitle(ModalTitle.ADD_ARCHITECTURE);
    }
    if (action === OTHER_ACTONS.EDIT_RELEASE_TAG) {
      onEditReleaseTagButtonClick();
    }
    if (action === OTHER_ACTONS.DISABLE_RELEASE || action === OTHER_ACTONS.ENABLE_RELEASE) {
      onDisableReleaseButtonClick();
    }
    if (action === OTHER_ACTONS.DELETE_RELEASE) {
      onDeleteReleaseButtonClick();
    }
    if (action === EDIT_ACTIONS.kubernetes) {
      onSetModalTitle(ModalTitle.EDIT_KUBERNETES);
      setReleaseArchitecture(null);
      onEditArchitectureClick();
    }
    if (action === EDIT_ACTIONS.aarch64) {
      onSetModalTitle(ModalTitle.EDIT_AARCH);
      setReleaseArchitecture(ReleasePlatformArchitecture.ARM);
      onEditArchitectureClick();
    }
    if (action === EDIT_ACTIONS.x86_64) {
      onSetModalTitle(ModalTitle.EDIT_X86);
      setReleaseArchitecture(ReleasePlatformArchitecture.X86);
      onEditArchitectureClick();
    }
  };

  const onSetReleaseArchitecture = (selectedArchitecture: ReleasePlatformArchitecture | null) => {
    setReleaseArchitecture(selectedArchitecture);
  };

  const onActionPerformed = () => {
    const getLatestReleasesList = async () => {
      queryClient.invalidateQueries(QUERY_KEY.fetchReleasesList);
      await releasesRefetch();
    };
    getLatestReleasesList();
  };

  const getMenuItemsActions = (row: any) => {
    const renderedItems: any = [];
    if (row.artifacts.length < 3) {
      for (const [key, value] of Object.entries(MAIN_ACTION)) {
        renderedItems.push(
          <MenuItem
            key={key}
            value={value}
            onClick={(e: any) => {
              onActionClick(value, row);
              e.stopPropagation();
            }}
          >
            {value}
          </MenuItem>
        );
      }
    }

    row.artifacts.map((artifact: ReleaseArtifacts) => {
      const action =
        artifact.architecture === null
          ? EDIT_ACTIONS['kubernetes']
          : EDIT_ACTIONS[artifact.architecture!];
      renderedItems.push(
        <MenuItem
          key={artifact.architecture}
          value={action}
          onClick={(e: any) => {
            onActionClick(action, row);
            e.stopPropagation();
          }}
        >
          {action}
        </MenuItem>
      );
    });

    for (const [key, value] of Object.entries(OTHER_ACTONS)) {
      if (row.state === ReleaseState.ACTIVE && value === OTHER_ACTONS.ENABLE_RELEASE) {
        continue;
      }
      if (row.state === ReleaseState.DISABLED && value === OTHER_ACTONS.DISABLE_RELEASE) {
        continue;
      }

      if (value === OTHER_ACTONS.DISABLE_RELEASE || value === OTHER_ACTONS.ENABLE_RELEASE) {
        renderedItems.push(<Divider />);
      }

      renderedItems.push(
        <MenuItem
          key={key}
          value={value}
          onClick={(e: any) => {
            onActionClick(value, row);
            e.stopPropagation();
          }}
        >
          {value}
        </MenuItem>
      );
    }

    return renderedItems;
  };

  const formatActionButtons = (cell: any, row: any) => {
    return (
      <DropdownButton
        title="Actions"
        id="release-list-actions"
        pullRight={false}
        onClick={(e) => e.stopPropagation()}
      >
        {getMenuItemsActions(row)}
      </DropdownButton>
    );
  };

  const handleAllowedReleases = (event: ChangeEvent<HTMLInputElement>) => {
    const isChecked = event.target.checked;
    setShowAllowedReleases(isChecked);
    const copyReleaseList = JSON.parse(JSON.stringify(releaseList));

    if (isChecked) {
      const filteredData = copyReleaseList.filter(
        (releaseData: Releases) => releaseData.state === ReleaseState.ACTIVE
      );

      setFilteredReleaseList(filteredData);
    } else {
      setFilteredReleaseList(releaseList);
    }
  };

  const onReleaseTypeChanged = (releaseType: any) => {
    if (releaseType === ReleaseType.ALL) {
      setFilteredReleaseList(releaseList);
    } else {
      const copyReleaseList = JSON.parse(JSON.stringify(releaseList));
      const filteredData = copyReleaseList.filter(
        (releaseData: Releases) => releaseData.release_type === releaseType
      );
      setFilteredReleaseList(filteredData);
    }
  };

  const onSearchVersions = (searchTerm: string) => {
    if (isEmptyString(searchTerm)) {
      setFilteredReleaseList(releaseList);
    } else {
      const copyReleaseList = JSON.parse(JSON.stringify(releaseList));
      const filteredData = copyReleaseList.filter((releaseData: Releases) =>
        releaseData.version.includes(searchTerm)
      );
      setFilteredReleaseList(filteredData);
    }
  };

  const onSidePanelClose = () => {
    setOpenSidePanel(false);
  };

  const onRowClick = (row: any, event: any) => {
    setSelectedReleaseDetails(row);
    setOpenSidePanel(true);
  };

  const onNewReleaseButtonClick = () => {
    onSetModalTitle(ModalTitle.ADD_RELEASE);
    setShowAddReleaseDialog(true);
  };

  const onEditReleaseTagButtonClick = () => {
    setShowEditReleaseTagDialog(true);
  };

  const onEditReleaseTagDialogClose = () => {
    setShowEditReleaseTagDialog(false);
  };

  const onDeleteReleaseButtonClick = () => {
    setShowDeleteReleaseDialog(true);
  };

  const onDeleteReleaseDialogClose = () => {
    setShowDeleteReleaseDialog(false);
  };

  const onEditArchitectureClick = () => {
    setShowEditKubernetesDialog(true);
  };

  const onEditArchitectureDialogClose = () => {
    setShowEditKubernetesDialog(false);
  };

  const onDisableReleaseButtonClick = () => {
    setShowDisableReleaseDialog(true);
  };

  const onDisableReleaseDialogClose = () => {
    setShowDisableReleaseDialog(false);
  };

  const onSetModalTitle = (modalTitle: string) => {
    setModalTitle(modalTitle);
  };

  const onNewReleaseDialogClose = () => {
    setShowAddReleaseDialog(false);
  };

  return (
    <Box ml={3} mr={3}>
      <YBPanelItem
        header={
          <Box mt={2}>
            <Box className={clsx(helperClasses.floatBoxLeft, helperClasses.flexRow)} mt={2}>
              <YBSearchInput
                className={helperClasses.searchInput}
                defaultValue={searchText}
                onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
                  setSearchText(e.target.value);
                  onSearchVersions(e.target.value);
                }}
                placeHolder={t('releases.searchVersions')}
              />
              <Dropdown id="release-type-dropdown" className={helperClasses.overrideDropdown}>
                <Dropdown.Toggle className={helperClasses.flexRow}>
                  <Box className={helperClasses.flexRow}>
                    <span className={helperClasses.biggerReleaseText}>
                      {t('releases.releaseType')}
                    </span>
                    <Box className={helperClasses.dropdownValueBox} ml={1}>
                      <span className={helperClasses.dropdownValue}>{releaseType}</span>
                    </Box>
                  </Box>
                </Dropdown.Toggle>
                <Dropdown.Menu>
                  {releaseSupportKeys.map((releaseSupport: ReleaseType, supportIdx: number) => {
                    return (
                      <MenuItem
                        eventKey={`releaseSupport-${supportIdx}`}
                        key={releaseSupport}
                        active={releaseType === releaseSupport}
                        onSelect={() => {
                          setReleaseType(releaseSupport);
                          onReleaseTypeChanged(releaseSupport);
                        }}
                      >
                        <span className={helperClasses.biggerReleaseText}>{releaseSupport}</span>
                      </MenuItem>
                    );
                  })}
                </Dropdown.Menu>
              </Dropdown>

              <YBCheckbox
                className={helperClasses.checkBox}
                label={t('releases.allowedReleases')}
                onChange={handleAllowedReleases}
                checked={showAllowedReleases}
                icon={<img src={UnChecked} alt="unchecked" />}
                checkedIcon={<img src={Checked} alt="checked" />}
              />
            </Box>
            <Box className={clsx(helperClasses.floatBoxRight, helperClasses.flexRow)}>
              <Box mt={2}>
                <YBButton
                  className={clsx(
                    helperClasses.refreshButton,
                    helperClasses.overrideMuiRefreshIcon
                  )}
                  size="large"
                  startIcon={<Refresh />}
                  variant="secondary"
                  onClick={() => {
                    onActionPerformed();
                  }}
                />
                <YBButton
                  size="large"
                  variant={'primary'}
                  startIcon={<Add />}
                  onClick={onNewReleaseButtonClick}
                >
                  {t('releases.newRelease')}
                </YBButton>
                <RbacValidator
                  accessRequiredOn={ApiPermissionMap.CREATE_RELEASE}
                  isControl
                  overrideStyle={{
                    float: 'right'
                  }}
                >
                  <></>
                </RbacValidator>
              </Box>
            </Box>
            <h2 className="content-title">{t('releases.headerTitle')}</h2>
          </Box>
        }
        body={
          <Box>
            <BootstrapTable
              data={filteredReleaseList}
              pagination={true}
              options={{
                onRowClick: onRowClick
              }}
            >
              <TableHeaderColumn dataField={'release_uuid'} isKey={true} hidden={true} />
              <TableHeaderColumn
                width="10%"
                tdStyle={{ verticalAlign: 'middle' }}
                dataFormat={formatVersion}
                dataSort
              >
                {t('releases.version')}
              </TableHeaderColumn>
              <TableHeaderColumn
                width="10%"
                tdStyle={{ verticalAlign: 'middle' }}
                dataField={'release_date'}
                dataSort
              >
                {t('releases.releaseDate')}
              </TableHeaderColumn>
              <TableHeaderColumn
                width="10%"
                tdStyle={{ verticalAlign: 'middle' }}
                dataFormat={formatReleaseSupport}
                dataSort
              >
                {t('releases.releaseSupport')}
              </TableHeaderColumn>
              <TableHeaderColumn
                width="10%"
                tdStyle={{ verticalAlign: 'middle' }}
                dataFormat={formatUsage}
                dataSort
              >
                {t('releases.releaseUsage')}
              </TableHeaderColumn>
              <TableHeaderColumn
                width="20%"
                tdStyle={{ verticalAlign: 'middle' }}
                dataFormat={formatImportedArchitecture}
              >
                {t('releases.importArchitecture')}
              </TableHeaderColumn>
              <TableHeaderColumn
                width="10%"
                tdStyle={{ verticalAlign: 'middle' }}
                dataFormat={formatDeploymentStatus}
                dataSort
              >
                {t('releases.releaseDeployment')}
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField={'actions'}
                columnClassName={'yb-actions-cell'}
                width="15%"
                dataFormat={formatActionButtons}
              >
                Actions
              </TableHeaderColumn>
            </BootstrapTable>
          </Box>
        }
      />
      {openSidePanel && selectedReleaseDetails && (
        <ReleaseDetails
          data={selectedReleaseDetails}
          onSidePanelClose={onSidePanelClose}
          onNewReleaseButtonClick={onNewReleaseButtonClick}
          onDeleteReleaseButtonClick={onDeleteReleaseButtonClick}
          onDisableReleaseButtonClick={onDisableReleaseButtonClick}
          onEditArchitectureClick={onEditArchitectureClick}
          onSetModalTitle={onSetModalTitle}
          onSetReleaseArchitecture={onSetReleaseArchitecture}
        />
      )}
      {showAddReleaseDialog && (
        <AddReleaseModal
          open={showAddReleaseDialog}
          onClose={onNewReleaseDialogClose}
          onActionPerformed={onActionPerformed}
          modalTitle={modalTitle}
          data={selectedReleaseDetails}
          isAddRelease={modalTitle === ModalTitle.ADD_RELEASE}
          versionNumber={selectedReleaseDetails?.version}
        />
      )}
      {showEditReleaseTagDialog && selectedReleaseDetails && (
        <EditReleaseTagModal
          open={showEditReleaseTagDialog}
          onActionPerformed={onActionPerformed}
          onClose={onEditReleaseTagDialogClose}
          data={selectedReleaseDetails}
        />
      )}
      {showDisableReleaseDialog && selectedReleaseDetails && (
        <DisableReleaseModal
          open={showDisableReleaseDialog}
          onActionPerformed={onActionPerformed}
          onClose={onDisableReleaseDialogClose}
          data={selectedReleaseDetails}
        />
      )}
      {showDeleteReleaseDialog && selectedReleaseDetails && (
        <DeleteReleaseModal
          open={showDeleteReleaseDialog}
          onActionPerformed={onActionPerformed}
          onClose={onDeleteReleaseDialogClose}
          data={selectedReleaseDetails}
        />
      )}
      {showEditKubernetesDialog && selectedReleaseDetails && (
        <EditArchitectureModal
          open={showEditKubernetesDialog}
          onActionPerformed={onActionPerformed}
          onClose={onEditArchitectureDialogClose}
          data={selectedReleaseDetails}
          modalTitle={modalTitle}
          releaseArchitecture={releaseArchitecture}
        />
      )}
    </Box>
  );
};
