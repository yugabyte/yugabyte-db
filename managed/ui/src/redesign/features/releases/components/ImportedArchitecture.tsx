import { useTranslation } from 'react-i18next';
import { Box, Divider, Typography, makeStyles } from '@material-ui/core';
import { Edit } from '@material-ui/icons';
import clsx from 'clsx';
import { YBButton } from '../../../components';
import { YBCopyButton } from '../../../components/YBCopyButton/YBCopyButton';
import { ModalTitle, ReleaseArtifacts, ReleasePlatform, ReleasePlatformArchitecture } from './dtos';
import { isEmptyString } from '../../../../utils/ObjectUtils';

interface ImportedArchitectureProps {
  artifacts: ReleaseArtifacts[] | undefined;
  onEditArchitectureClick: () => void;
  onSetModalTitle: (modalTitle: string) => void;
  onSetReleaseArchitecture: (selectedArchitecture: ReleasePlatformArchitecture) => void;
  onSidePanelClose: () => void;
}

const useStyles = makeStyles((theme) => ({
  architectureBox: {
    border: '1px',
    borderRadius: '8px',
    padding: '20px 20px 20px 0',
    borderColor: '#E3E3E5',
    borderStyle: 'solid'
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
  architectureLocation: {
    marginTop: '7px'
  },
  architectureType: {
    border: '1px',
    borderRadius: '6px',
    borderStyle: 'dotted',
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
  COPY_FILE_PATH: 'Copy File Path',
  COPY_URL: 'Copy URL'
} as const;

export const ImportedArchitecture = ({
  artifacts,
  onEditArchitectureClick,
  onSidePanelClose,
  onSetModalTitle,
  onSetReleaseArchitecture
}: ImportedArchitectureProps) => {
  const helperClasses = useStyles();
  const { t } = useTranslation();

  const formatArchitectureLocation = (artifact: ReleaseArtifacts) => {
    const btnText = artifact?.location?.package_url
      ? IMPORT_OPTIONS.COPY_URL
      : IMPORT_OPTIONS.COPY_FILE_PATH;
    const architectureLocation = artifact?.location?.package_url
      ? artifact?.location?.package_url
      : artifact?.location.package_file_path;

    return (
      <Box className={clsx(helperClasses.architectureLocation, helperClasses.architectureType)}>
        <YBCopyButton text={architectureLocation!} btnText={btnText} />
      </Box>
    );
  };

  const formatArchitectureImportMethod = (artifact: ReleaseArtifacts) => {
    const architectureImportMethod = artifact?.location?.package_url ? 'URL' : 'File Upload';
    return (
      <Box className={clsx(helperClasses.architectureMetadataValue)}>
        {architectureImportMethod}
      </Box>
    );
  };

  const formatArchitectureType = (artifact: ReleaseArtifacts) => {
    let architectureType = artifact?.architecture;
    if (
      (!architectureType || isEmptyString(architectureType)) &&
      artifact?.platform === ReleasePlatform.KUBERNETES
    ) {
      architectureType = ReleasePlatformArchitecture.KUBERNETES;
    }
    return (
      <Box>
        <Box className={clsx(helperClasses.architectureType, helperClasses.architectureTypeWidth)}>
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
              <YBButton
                variant="secondary"
                size="large"
                startIcon={<Edit />}
                onClick={() => {
                  if (artifact.architecture === ReleasePlatformArchitecture.X86) {
                    onSetReleaseArchitecture(ReleasePlatformArchitecture.X86);
                    onSetModalTitle(ModalTitle.EDIT_X86);
                  } else if (artifact.architecture === ReleasePlatformArchitecture.ARM) {
                    onSetReleaseArchitecture(ReleasePlatformArchitecture.ARM);
                    onSetModalTitle(ModalTitle.EDIT_AARCH);
                  } else {
                    onSetReleaseArchitecture(ReleasePlatformArchitecture.KUBERNETES);
                    onSetModalTitle(ModalTitle.EDIT_KUBERNETES);
                  }
                  onEditArchitectureClick();
                  onSidePanelClose();
                }}
              >
                {t('releases.edit')}
              </YBButton>
            </Box>
          </Box>
        );
      })}
    </Box>
  );
};
