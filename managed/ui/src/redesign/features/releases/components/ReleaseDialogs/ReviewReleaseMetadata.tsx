import { Box, Typography, makeStyles } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import clsx from 'clsx';
import { AlertVariant, YBAlert, YBButtonGroup, YBInput, YBLabel } from '../../../../components';
import { ReleasePlatform, ReleasePlatformArchitecture } from '../dtos';
import { ybFormatDate, YBTimeFormats } from '../../../../helpers/DateUtils';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: `${theme.spacing(3)}px ${theme.spacing(4.5)}px`
  },
  marginTop: {
    marginTop: theme.spacing(2)
  },
  marginLeft: {
    marginLeft: theme.spacing(2)
  },
  reviewReleaseDetailsBox: {
    border: '1px',
    borderRadius: '8px',
    padding: '8px 16px 8px 16px',
    gap: theme.spacing(2),
    borderColor: theme.palette.ybacolors.ybBorderGray,
    borderStyle: 'solid',
    backgroundColor: 'rgba(229, 229, 233, 0.2)',
    marginTop: theme.spacing(2)
  },
  reviewReleaseMetadataRow: {
    display: 'flex',
    flexDirection: 'row'
  },
  flexColumn: {
    display: 'flex',
    flexDirection: 'column'
  },
  smallerMetadata: {
    fontWeight: 400,
    fontFamily: 'Inter',
    fontSize: '11.5px'
  },
  largerMetaData: {
    fontWeight: 400,
    fontFamily: 'Inter',
    fontSize: '13px',
    alignSelf: 'center'
  },
  labelWidth: {
    width: 'fit-content'
  },
  smallInputTextBox: {
    width: '300px',
    marginTop: theme.spacing(1)
  },
  largeInputTextBox: {
    width: '384px'
  },
  overrideMuiButtonGroup: {
    '& .MuiButton-containedSecondary': {
      backgroundColor: 'rgba(43, 89, 195, 0.1)',
      color: theme.palette.primary[600],
      border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
    },
    '& .MuiButton-outlinedSecondary': {
      backgroundColor: 'white',
      border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
    }
  },
  warningBox: {
    height: '40px',
    padding: theme.spacing(1),
    gap: '8px',
    border: '1px',
    borderRadius: '8px',
    backgroundColor: theme.palette.warning[100]
  },
  helperVersionMessage: {
    marginTop: theme.spacing(1),
    color: '#818182',
    fontFamily: 'Inter',
    fontSize: '12px',
    fontWeight: 400
  }
}));

const VERSION_FORMAT = 'Format: 2.22.0.0 or 2024.1.0.0';
interface ReleasePlatformButtonProps {
  label: string;
  value: ReleasePlatform;
}

interface ReleaseArchitectureButtonProps {
  label: string;
  value: ReleasePlatformArchitecture;
}

interface ReviewReleaseMetadataProps {
  releaseMetadatFetchError: boolean;
  urlMetadata?: any;
  deploymentType: ReleasePlatform | null;
  architecture: ReleasePlatformArchitecture | null;
  releaseVersion: string;
  handleReleaseVersionPart: (event: any) => void;
  handlePlatformSelect: (value: ReleasePlatformButtonProps) => void;
  handleArchitectureSelect: (value: ReleaseArchitectureButtonProps) => void;
}

