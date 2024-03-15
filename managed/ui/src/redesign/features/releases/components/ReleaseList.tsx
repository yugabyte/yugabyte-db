import { ChangeEvent, useEffect, useState } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { DropdownButton, MenuItem, Dropdown } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';
import { Box, Divider, makeStyles } from '@material-ui/core';
import { Add, Refresh } from '@material-ui/icons';
import clsx from 'clsx';
import { ReleaseDetails } from './ReleaseDetails';
import { DeploymentStatus } from './ReleaseDeploymentStatus';
import { AddReleaseModal } from './ReleaseDialogs/AddReleaseModal';
import { EditKubernetesModal } from './ReleaseDialogs/EditArchitectureModal';
import { EditReleaseTagModal } from './ReleaseDialogs/EditReleaseTagModal';
import { DisableReleaseModal } from './ReleaseDialogs/DisableReleaseModal';
import { DeleteReleaseModal } from './ReleaseDialogs/DeleteReleaseModal';
import { YBButton, YBCheckbox } from '../../../components';
import { YBPanelItem } from '../../../../components/panels';
import { YBSearchInput } from '../../../../components/common/forms/fields/YBSearchInput';
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
import { RELEASE_LIST_DATA } from '../api/MockData';
import { RuntimeConfigKey } from '../../../helpers/constants';
import { getImportedArchitectures } from '../helpers/utils';
import { isEmptyString, isNonEmptyString } from '../../../../utils/ObjectUtils';

import UnChecked from '../../../../redesign/assets/checkbox/UnChecked.svg';
import Checked from '../../../../redesign/assets/checkbox/Checked.svg';

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
    alignSelf: 'center'
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

