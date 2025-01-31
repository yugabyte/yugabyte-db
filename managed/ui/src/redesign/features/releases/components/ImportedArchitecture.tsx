import { useTranslation } from 'react-i18next';
import { Box, Divider, Typography, makeStyles } from '@material-ui/core';
import { Edit } from '@material-ui/icons';
import clsx from 'clsx';
import { YBButton } from '../../../components';
import { YBCopyButton } from '../../../components/YBCopyButton/YBCopyButton';
import { RbacValidator } from '../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../rbac/ApiAndUserPermMapping';
import { ModalTitle, ReleaseArtifacts, ReleasePlatform, ReleasePlatformArchitecture } from './dtos';

interface ImportedArchitectureProps {
  artifacts: ReleaseArtifacts[] | undefined;
  isDisabled: boolean;
  onEditArchitectureClick: () => void;
  onSetModalTitle: (modalTitle: string) => void;
  onSetReleaseArchitecture: (selectedArchitecture: ReleasePlatformArchitecture | null) => void;
  onSidePanelClose: () => void;
}

const useStyles = makeStyles((theme) => ({
  architectureBox: {
    border: '1px',
    borderRadius: '8px',
    padding: '20px 20px 20px 0',
    borderColor: '#E3E3E5',
    borderStyle: 'solid',
    backgroundColor: theme.palette.common.white
  },
  architectureMetadataBox: {
    marginLeft: theme.spacing(2),
    justifyContent: 'space-between'
  },
  floatButton: {
    float: 'right'
  },
  architectureMetadataValue: {
    marginTop: theme.spacing(1)
  },
  architectureLocationBox: {
    marginTop: '7px'
  },
  architectureTypeBox: {
    border: '1px',
    borderRadius: '6px',
    borderStyle: 'solid',
    padding: '4px 6px 4px 6px',
    borderColor: theme.palette.grey[300],
    marginTop: theme.spacing(0.5),
    fontSize: '11.5px'
  },
  architectureTypeWidth: {
    minWidth: '75px'
  },
  divider: {
    border: '1px',
    borderStyle: 'unset',
    backgroundColor: theme.palette.ybacolors.ybBorderGray,
    height: '41px'
  },
  flexColumn: {
    display: 'flex',
    flexDirection: 'column'
  },
  flexRow: {
    display: 'flex',
    flexDirection: 'row'
  }
}));

const IMPORT_OPTIONS = {
  COPY_FILE_NAME: 'Copy File Name',
  COPY_URL: 'Copy URL'
} as const;

export const ImportedArchitecture = ({
  artifacts,
  isDisabled,
  onEditArchitectureClick,
  onSidePanelClose,
  onSetModalTitle,
  onSetReleaseArchitecture
}: ImportedArchitectureProps) => {
  const helperClasses = useStyles();
  const { t } = useTranslation();

  const formatArchitectureLocation = (artifact: ReleaseArtifacts) => {
    const btnText = artifact?.package_url ? IMPORT_OPTIONS.COPY_URL : IMPORT_OPTIONS.COPY_FILE_NAME;
    const architectureLocation = artifact?.package_url
      ? artifact?.package_url
      : artifact?.file_name;

    return (
      <Box
        className={clsx(helperClasses.architectureLocationBox, helperClasses.architectureTypeBox)}
      >
        <YBCopyButton text={architectureLocation!} btnText={btnText} />
      </Box>
    );
  };

  const formatArchitectureImportMethod = (artifact: ReleaseArtifacts) => {
    const architectureImportMethod = artifact?.package_url ? 'URL' : 'File Upload';
    return (
      <Box className={clsx(helperClasses.architectureMetadataValue)}>
        {architectureImportMethod}
      </Box>
    );
  };

  const formatArchitectureType = (artifact: ReleaseArtifacts) => {
    let architectureType = artifact?.architecture;
    if (architectureType === null && artifact?.platform === ReleasePlatform.KUBERNETES) {
      architectureType = 'kubernetes';
    }
    return (
      <Box>
        <Box
          className={clsx(helperClasses.architectureTypeBox, helperClasses.architectureTypeWidth)}
        >
          <span>{t(`releases.tags.${architectureType}`)}</span>
        </Box>
      </Box>
    );
  };

  return (
    <Box>
      {artifacts?.map((artifact: ReleaseArtifacts) => {
        return (
          <Box className={helperClasses.architectureBox} mt={3}>
            <Box className={clsx(helperClasses.architectureMetadataBox, helperClasses.flexRow)}>
              <Box className={helperClasses.flexColumn}>
                <Typography variant="body1">{t(`releases.architecture.type`)}</Typography>
                <Box>
                  <Typography variant="body2">{formatArchitectureType(artifact)}</Typography>
                </Box>
              </Box>

              <Box className={helperClasses.flexColumn}>
                <Typography variant="body1">{t(`releases.architecture.importMethod`)}</Typography>
                <Box>
                  <Typography variant="body2">
                    {formatArchitectureImportMethod(artifact)}
                  </Typography>
                </Box>
              </Box>

              <Box className={helperClasses.flexColumn}>
                <Typography variant="body1">{t(`releases.architecture.location`)}</Typography>
                <Box>
                  <Typography variant="body2">{formatArchitectureLocation(artifact)}</Typography>
                </Box>
              </Box>

              <Divider orientation="vertical" className={helperClasses.divider} />
              <Box>
                <RbacValidator
                  accessRequiredOn={ApiPermissionMap.MODIFY_YBDB_RELEASE}
                  isControl
                  overrideStyle={{ display: 'block' }}
                >
                  <YBButton
                    title={isDisabled ? t('releases.disableActionsTooltipMessage') : ''}
                    variant="secondary"
                    size="large"
                    disabled={isDisabled}
                    startIcon={<Edit />}
                    onClick={() => {
                      if (artifact.architecture === ReleasePlatformArchitecture.X86) {
                        onSetReleaseArchitecture(ReleasePlatformArchitecture.X86);
                        onSetModalTitle(ModalTitle.EDIT_X86);
                      } else if (artifact.architecture === ReleasePlatformArchitecture.ARM) {
                        onSetReleaseArchitecture(ReleasePlatformArchitecture.ARM);
                        onSetModalTitle(ModalTitle.EDIT_AARCH);
                      } else if (
                        artifact.architecture === null &&
                        artifact.platform === ReleasePlatform.KUBERNETES
                      ) {
                        onSetReleaseArchitecture(null);
                        onSetModalTitle(ModalTitle.EDIT_KUBERNETES);
                      }
                      onEditArchitectureClick();
                      onSidePanelClose();
                    }}
                  >
                    {t('releases.edit')}
                  </YBButton>
                </RbacValidator>
              </Box>
            </Box>
          </Box>
        );
      })}
    </Box>
  );
};
