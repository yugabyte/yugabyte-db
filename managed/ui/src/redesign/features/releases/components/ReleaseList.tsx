import { ChangeEvent, useState } from 'react';
import { TableHeaderColumn, SortOrder } from 'react-bootstrap-table';
import { DropdownButton, MenuItem, Dropdown } from 'react-bootstrap';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Box, Divider, makeStyles } from '@material-ui/core';
import { Add, Refresh } from '@material-ui/icons';
import clsx from 'clsx';
import { ReleaseDetails } from './ReleaseDetails';
import { DeploymentStatus } from './ReleaseDeploymentStatus';
import { AddReleaseModal } from './ReleaseDialogs/AddReleaseModal';
import { EditArchitectureModal } from './ReleaseDialogs/EditArchitectureModal';
import { EditReleaseTagModal } from './ReleaseDialogs/EditReleaseTagModal';
import { ModifyReleaseStateModal } from './ReleaseDialogs/ModifyReleaseStateModal';
import { DeleteReleaseModal } from './ReleaseDialogs/DeleteReleaseModal';
import { YBButton, YBCheckbox } from '../../../components';
import { YBPanelItem } from '../../../../components/panels';
import { YBTable } from '../../../../components/common/YBTable';
import { YBSearchInput } from '../../../../components/common/forms/fields/YBSearchInput';
import { YBErrorIndicator, YBLoading } from '../../../../components/common/indicators';
import { RbacValidator } from '../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../rbac/ApiAndUserPermMapping';
import {
  ModalTitle,
  ReleaseArtifacts,
  ReleasePlatformArchitecture,
  ReleaseState,
  ReleaseType,
  Releases
} from './dtos';
import { QUERY_KEY, ReleasesAPI } from '../api';
import {
  getImportedArchitectures,
  MAX_RELEASE_TAG_CHAR,
  MAX_RELEASE_VERSION_CHAR
} from '../helpers/utils';
import { ybFormatDate, YBTimeFormats } from '../../../helpers/DateUtils';
import { isEmptyString, isNonEmptyString } from '../../../../utils/ObjectUtils';

import UnChecked from '../../../../redesign/assets/checkbox/UnChecked.svg';
import Checked from '../../../../redesign/assets/checkbox/Checked.svg';
import AddIcon from '../../../../redesign/assets/Add.svg';

const useStyles = makeStyles((theme) => ({
  releaseListBox: {
    overflow: 'unset'
  },
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
  verticalText: {
    verticalAlign: 'super'
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
    height: '24px',
    borderRadius: '6px',
    padding: '4px 6px 4px 6px',
    backgroundColor: theme.palette.grey[200],
    maxWidth: 'fit-content',
    marginLeft: theme.spacing(0.5)
  },
  releaseTagText: {
    color: theme.palette.grey[700],
    cursor: 'pointer'
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
    height: '24px',
    borderStyle: 'solid',
    backgroundColor: theme.palette.common.white,
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
    padding: '0px',
    height: theme.spacing(3)
  },
  addButtonBox: {
    height: theme.spacing(3),
    width: '26px',
    border: '1px',
    borderRadius: '6px',
    borderColor: theme.palette.grey[200]
  },
  releaseRow: {
    cursor: 'pointer'
  },
  releaseActionsDropdown: {
    width: '24px',
    height: '24px',
    border: '0px',
    padding: '0px',
    fontWeight: 'bold'
  },
  rowHover: {
    '&:hover': {
      cursor: 'pointer',
      backgroundColor: '#F3F3F3'
    }
  }
}));

