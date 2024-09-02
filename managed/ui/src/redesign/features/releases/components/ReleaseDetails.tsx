import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Link } from 'react-router';
import clsx from 'clsx';
import { Box, Typography, makeStyles, Tab, useTheme } from '@material-ui/core';
import { Add } from '@material-ui/icons';
import { TabContext, TabList, TabPanel } from '@material-ui/lab';
import { DeploymentStatus } from './ReleaseDeploymentStatus';
import { ImportedArchitecture } from './ImportedArchitecture';
import { InUseUniverses } from './InUseUniverses';
import { YBButton } from '../../../components';
import { hasNecessaryPerm } from '../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../rbac/ApiAndUserPermMapping';
import { ModalTitle, ReleasePlatformArchitecture, ReleaseState, Releases } from './dtos';
import { ybFormatDate, YBTimeFormats } from '../../../helpers/DateUtils';
import { isNonEmptyString } from '../../../../utils/ObjectUtils';

import { ReactComponent as Delete } from '../../../../redesign/assets/trashbin.svg';
import { MAX_RELEASE_TAG_CHAR, MAX_RELEASE_VERSION_CHAR } from '../helpers/utils';

const DOCS_LINK = 'https://docs.yugabyte.com/preview/releases/yba-releases/';

const useStyles = makeStyles((theme) => ({
  sidePanel: {
    backgroundColor: theme.palette.ybacolors.backgroundGrayLightest,
    height: '100%',
    width: '50%',
    position: 'fixed',
    zIndex: 1110,
    top: 0,
    right: 0,
    border: `1px solid #E3E3E5`,
    maxHeight: '100%',
    transition: 'right 0.5s ease-in-out',
    boxShadow: `inset 4px 0 0 0 rgba(0, 0, 0, 0.1)`
  },
  header: {
    display: 'flex',
    alignItems: 'center',
    padding: `${theme.spacing(2)}px ${theme.spacing(3)}px`,
    background: theme.palette.common.white,
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    boxShadow: `inset 4px 0 0 0 rgba(0, 0, 0, 0.1)`
  },
  headerIcon: {
    marginLeft: 'auto',
    fontSize: '16px'
  },
  floatBoxRight: {
    float: 'right'
  },
  flexRow: {
    display: 'flex',
    flexDirection: 'row'
  },
  flexColumn: {
    display: 'flex',
    flexDirection: 'column'
  },
  releaseDetailsBox: {
    border: '1px',
    borderRadius: '4px',
    justifyContent: 'space-between',
    padding: '16px 20px 16px 20px',
    backgroundColor: theme.palette.ybacolors.backgroundGrayLight,
    borderColor: '#E3E3E5',
    marginTop: theme.spacing(10),
    height: '76px'
  },
  releaseMetadataBox: {
    justifyContent: 'space-between'
  },
  deleteButton: {
    marginRight: theme.spacing(2)
  },
  releaseMetadataValue: {
    marginTop: theme.spacing(0.5)
  },
  overrideMuiTabs: {
    borderBottom: `1px solid ${theme.palette.grey[200]}`,
    '& .MuiButtonBase-root': {
      fontWeight: 400,
      fontFamily: 'Inter',
      fontSize: '13px',
      color: theme.palette.ybacolors.labelBackground
    }
  },
  noTabBorder: {
    borderBottom: 'unset'
  },
  tab: {
    border: '1px',
    borderRadius: '4px 4px 0 0',
    borderColor: theme.palette.grey[300],
    borderStyle: 'solid',
    marginRight: theme.spacing(0.5),
    padding: '6px 12px 6px 12px !important'
  },
  tabLabel: {
    display: 'flex',
    flexDirection: 'row',
    alignItems: 'center'
  },
  unfocusedTab: {
    backgroundColor: theme.palette.ybacolors.ybBorderGray
  },
  tabPanel: {
    border: '1px',
    borderRadius: '0 8px 8px 8px',
    padding: '0px',
    borderColor: '#DDE1E6',
    borderStyle: 'solid'
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
  verticalText: {
    verticalAlign: 'super'
  },
  smallerReleaseText: {
    fontWeight: 400,
    fontFamily: 'Inter',
    fontSize: '11.5px',
    color: theme.palette.grey[900],
    alignSelf: 'center'
  },
  importedArchitectureBox: {
    backgroundColor: theme.palette.common.white
  }
}));

const ReleaseDetailsTab = {
  IMPORTED_ARCHITECTURE: 'Imported Architecture',
  IN_USE_UNIVERSES: 'In-Use Universes'
} as const;
type ReleaseDetailsTab = typeof ReleaseDetailsTab[keyof typeof ReleaseDetailsTab];

const DEFAULT_TAB = ReleaseDetailsTab.IMPORTED_ARCHITECTURE;

interface ReleaseDetailsProps {
  data: Releases;
  onSidePanelClose: () => void;
  onNewReleaseButtonClick: () => void;
  onEditArchitectureClick: () => void;
  onDeleteReleaseButtonClick: () => void;
  onDisableReleaseButtonClick: () => void;
  onSetReleaseArchitecture: (selectedArchitecture: ReleasePlatformArchitecture | null) => void;
  onSetModalTitle: (modalTitle: string) => void;
}

const releaseDetailsMap = {
  version: 'version',
  support: 'release_type',
  releaseDate: 'release_date_msecs',
  releaseNote: 'release_notes',
  status: 'state'
};

export const ReleaseDetails = ({
  data,
  onSidePanelClose,
  onNewReleaseButtonClick,
  onEditArchitectureClick,
  onDeleteReleaseButtonClick,
  onSetReleaseArchitecture,
  onDisableReleaseButtonClick,
  onSetModalTitle
}: ReleaseDetailsProps) => {
  const helperClasses = useStyles();
  const theme = useTheme();
  const { t } = useTranslation();

  const [currentTab, setCurrentTab] = useState<ReleaseDetailsTab>(DEFAULT_TAB);

  const formatReleaseDetails = (releaseDetailsKey: string) => {
    const key = releaseDetailsMap[releaseDetailsKey];
    let value = data?.[key];
    if (!value) {
      value = '';
    }

    if (key === releaseDetailsMap.releaseNote) {
      return (
        <Box className={helperClasses.releaseMetadataValue}>
          <Link target="_blank" to={DOCS_LINK}>
            <span data-testid={`ReleaseDetails-ReleaseNote`}>{t('releases.releaseNote')}</span>
          </Link>
        </Box>
      );
    }
    if (key === releaseDetailsMap.status) {
      return <DeploymentStatus data={data} />;
    }
    if (key === releaseDetailsMap.version) {
      return (
        <Box className={helperClasses.flexRow}>
          <Box
            className={helperClasses.releaseMetadataValue}
            data-testid={'ReleaseDetails-ReleaseVersion'}
          >
            {value.length > MAX_RELEASE_VERSION_CHAR
              ? `${value.substring(0, MAX_RELEASE_VERSION_CHAR)}...`
              : value.version}
            <span title={value}>{value}</span>
          </Box>
          {isNonEmptyString(data.release_tag) && (
            <>
              <Box className={helperClasses.releaseTagBox}>
                <span
                  title={data.release_tag}
                  data-testid={'ReleaseDetails-ReleaseTag'}
                  className={clsx(
                    helperClasses.releaseTagText,
                    helperClasses.smallerReleaseText,
                    helperClasses.verticalText
                  )}
                >
                  {data.release_tag.length > MAX_RELEASE_TAG_CHAR
                    ? `${data.release_tag.substring(0, MAX_RELEASE_TAG_CHAR)}...`
                    : data.release_tag}
                </span>
              </Box>
            </>
          )}
        </Box>
      );
    }
    if (key === releaseDetailsMap.support) {
      return (
        <Box
          className={helperClasses.releaseMetadataValue}
          data-testid={'ReleaseDetails-ReleaseSupport'}
        >
          {t(`releases.type.${value}`)}
        </Box>
      );
    }

    // Release Date
    return (
      <Box
        className={helperClasses.releaseMetadataValue}
        data-testid={'ReleaseDetails-ReleaseMetadata'}
      >
        {value ? ybFormatDate(value, YBTimeFormats.YB_DATE_ONLY_TIMESTAMP) : '-'}
      </Box>
    );
  };

  const handleTabChange = (_event: React.ChangeEvent<{}>, newTab: ReleaseDetailsTab) => {
    setCurrentTab(newTab);
  };

  return (
    <Box className={helperClasses.sidePanel}>
      <Box className={helperClasses.header}>
        <Typography variant="h4">{t('releases.releaseDetails')}</Typography>
        <i className={clsx('fa fa-close', helperClasses.headerIcon)} onClick={onSidePanelClose} />
      </Box>
      <Box>
        <Box className={helperClasses.floatBoxRight} mt={2} mr={2}>
          <YBButton
            className={helperClasses.deleteButton}
            variant="secondary"
            disabled={
              data?.universes?.length > 0 || !hasNecessaryPerm(ApiPermissionMap.MODIFY_YBDB_RELEASE)
            }
            size="large"
            startIcon={<Delete />}
            onClick={() => {
              onDeleteReleaseButtonClick();
              onSidePanelClose();
            }}
          >
            {t('common.delete')}
          </YBButton>
          <YBButton
            variant="secondary"
            size="large"
            disabled={
              data?.universes?.length > 0 || !hasNecessaryPerm(ApiPermissionMap.MODIFY_YBDB_RELEASE)
            }
            onClick={() => {
              onDisableReleaseButtonClick();
              onSidePanelClose();
            }}
          >
            {data?.state === ReleaseState.ACTIVE
              ? t('releases.disableDeployment')
              : t('releases.enableDeployment')}
          </YBButton>
        </Box>
      </Box>
      <Box ml={3} mr={2} className={helperClasses.releaseDetailsBox}>
        <Box display="flex" className={helperClasses.releaseMetadataBox}>
          {(['version', 'support', 'releaseDate', 'releaseNote', 'status'] as const).map(
            (releaseDetailsKey) => (
              <Box key={releaseDetailsKey}>
                <Typography variant="body1">{t(`releases.${releaseDetailsKey}`)}</Typography>
                <Box>
                  <Typography variant="body2">{formatReleaseDetails(releaseDetailsKey)}</Typography>
                </Box>
              </Box>
            )
          )}
        </Box>
      </Box>
      <Box mt={3} ml={3} mr={2}>
        <TabContext value={currentTab}>
          <TabList
            classes={{ root: helperClasses.overrideMuiTabs }}
            TabIndicatorProps={{
              style: { backgroundColor: theme.palette.common.white }
            }}
            onChange={handleTabChange}
          >
            <Tab
              label={t('releases.importArchitecture')}
              value={ReleaseDetailsTab.IMPORTED_ARCHITECTURE}
              className={clsx({
                [helperClasses.tab]: true,
                [helperClasses.unfocusedTab]: currentTab !== ReleaseDetailsTab.IMPORTED_ARCHITECTURE
              })}
            />
            <Tab
              label={
                data?.universes?.length
                  ? `${t('releases.inUseUniverses')} (${data?.universes?.length})`
                  : t('releases.inUseUniverses')
              }
              value={ReleaseDetailsTab.IN_USE_UNIVERSES}
              className={clsx({
                [helperClasses.tab]: true,
                [helperClasses.unfocusedTab]: currentTab !== ReleaseDetailsTab.IN_USE_UNIVERSES
              })}
            />
          </TabList>
          <Box className={clsx(helperClasses.tabPanel, helperClasses.importedArchitectureBox)}>
            <TabPanel value={ReleaseDetailsTab.IMPORTED_ARCHITECTURE}>
              <YBButton
                className={helperClasses.floatBoxRight}
                disabled={
                  data?.artifacts.length >= 3 ||
                  !hasNecessaryPerm(ApiPermissionMap.MODIFY_YBDB_RELEASE)
                }
                variant="secondary"
                size="large"
                startIcon={<Add />}
                onClick={() => {
                  onNewReleaseButtonClick();
                  onSidePanelClose();
                  onSetModalTitle(ModalTitle.ADD_ARCHITECTURE);
                }}
              >
                {ModalTitle.ADD_ARCHITECTURE}
              </YBButton>
              <Box mt={8}>
                <ImportedArchitecture
                  isDisabled={
                    data?.universes?.length > 0 ||
                    !hasNecessaryPerm(ApiPermissionMap.MODIFY_YBDB_RELEASE)
                  }
                  artifacts={data?.artifacts}
                  onSetModalTitle={onSetModalTitle}
                  onSidePanelClose={onSidePanelClose}
                  onEditArchitectureClick={onEditArchitectureClick}
                  onSetReleaseArchitecture={onSetReleaseArchitecture}
                />
              </Box>
            </TabPanel>
            <TabPanel value={ReleaseDetailsTab.IN_USE_UNIVERSES}>
              <InUseUniverses inUseUniverses={data?.universes} />
            </TabPanel>
          </Box>
        </TabContext>
      </Box>
    </Box>
  );
};
