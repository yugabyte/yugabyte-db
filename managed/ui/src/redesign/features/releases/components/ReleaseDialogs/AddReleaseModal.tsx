import { useEffect, useState } from 'react';
import { useForm, FormProvider } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { Box, Typography, makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import {
  YBModal,
  YBRadioGroupField,
  RadioGroupOrientation,
  YBInput,
  YBInputField,
  YBLabel,
  YBButtonGroup,
  YBAlert,
  AlertVariant
} from '../../../../components';
import { YBFileUpload } from '../../../../components/YBFileUpload/YBFileUpload';
import {
  ReleaseFormFields,
  AddReleaseImportMethod,
  ModalTitle,
  ReleasePlatform,
  ReleasePlatformArchitecture
} from '../dtos';
import { IMPORT_METHOD_OPTIONS } from '../../helpers/constants';
import { isNonEmptyString } from '../../../../../utils/ObjectUtils';

import Path from '../../../../../redesign/assets/path.svg';
import PathDown from '../../../../../redesign/assets/path-down.svg';

const FILE_SIZE_LIMIT = 1048576000; // 1000 MB

interface AddReleaseModalProps {
  open: boolean;
  onClose: () => void;
  onActionPerformed: () => void;
  modalTitle: string;
  isAddRelease?: boolean;
  versionNumber?: string;
}

const useStyles = makeStyles((theme) => ({
  root: {
    padding: `${theme.spacing(3)}px ${theme.spacing(4.5)}px`
  },
  modalTitle: {
    marginLeft: theme.spacing(2.25)
  },
  marginTop: {
    marginTop: theme.spacing(2)
  },
  marginLeft: {
    marginLeft: theme.spacing(2)
  },
  importBox: {
    border: '1px',
    borderRadius: '8px',
    padding: '20px 20px 20px 0',
    borderColor: '#E3E3E5',
    borderStyle: 'solid',
    marginTop: theme.spacing(2)
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
  instructionsLargeText: {
    color: theme.palette.ybacolors.ybDarkGray
  },
  instructionsSmallText: {
    marginTop: theme.spacing(1),
    fontSize: '11.5px',
    color: theme.palette.ybacolors.pillInactiveText
  },
  upload: {
    marginTop: theme.spacing(2)
  },
  reviewReleaseMetadataRow: {
    display: 'flex',
    flexDirection: 'row'
  },
  reviewReleaseMetadataColumn: {
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
    width: '50px'
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
  }
}));

interface ReleasePlatformButtonProps {
  label: string;
  value: ReleasePlatform;
}

interface ReleaseArchitectureButtonProps {
  label: string;
  value: ReleasePlatformArchitecture;
}

export const AddReleaseModal = ({
  open,
  onActionPerformed,
  onClose,
  modalTitle,
  isAddRelease = true,
  versionNumber
}: AddReleaseModalProps) => {
  const { t } = useTranslation();
  const helperClasses = useStyles();

  const [viewTag, setViewTag] = useState<boolean>(false);
  const [reviewReleaseDetails, setReviewReleaseDetails] = useState<boolean>(false);
  const [releaseMetadatFetchError, setReleaseMetadatFetchError] = useState<boolean>(false);
  const [releaseBasePart, setReleaseBasePart] = useState<string>('');
  const [releaseFirstPart, setReleaseFirstPart] = useState<string>('');
  const [releaseSecondPart, setReleaseSecondPart] = useState<string>('');
  const [releaseThirdPart, setReleaseThirdPart] = useState<string>('');
  const [deploymentType, setDeploymentType] = useState<ReleasePlatform>(ReleasePlatform.LINUX);
  const [architecture, setArchitecure] = useState<ReleasePlatformArchitecture>(
    ReleasePlatformArchitecture.X86
  );

  const formMethods = useForm<ReleaseFormFields>({
    defaultValues: {
      importMethod: AddReleaseImportMethod.FILE_UPLOAD,
      installationPackageUrl: '',
      signatureUrl: '',
      installationPackageFile: undefined,
      signatureFile: undefined,
      releaseTag: '',
      version: '',
      architecture: '',
      platform: ''
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const {
    formState: { isValid, dirtyFields },
    watch,
    reset,
    control,
    handleSubmit,
    setValue
  } = formMethods;

  useEffect(() => {
    reset();
  }, [open, reset]);

  const hideModal = () => {
    reset();
    onClose();
  };

  // Watchers
  const importMethodValue = watch('importMethod');
  const installationPackageUrlValue = watch('installationPackageUrl');
  const signatureUrlValue = watch('signatureUrl');
  const installationPackageFileValue = watch('installationPackageFile');
  const signatureFileValue = watch('signatureFile');

  useEffect(() => {
    if (
      importMethodValue === AddReleaseImportMethod.URL &&
      isNonEmptyString(installationPackageUrlValue) &&
      isNonEmptyString(signatureUrlValue)
    ) {
      // TODO: Make an API call to ensure it returns the list of releases - fetchUrlReleaseArtifact (GET) from api.ts
      setReviewReleaseDetails(true);
    } else {
      setReviewReleaseDetails(false);
    }

    if (importMethodValue === AddReleaseImportMethod.FILE_UPLOAD && signatureFileValue) {
      // TODO: Make an API call to ensure it returns the list of releases - fetchFileReleaseArtifact (GET) from api.ts
    }

    if (importMethodValue === AddReleaseImportMethod.FILE_UPLOAD && installationPackageFileValue) {
      // TODO: Make an API call to ensure it returns the list of releases - uploadReleaseArtifact (POST) from api.ts
    }

    // TODO: Fetch API for release metadata and set error value appropriately
    setReleaseMetadatFetchError(
      isNonEmptyString(signatureUrlValue) && installationPackageUrlValue === 'ab'
    );
  }, [
    installationPackageUrlValue,
    signatureUrlValue,
    signatureFileValue,
    installationPackageFileValue,
    importMethodValue
  ]);

  const handleFormSubmit = handleSubmit((formValues) => {
    if (importMethodValue === AddReleaseImportMethod.URL) {
      setValue('version', `${releaseFirstPart}.${releaseSecondPart}.${releaseThirdPart}`);
    }
    // TODO: Combine release version releaseFirstPart, releaseSecondPart, releaseThirdPart

    // TODO: Write a mutation call on top Make to cxreate release - createRelease (POST) from api.ts
    // TODO: onSuccess on above mutation call, ensure to call onActionPerformed() which will get fresh set of releasaes
    // to be displayed in ReleaseList page
  });

  const handleReleaseFirstPart = (event: React.ChangeEvent<HTMLInputElement>) => {
    setReleaseFirstPart(event.target.value);
  };

  const handleReleaseBasePart = (event: React.ChangeEvent<HTMLInputElement>) => {
    setReleaseBasePart(event.target.value);
  };

  const handleReleaseSecondPart = (event: React.ChangeEvent<HTMLInputElement>) => {
    setReleaseSecondPart(event.target.value);
  };

  const handleReleaseThirdPart = (event: React.ChangeEvent<HTMLInputElement>) => {
    setReleaseThirdPart(event.target.value);
  };

  const handlePlatformSelect = (val: ReleasePlatformButtonProps) => {
    //TODO: call setvalue and add thsi field to form
    setDeploymentType(val.value);
    setValue('platform', val.value);
  };

  const handleArchitectureSelect = (val: ReleaseArchitectureButtonProps) => {
    //TODO: call setvalue and add this field to form
    setArchitecure(val.value);
    setValue('architecture', val.value);
  };

  const handleViewTag = () => {
    setViewTag(!viewTag);
  };

  const IntelArch = {
    value: ReleasePlatformArchitecture.X86,
    label: t(`releases.${ReleasePlatformArchitecture.X86}`)
  };
  const ArmArch = {
    value: ReleasePlatformArchitecture.ARM,
    label: t(`releases.${ReleasePlatformArchitecture.ARM}`)
  };

  const LinuxPlatform = {
    value: ReleasePlatform.LINUX,
    label: t(`releases.${ReleasePlatform.LINUX}`)
  };

  const KubernetesPlatform = {
    value: ReleasePlatform.KUBERNETES,
    label: t(`releases.${ReleasePlatform.KUBERNETES}`)
  };

  const getArchLabel = (val: ReleasePlatformArchitecture) => {
    return val === ReleasePlatformArchitecture.X86
      ? t(`releases.${ReleasePlatformArchitecture.X86}`)
      : t(`releases.${ReleasePlatformArchitecture.ARM}`);
  };

  const getPlatformLabel = (val: ReleasePlatform) => {
    return val === ReleasePlatform.LINUX
      ? t(`releases.${ReleasePlatform.LINUX}`)
      : t(`releases.${ReleasePlatform.KUBERNETES}`);
  };

  const platformList: ReleasePlatformButtonProps[] = [LinuxPlatform, KubernetesPlatform];
  const supportedArchList: ReleaseArchitectureButtonProps[] = [IntelArch, ArmArch];

  const reviewReleaseDetailsComponent = () => {
    return (
      <Box>
        <Box mt={3}>
          <Typography variant={'body1'}>
            {t('releases.addReleaseModal.viewReleaseDetails')}
          </Typography>
          {!releaseMetadatFetchError ? (
            <Box
              className={helperClasses.reviewReleaseDetailsBox}
              display="flex"
              flexDirection={'column'}
            >
              <Box className={clsx(helperClasses.reviewReleaseMetadataRow)}>
                <YBLabel className={helperClasses.smallerMetadata}>
                  {t('releases.addReleaseModal.version')}
                </YBLabel>
                <YBLabel className={(helperClasses.largerMetaData, helperClasses.labelWidth)}>
                  {'{VERSION FROM API}'}
                </YBLabel>
              </Box>
              <Box
                className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}
              >
                <YBLabel className={helperClasses.smallerMetadata}>
                  {t('releases.addReleaseModal.deploymentType')}
                </YBLabel>
                <YBLabel className={(helperClasses.largerMetaData, helperClasses.labelWidth)}>
                  {'DEPLOYMENT FROM API'}
                </YBLabel>
              </Box>
              <Box
                className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}
              >
                <YBLabel className={helperClasses.smallerMetadata}>
                  {t('releases.addReleaseModal.architecture')}
                </YBLabel>
                <YBLabel className={(helperClasses.largerMetaData, helperClasses.labelWidth)}>
                  {'ARCHITECTURE FROM API'}
                </YBLabel>
              </Box>
              <Box
                className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}
              >
                <YBLabel className={helperClasses.smallerMetadata}>
                  {t('releases.addReleaseModal.releaseSupport')}
                </YBLabel>
                <YBLabel className={(helperClasses.largerMetaData, helperClasses.labelWidth)}>
                  {'RELEASE SUPPORT FROM API'}
                </YBLabel>
              </Box>
              <Box
                className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}
              >
                <YBLabel className={helperClasses.smallerMetadata}>
                  {t('releases.addReleaseModal.releaseDate')}
                </YBLabel>
                <YBLabel className={(helperClasses.largerMetaData, helperClasses.labelWidth)}>
                  {'RELEASE DATE FROM API'}
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
                  text={t('releases.addReleaseModal.unableFetchRelease')}
                  variant={AlertVariant.Warning}
                  open={true}
                />
              </Box>
              <Box
                className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}
              >
                <YBLabel className={helperClasses.largerMetaData}>{t('releases.version')}</YBLabel>
                <YBLabel className={helperClasses.labelWidth}>
                  <YBInput
                    className={helperClasses.smallInputTextBox}
                    value={releaseBasePart}
                    onChange={handleReleaseBasePart}
                  />
                  {t('common.dot')}
                  <YBInput
                    className={helperClasses.smallInputTextBox}
                    value={releaseFirstPart}
                    onChange={handleReleaseFirstPart}
                  />
                  {t('common.dot')}
                  <YBInput
                    className={helperClasses.smallInputTextBox}
                    value={releaseSecondPart}
                    onChange={handleReleaseSecondPart}
                  />
                  {t('common.dot')}
                  <YBInput
                    className={helperClasses.smallInputTextBox}
                    value={releaseThirdPart}
                    onChange={handleReleaseThirdPart}
                  />
                </YBLabel>
              </Box>

              <Box
                className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}
              >
                <YBLabel className={helperClasses.largerMetaData}>
                  {t('releases.deploymentType')}
                </YBLabel>
                <YBButtonGroup
                  dataTestId={'AddReleaseModal-DeploymentTypeButtonGroup'}
                  btnGroupClassName={helperClasses.overrideMuiButtonGroup}
                  variant={'contained'}
                  color={'secondary'}
                  values={platformList}
                  selectedNum={{ label: getPlatformLabel(deploymentType), value: deploymentType }}
                  displayLabelFn={(platformButtonProps: ReleasePlatformButtonProps) => (
                    <>{platformButtonProps.label}</>
                  )}
                  handleSelect={handlePlatformSelect}
                />
              </Box>

              {deploymentType === ReleasePlatform.LINUX && (
                <Box
                  className={clsx(helperClasses.reviewReleaseMetadataRow, helperClasses.marginTop)}
                >
                  <YBLabel className={helperClasses.largerMetaData}>
                    {t('releases.architectureLabel')}
                  </YBLabel>
                  <YBButtonGroup
                    dataTestId={'AddReleaseModal-ArchitectureButtonGroup'}
                    btnGroupClassName={helperClasses.overrideMuiButtonGroup}
                    variant={'contained'}
                    color={'secondary'}
                    values={supportedArchList}
                    selectedNum={{ label: getArchLabel(architecture), value: architecture }}
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
        <Box className={helperClasses.reviewReleaseMetadataColumn}>
          <Box
            mt={4}
            className={helperClasses.reviewReleaseMetadataRow}
            style={{ cursor: 'pointer' }}
          >
            <img src={viewTag ? PathDown : Path} alt="path" onClick={handleViewTag} />
            <span className={clsx(helperClasses.largerMetaData, helperClasses.marginLeft)}>
              {t('releases.addReleaseModal.addReleaseTag')}
            </span>
          </Box>
          {viewTag && (
            <Box mt={2} className={helperClasses.reviewReleaseMetadataRow}>
              <span className={helperClasses.largerMetaData}>{t('releases.version')}</span>
              <YBInputField
                name={'releaseTag'}
                control={control}
                className={clsx(helperClasses.marginLeft, helperClasses.largeInputTextBox)}
              />
            </Box>
          )}
        </Box>
      </Box>
    );
  };

  return (
    <YBModal
      open={open}
      onClose={hideModal}
      title={
        isAddRelease ? ModalTitle.ADD_RELEASE : `${ModalTitle.ADD_ARCHITECTURE} - ${versionNumber}`
      }
      onSubmit={handleFormSubmit}
      cancelLabel={t('common.cancel')}
      submitLabel={modalTitle === ModalTitle.ADD_RELEASE ? t('releases.addRelease') : modalTitle}
      overrideHeight="860px"
      size="lg"
      titleSeparator
      enableBackdropDismiss
      dialogContentProps={{
        className: helperClasses.root,
        dividers: true
      }}
      titleContentProps={helperClasses.modalTitle}
      buttonProps={{
        primary: {
          disabled: !isValid
        }
      }}
    >
      <FormProvider {...formMethods}>
        <Box>
          <Box>
            <Typography variant={'body1'}>{t('releases.chooseImportMethod')}</Typography>
            <Box mt={2}>
              <YBRadioGroupField
                control={control}
                options={IMPORT_METHOD_OPTIONS}
                name="importMethod"
                orientation={RadioGroupOrientation.HORIZONTAL}
              />
            </Box>
          </Box>
          {importMethodValue === AddReleaseImportMethod.FILE_UPLOAD && (
            <Box className={helperClasses.importBox}>
              <Box ml={2}>
                <Typography variant={'body2'} className={helperClasses.instructionsLargeText}>
                  {t('releases.editArchitectureModal.uploadDBInstaller')}
                </Typography>
                <Typography variant={'body2'} className={helperClasses.instructionsSmallText}>
                  {t('releases.editArchitectureModal.supportedFileFormat')}
                </Typography>
                <Box mt={2}>
                  <YBFileUpload
                    name={'installationPackageFile'}
                    className={helperClasses.upload}
                    inputProps={{ accept: ['.tar.gz'] }}
                    label={t('releases.editArchitectureModal.upload')}
                    dataTestId={'AddReleaseModal-InstallerFileUploadButton'}
                    fileSizeLimit={FILE_SIZE_LIMIT}
                    onChange={(e: React.ChangeEvent<HTMLInputElement>) => {}}
                  />
                </Box>
              </Box>
              <Box ml={2} mt={4}>
                <Typography variant={'body2'} className={helperClasses.instructionsLargeText}>
                  {t('releases.editArchitectureModal.uploadSignatureFile')}
                </Typography>
                <Typography variant={'body2'} className={helperClasses.instructionsSmallText}>
                  {t('releases.editArchitectureModal.supportedFileFormat')}
                </Typography>
                <Box mt={2}>
                  <YBFileUpload
                    name={'signatureFile'}
                    className={helperClasses.upload}
                    label={'Upload'}
                    inputProps={{ accept: ['.tar.gz'] }}
                    dataTestId={'AddReleaseModal-InstallerFileUploadButton'}
                    fileSizeLimit={FILE_SIZE_LIMIT}
                    onChange={(e: React.ChangeEvent<HTMLInputElement>) => {}}
                  />
                </Box>
              </Box>
            </Box>
          )}
          {importMethodValue === AddReleaseImportMethod.URL && (
            <Box className={helperClasses.importBox}>
              <Box ml={2}>
                <Typography variant={'body2'} className={helperClasses.instructionsLargeText}>
                  {t('releases.editArchitectureModal.installerURL')}
                </Typography>
                <Box mt={2}>
                  <YBInputField
                    control={control}
                    placeholder={'https://'}
                    name="installationPackageUrl"
                    fullWidth
                  />
                </Box>
              </Box>
              <Box ml={2} mt={3}>
                <Typography variant={'body2'} className={helperClasses.instructionsLargeText}>
                  {t('releases.editArchitectureModal.signature')}
                </Typography>
                <Box mt={2}>
                  <YBInputField
                    control={control}
                    placeholder={'https://'}
                    name="signatureUrl"
                    fullWidth
                  />
                </Box>
              </Box>
            </Box>
          )}
          {reviewReleaseDetails && reviewReleaseDetailsComponent()}
        </Box>
      </FormProvider>
    </YBModal>
  );
};