const DEFAULT_SORT_COLUMN = 'version';
const DEFAULT_SORT_DIRECTION = 'DESC';

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
  DISABLE_RELEASE: 'Disallow',
  ENABLE_RELEASE: 'Allow',
  DELETE_RELEASE: 'Delete'
} as const;

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

  const refreshRelease = useMutation(() => ReleasesAPI.refreshRelease());

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
        <span
          title={row.version}
          data-testid={'ReleaseList-VersionText'}
          className={helperClasses.versionText}
        >
          {row.version.length > MAX_RELEASE_VERSION_CHAR
            ? `${row.version.substring(0, MAX_RELEASE_VERSION_CHAR)}...`
            : row.version}
        </span>
        {isNonEmptyString(row.release_tag) && (
          <>
            <Box className={helperClasses.releaseTagBox}>
              <span
                title={row.release_tag}
                data-testid={'ReleaseList-ReleaseTag'}
                className={clsx(
                  helperClasses.releaseTagText,
                  helperClasses.smallerReleaseText,
                  helperClasses.verticalText
                )}
              >
                {row.release_tag.length > MAX_RELEASE_TAG_CHAR
                  ? `${row.release_tag.substring(0, MAX_RELEASE_TAG_CHAR)}...`
                  : row.release_tag}
              </span>
            </Box>
          </>
        )}
      </Box>
    );
  };

  const formatDateMilliSecs = (cell: any, row: any) => {
    return (
      <Box className={helperClasses.biggerReleaseText}>
        {row.release_date_msecs
          ? ybFormatDate(row.release_date_msecs, YBTimeFormats.YB_DATE_ONLY_TIMESTAMP)
          : '--'}
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
                <span
                  className={clsx(helperClasses.smallerReleaseText, helperClasses.verticalText)}
                >
                  {t(`releases.tags.${architecture === null ? 'kubernetes' : architecture}`)}
                </span>
              </Box>
            );
          })}
        {row.artifacts.length < 3 && (
          <Box className={helperClasses.addButtonBox}>
            <RbacValidator accessRequiredOn={ApiPermissionMap.MODIFY_YBDB_RELEASE} isControl>
              <YBButton
                className={helperClasses.overrideMuiStartIcon}
                onClick={(e: any) => {
                  setSelectedReleaseDetails(row);
                  onNewReleaseButtonClick();
                  onSetModalTitle(ModalTitle.ADD_ARCHITECTURE);
                  e.stopPropagation();
                }}
                data-testid="ReleaseList-AddArchitectureButton"
                variant="secondary"
              >
                <img src={AddIcon} alt="add" />
              </YBButton>
            </RbacValidator>
          </Box>
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

  const onRefreshRelease = () => {
    refreshRelease.mutate();
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
          <RbacValidator accessRequiredOn={ApiPermissionMap.MODIFY_YBDB_RELEASE} isControl>
            <MenuItem
              key={key}
              value={value}
              onClick={(e: any) => {
                onActionClick(value, row);
                e.stopPropagation();
              }}
              data-testid={`ReleaseList-Action${value}`}
            >
              {value}
            </MenuItem>
          </RbacValidator>
        );
      }
    }

    row.artifacts.map((artifact: ReleaseArtifacts) => {
      const action =
        artifact.architecture === null
          ? EDIT_ACTIONS['kubernetes']
          : EDIT_ACTIONS[artifact.architecture!];
      const isDisabled = row.universes?.length > 0 || row.state === ReleaseState.DISABLED;
      renderedItems.push(
        <RbacValidator
          accessRequiredOn={ApiPermissionMap.MODIFY_YBDB_RELEASE}
          isControl
          overrideStyle={{ display: 'block' }}
        >
          <MenuItem
            key={artifact.architecture}
            value={action}
            onClick={(e: any) => {
              if (!isDisabled) {
                onActionClick(action, row);
              }
              e.stopPropagation();
            }}
            disabled={isDisabled}
            data-testid={`ReleaseList-Action${action}`}
          >
            {action}
          </MenuItem>
        </RbacValidator>
      );
    });

    for (const [key, value] of Object.entries(OTHER_ACTONS)) {
      let disabled = false;
      if (row.state === ReleaseState.ACTIVE && value === OTHER_ACTONS.ENABLE_RELEASE) {
        continue;
      }
      if (row.state === ReleaseState.DISABLED && value === OTHER_ACTONS.DISABLE_RELEASE) {
        continue;
      }

      if (value === OTHER_ACTONS.DISABLE_RELEASE || value === OTHER_ACTONS.ENABLE_RELEASE) {
        renderedItems.push(<Divider />);
      }

      if (
        row.universes?.length > 0 &&
        (value === OTHER_ACTONS.DISABLE_RELEASE || value === OTHER_ACTONS.DELETE_RELEASE)
      ) {
        disabled = true;
      }

      renderedItems.push(
        <RbacValidator
          accessRequiredOn={
            value === OTHER_ACTONS.DELETE_RELEASE
              ? ApiPermissionMap.DELETE_YBDB_RELEASE
              : ApiPermissionMap.MODIFY_YBDB_RELEASE
          }
          isControl
          overrideStyle={{ display: 'block' }}
        >
          <MenuItem
            key={key}
            value={value}
            onClick={(e: any) => {
              if (!disabled) {
                onActionClick(value, row);
              }
              e.stopPropagation();
            }}
            disabled={disabled}
            data-testid={`ReleaseList-Action${value}`}
          >
            {value}
          </MenuItem>
        </RbacValidator>
      );
    }

    return renderedItems;
  };

  const formatActionButtons = (cell: any, row: any) => {
    return (
      <DropdownButton
        noCaret
        key={`release-list-actions-${row.release_uuid}`}
        title="..."
        className={helperClasses.releaseActionsDropdown}
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
    <Box ml={3} mr={3} className={helperClasses.releaseListBox}>
      <YBPanelItem
        header={
          <Box mt={2}>
            <Box className={clsx(helperClasses.floatBoxLeft, helperClasses.flexRow)} mt={2}>
              <YBSearchInput
                data-testid="ReleaseList-SearchReleaseVersion"
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
                <RbacValidator accessRequiredOn={ApiPermissionMap.MODIFY_YBDB_RELEASE} isControl>
                  <YBButton
                    className={clsx(
                      helperClasses.refreshButton,
                      helperClasses.overrideMuiRefreshIcon
                    )}
                    data-testid="ReleaseList-RefreshReleasesButton"
                    size="large"
                    startIcon={<Refresh />}
                    variant="secondary"
                    onClick={() => {
                      onActionPerformed();
                      onRefreshRelease();
                    }}
                  />
                </RbacValidator>
                <RbacValidator accessRequiredOn={ApiPermissionMap.CREATE_YBDB_RELEASE} isControl>
                  <YBButton
                    size="large"
                    variant={'primary'}
                    data-testid="ReleaseList-AddReleaseButton"
                    startIcon={<Add />}
                    onClick={onNewReleaseButtonClick}
                  >
                    {t('releases.newRelease')}
                  </YBButton>
                </RbacValidator>
              </Box>
            </Box>
            <h2 className="content-title">{t('releases.headerTitle')}</h2>
          </Box>
        }
        body={
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.GET_YBDB_RELEASES}
            isControl
            overrideStyle={{
              float: 'right'
            }}
          >
            <Box>
              <YBTable
                data={filteredReleaseList}
                pagination={true}
                options={{
                  defaultSortOrder: DEFAULT_SORT_DIRECTION.toLowerCase() as SortOrder,
                  defaultSortName: DEFAULT_SORT_COLUMN,
                  onRowClick: onRowClick
                }}
                trClassName={helperClasses.rowHover}
              >
                <TableHeaderColumn dataField={'release_uuid'} isKey={true} hidden={true} />
                <TableHeaderColumn
                  width="15%"
                  tdStyle={{ verticalAlign: 'middle' }}
                  dataField={'version'}
                  dataFormat={formatVersion}
                  dataSort
                >
                  {t('releases.version')}
                </TableHeaderColumn>
                <TableHeaderColumn
                  width="10%"
                  tdStyle={{ verticalAlign: 'middle' }}
                  dataFormat={formatDateMilliSecs}
                  dataField={'release_date_msecs'}
                  dataSort
                >
                  {t('releases.releaseDate')}
                </TableHeaderColumn>
                <TableHeaderColumn
                  width="10%"
                  tdStyle={{ verticalAlign: 'middle' }}
                  dataField={'release_type'}
                  dataFormat={formatReleaseSupport}
                  dataSort
                >
                  {t('releases.releaseSupport')}
                </TableHeaderColumn>
                <TableHeaderColumn
                  width="10%"
                  tdStyle={{ verticalAlign: 'middle' }}
                  dataFormat={formatUsage}
                >
                  {t('releases.releaseUsage')}
                </TableHeaderColumn>
                <TableHeaderColumn
                  width="15%"
                  tdStyle={{ verticalAlign: 'middle' }}
                  dataFormat={formatImportedArchitecture}
                >
                  {t('releases.importArchitecture')}
                </TableHeaderColumn>
                <TableHeaderColumn
                  width="10%"
                  tdStyle={{ verticalAlign: 'middle' }}
                  dataFormat={formatDeploymentStatus}
                  dataField={'state'}
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
              </YBTable>
            </Box>
          </RbacValidator>
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
        <ModifyReleaseStateModal
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