export const ReviewReleaseMetadata = ({
  releaseMetadatFetchError,
  urlMetadata,
  deploymentType,
  architecture,
  releaseVersion,
  handleReleaseVersionPart,
  handlePlatformSelect,
  handleArchitectureSelect
}: ReviewReleaseMetadataProps) => {
  const helperClasses = useStyles();
  const { t } = useTranslation();

  const IntelArch = {
    value: ReleasePlatformArchitecture.X86,
    label: t(`releases.tags.${ReleasePlatformArchitecture.X86}`)
  };
  const ArmArch = {
    value: ReleasePlatformArchitecture.ARM,
    label: t(`releases.tags.${ReleasePlatformArchitecture.ARM}`)
  };

  const LinuxPlatform = {
    value: ReleasePlatform.LINUX,
    label: t(`releases.tags.${ReleasePlatform.LINUX}`)
  };

  const KubernetesPlatform = {
    value: ReleasePlatform.KUBERNETES,
    label: t(`releases.tags.${ReleasePlatform.KUBERNETES}`)
  };

  const getArchLabel = (val: ReleasePlatformArchitecture) => {
    return val === ReleasePlatformArchitecture.X86
      ? t(`releases.tags.${ReleasePlatformArchitecture.X86}`)
      : t(`releases.tags.${ReleasePlatformArchitecture.ARM}`);
  };

  const getPlatformLabel = (val: ReleasePlatform) => {
    return val === ReleasePlatform.LINUX
      ? t(`releases.tags.${ReleasePlatform.LINUX}`)
      : t(`releases.tags.${ReleasePlatform.KUBERNETES}`);
  };

  const platformList: ReleasePlatformButtonProps[] = [LinuxPlatform, KubernetesPlatform];
  const supportedArchList: ReleaseArchitectureButtonProps[] = [IntelArch, ArmArch];

  return (
    <Box>
      <Box mt={3}>
        <Typography variant={'body1'}>
          {t('releases.reviewReleaseMetadataSection.viewReleaseDetails')}
        </Typography>
        {!releaseMetadatFetchError ? (
          <Box
            className={helperClasses.reviewReleaseDetailsBox}
            display="flex"
            flexDirection={'column'}
          >
            <Box className={clsx(helperClasses.reviewReleaseMetadataRow)}>
              <YBLabel className={helperClasses.smallerMetadata}>
                {t('releases.reviewReleaseMetadataSection.version')}
              </YBLabel>
              <YBLabel className={(helperClasses.largerMetaData, helperClasses.labelWidth)}>
                {urlMetadata?.version}
              </YBLabel>
            </Box>
            <Box className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}>
              <YBLabel className={helperClasses.smallerMetadata}>
                {t('releases.reviewReleaseMetadataSection.deploymentType')}
              </YBLabel>
              <YBLabel className={(helperClasses.largerMetaData, helperClasses.labelWidth)}>
                {urlMetadata?.platform}
              </YBLabel>
            </Box>
            <Box className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}>
              <YBLabel className={helperClasses.smallerMetadata}>
                {t('releases.reviewReleaseMetadataSection.architecture')}
              </YBLabel>
              <YBLabel className={(helperClasses.largerMetaData, helperClasses.labelWidth)}>
                {urlMetadata?.architecture}
              </YBLabel>
            </Box>
            <Box className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}>
              <YBLabel className={helperClasses.smallerMetadata}>
                {t('releases.reviewReleaseMetadataSection.releaseSupport')}
              </YBLabel>
              <YBLabel className={(helperClasses.largerMetaData, helperClasses.labelWidth)}>
                {urlMetadata?.releaseType}
              </YBLabel>
            </Box>
            <Box className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}>
              <YBLabel className={helperClasses.smallerMetadata}>
                {t('releases.reviewReleaseMetadataSection.releaseDate')}
              </YBLabel>
              <YBLabel className={(helperClasses.largerMetaData, helperClasses.labelWidth)}>
                {urlMetadata?.releaseDate
                  ? ybFormatDate(urlMetadata?.releaseDate, YBTimeFormats.YB_DATE_ONLY_TIMESTAMP)
                  : ''}
              </YBLabel>
            </Box>
          </Box>
        ) : (
          <Box className={helperClasses.marginTop}>
            <Box
              className={clsx(helperClasses.warningBox, helperClasses.reviewReleaseMetadataRow)}
              mt={2}
            >
              <YBAlert
                text={t('releases.reviewReleaseMetadataSection.unableFetchRelease')}
                variant={AlertVariant.Warning}
                open={true}
              />
            </Box>
            <Box className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}>
              <YBLabel className={helperClasses.largerMetaData}>{t('releases.version')}</YBLabel>
              <YBLabel className={helperClasses.labelWidth}>
                <Box className={helperClasses.flexColumn}>
                  <YBInput
                    className={helperClasses.smallInputTextBox}
                    value={releaseVersion}
                    onChange={handleReleaseVersionPart}
                  />
                  <span className={helperClasses.helperVersionMessage}>{VERSION_FORMAT}</span>
                </Box>
              </YBLabel>
            </Box>

            <Box className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}>
              <YBLabel className={helperClasses.largerMetaData}>
                {t('releases.deploymentType')}
              </YBLabel>
              <YBButtonGroup
                dataTestId={'ReviewReleaseMetadata-DeploymentTypeButtonGroup'}
                btnGroupClassName={helperClasses.overrideMuiButtonGroup}
                variant={'contained'}
                color={'secondary'}
                values={platformList}
                selectedNum={{
                  label: getPlatformLabel(deploymentType ?? ReleasePlatform.LINUX),
                  value: deploymentType ?? ReleasePlatform.LINUX
                }}
                displayLabelFn={(platformButtonProps: ReleasePlatformButtonProps) => (
                  <>{platformButtonProps.label}</>
                )}
                handleSelect={handlePlatformSelect}
              />
            </Box>

            {(!deploymentType || deploymentType === ReleasePlatform.LINUX) && (
              <Box
                className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}
              >
                <YBLabel className={helperClasses.largerMetaData}>
                  {t('releases.architectureLabel')}
                </YBLabel>
                <YBButtonGroup
                  dataTestId={'ReviewReleaseMetadata-ArchitectureButtonGroup'}
                  btnGroupClassName={helperClasses.overrideMuiButtonGroup}
                  variant={'contained'}
                  color={'secondary'}
                  values={supportedArchList}
                  selectedNum={{
                    label: getArchLabel(architecture ?? ReleasePlatformArchitecture.X86),
                    value: architecture ?? ReleasePlatformArchitecture.X86
                  }}
                  displayLabelFn={(architectureButtonProps: ReleaseArchitectureButtonProps) => (
                    <>{architectureButtonProps.label}</>
                  )}
                  handleSelect={handleArchitectureSelect}
                />
              </Box>
            )}
          </Box>
        )}
      </Box>
    </Box>
  );
};