export const ReleaseList = () => {
  const helperClasses = useStyles();
  const { t } = useTranslation();

  const [openSidePanel, setOpenSidePanel] = useState<boolean>(false);
  const [showAllowedReleases, setShowAllowedReleases] = useState<boolean>(false);
  const [selectedReleaseDetails, setSelectedReleaseDetails] = useState<Releases | null>(null);
  const [showAddReleaseDialog, setShowAddReleaseDialog] = useState<boolean>(false);
  const [showEditReleaseTagDialog, setShowEditReleaseTagDialog] = useState<boolean>(false);
  const [showDisableReleaseDialog, setShowDisableReleaseDialog] = useState<boolean>(false);
  const [showDeleteReleaseDialog, setShowDeleteReleaseDialog] = useState<boolean>(false);
  const [showEditKubernetesDialog, setShowEditKubernetesDialog] = useState<boolean>(false);
  const [releaseArchitecture, setReleaseArchitecture] = useState<ReleasePlatformArchitecture>(
    ReleasePlatformArchitecture.X86
  );
  const releaseSupportKeys = Object.values(ReleaseType);
  const [releaseType, setReleaseType] = useState<string>(releaseSupportKeys[0]);
  const [releaseList, setReleaseList] = useState<Releases[]>([]);
  const [searchText, setSearchText] = useState<string>('');
  const [modalTitle, setModalTitle] = useState<string>(ModalTitle.ADD_RELEASE);

  useEffect(() => {
    // TODO: Make an API call to ensure it returns the list of releases - fetchReleasesList (GET) from api.ts
    setReleaseList(RELEASE_LIST_DATA);
  }, []);

  // Check if runtime config is enabled for new releases page
  const runtimeConfigsKeyMetadata = useSelector(
    (state: any) => state.customer.runtimeConfigsKeyMetadata
  );
  const isReleasesRedesignEnabled =
    runtimeConfigsKeyMetadata?.data?.configEntries?.find(
      (c: any) => c.key === RuntimeConfigKey.RELEASES_REDESIGN_UI_FEATURE_FLAG
    )?.value === 'true';

  if (!isReleasesRedesignEnabled) {
    return <></>;
  }

  const formatVersion = (cell: any, row: any) => {
    return (
      <Box className={helperClasses.flexRow}>
        <span className={helperClasses.versionText}>{row.version}</span>
        {isNonEmptyString(row.release_tag) && (
          <Box className={helperClasses.releaseTagBox}>
            <span className={clsx(helperClasses.releaseTagText, helperClasses.smallerReleaseText)}>
              {row.release_tag}
            </span>
          </Box>
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
          {row.in_use ? 'In Use' : 'Not in Use'}
        </span>
        {row.in_use && (
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
                  {t(`releases.tags.${architecture}`)}
                </span>
              </Box>
            );
          })}
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
      setReleaseArchitecture(ReleasePlatformArchitecture.KUBERNETES);
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

  const onSetReleaseArchitecture = (selectedArchitecture: ReleasePlatformArchitecture) => {
    setReleaseArchitecture(selectedArchitecture);
  };

  const onActionPerformed = () => {
    // TODO: Ensure we must call the RELEASES LIST API when we successfully
    // Add Release/Disable Release/Delete Release/Edit Architecture/Edit Release Tag
  };

  const getMenuItemsActions = (row: any) => {
    const renderedItems: any = [];
    for (const [key, value] of Object.entries(MAIN_ACTION)) {
      renderedItems.push(
        <MenuItem
          key={key}
          value={value}
          onClick={(e: any) => {
            onActionClick(value, row);
          }}
        >
          {value}
        </MenuItem>
      );
    }

    row.artifacts.map((artifact: ReleaseArtifacts) => {
      const action = isEmptyString(artifact.architecture)
        ? EDIT_ACTIONS[ReleasePlatformArchitecture.KUBERNETES]
        : EDIT_ACTIONS[artifact.architecture];
      renderedItems.push(
        <MenuItem
          key={artifact.architecture}
          value={action}
          onClick={(e: any) => {
            onActionClick(action, row);
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
      <DropdownButton title="Actions" id="release-list-actions" pullRight={false}>
        {getMenuItemsActions(row)}
      </DropdownButton>
    );
  };

  const handleAllowedReleases = (event: ChangeEvent<HTMLInputElement>) => {
    const isChecked = event.target.checked;
    setShowAllowedReleases(isChecked);
    if (isChecked) {
      const filteredData = RELEASE_LIST_DATA.filter(
        (releaseData: Releases) => releaseData.state === ReleaseState.ACTIVE
      );
      setReleaseList(filteredData);
    } else {
      setReleaseList(RELEASE_LIST_DATA);
    }
  };

  const onReleaseTypeChanged = (releaseType: any) => {
    if (releaseType === ReleaseType.ALL) {
      setReleaseList(RELEASE_LIST_DATA);
    } else {
      const filteredData = RELEASE_LIST_DATA.filter(
        (releaseData: Releases) => releaseData.release_type === releaseType
      );
      setReleaseList(filteredData);
    }
  };

  const onSearchVersions = (searchTerm: string) => {
    if (isEmptyString(searchTerm)) {
      setReleaseList(RELEASE_LIST_DATA);
    } else {
      const filteredData = RELEASE_LIST_DATA.filter((releaseData: Releases) =>
        releaseData.version.includes(searchTerm)
      );
      setReleaseList(filteredData);
    }
  };

  const onSidePanelClose = () => {
    setOpenSidePanel(false);
  };

  const onRowDoubleClick = (row: any, event: any) => {
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
                    // TODO: call onActionPerformed
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
              data={releaseList}
              pagination={true}
              options={{
                onRowDoubleClick: onRowDoubleClick
              }}
            >
              <TableHeaderColumn dataField={'uuid'} isKey={true} hidden={true} />
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
      {openSidePanel && (
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
          isAddRelease={modalTitle === ModalTitle.ADD_RELEASE}
          versionNumber={selectedReleaseDetails?.version}
        />
      )}
      {showEditReleaseTagDialog && (
        <EditReleaseTagModal
          open={showEditReleaseTagDialog}
          onActionPerformed={onActionPerformed}
          onClose={onEditReleaseTagDialogClose}
          data={selectedReleaseDetails}
        />
      )}
      {showDisableReleaseDialog && (
        <DisableReleaseModal
          open={showDisableReleaseDialog}
          onActionPerformed={onActionPerformed}
          onClose={onDisableReleaseDialogClose}
          data={selectedReleaseDetails}
        />
      )}
      {showDeleteReleaseDialog && (
        <DeleteReleaseModal
          open={showDeleteReleaseDialog}
          onActionPerformed={onActionPerformed}
          onClose={onDeleteReleaseDialogClose}
          data={selectedReleaseDetails}
        />
      )}
      {showEditKubernetesDialog && (
        <EditKubernetesModal
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
