import { useEffect, useState } from 'react';
import { useForm, FormProvider } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useSelector } from 'react-redux';
import { useInterval } from 'react-use';
import { toast } from 'react-toastify';
import { Box, Typography, makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import { YBBanner } from '../YBBanner';
import {
  YBModal,
  YBRadioGroupField,
  RadioGroupOrientation,
  YBInputField,
  YBButton
} from '../../../../components';
import { YBFileUpload } from '../../../../components/YBFileUpload/YBFileUpload';
import { YBLoading } from '../../../../../components/common/indicators';
import { ReviewReleaseMetadata } from './ReviewReleaseMetadata';
import { AXIOS_INSTANCE, ReleasesAPI, releaseArtifactKey } from '../../api';
import {
  ReleaseFormFields,
  AddReleaseImportMethod,
  ModalTitle,
  ReleasePlatform,
  ReleasePlatformArchitecture,
  ReleaseYBType,
  ReleasePlatformButtonProps,
  ReleaseArchitectureButtonProps,
  Releases,
  UrlArtifactStatus,
  ReleaseType
} from '../dtos';
import {
  IMPORT_METHOD_OPTIONS,
  FILE_SIZE_LIMIT,
  REFETCH_URL_METADATA_MS
} from '../../helpers/constants';
import { isEmptyString, isNonEmptyString } from '../../../../../utils/ObjectUtils';

import Path from '../../../../../redesign/assets/path.svg';
import PathDown from '../../../../../redesign/assets/path-down.svg';

interface AddReleaseModalProps {
  open: boolean;
  data: Releases | null;
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
  largerMetaData: {
    fontWeight: 400,
    fontFamily: 'Inter',
    fontSize: '13px',
    alignSelf: 'center'
  },
  largeInputTextBox: {
    width: '384px'
  },
  overrideMuiHelperText: {
    '& .MuiFormHelperText-root': {
      color: theme.palette.error[500]
    }
  },
  bannerBox: {
    marginBottom: theme.spacing(2),
    marginLeft: theme.spacing(6)
  }
}));

export const AddReleaseModal = ({
  open,
  data,
  onActionPerformed,
  onClose,
  modalTitle,
  isAddRelease = true,
  versionNumber
}: AddReleaseModalProps) => {
  const { t } = useTranslation();
  const helperClasses = useStyles();
  const queryClient = useQueryClient();
  const formData = new FormData();

  // Selector variable
  const currentCustomerInfo = useSelector((state: any) => state.customer.currentCustomer.data);
  const currentCustomerUuid = currentCustomerInfo?.uuid;

  // State variables
  const [isMetadataLoading, setIsMetadataLoading] = useState<boolean>(false);
  const [urlMetadataStatus, setUrlMetadataStatus] = useState<UrlArtifactStatus>(
    UrlArtifactStatus.EMPTY
  );
  const [progressValue, setProgressValue] = useState<number>(0);
  const [isFileUploaded, setIsFileUploaded] = useState<boolean>(false);
  const [fileId, setFileId] = useState<string | null>(null);
  const [resourceUuid, setResourceUuid] = useState<string | null>(null);
  const [urlMetadata, setUrlMetadata] = useState<any>(null);
  const [errorHelperText, setErrorHelperText] = useState<string>('');
  const [viewTag, setViewTag] = useState<boolean>(false);
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const [reviewReleaseDetails, setReviewReleaseDetails] = useState<boolean>(false);
  const [releaseMetadatFetchError, setReleaseMetadatFetchError] = useState<boolean>(false);
  const [releaseVersion, setReleaseVersion] = useState<string>('');
  const [deploymentType, setDeploymentType] = useState<ReleasePlatform | null>(null);
  const [architecture, setArchitecure] = useState<ReleasePlatformArchitecture | null>(null);
  const [installationPackageFile, setInstallationPackageFile] = useState<File | undefined>(
    undefined
  );

  // useForm hook definition
  const formMethods = useForm<ReleaseFormFields>({
    defaultValues: {
      importMethod: AddReleaseImportMethod.FILE_UPLOAD,
      ybType: ReleaseYBType.YUGABYTEDB,
      sha256: '',
      installationPackageUrl: '',
      signatureUrl: '',
      installationPackageFile: installationPackageFile,
      releaseTag: '',
      version: '',
      architecture: '',
      platform: ''
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const { watch, control, handleSubmit, setValue, getValues } = formMethods;

  const hideModal = () => {
    onClose();
  };

  // Watchers
  const importMethodValue = watch('importMethod');
  const installationPackageUrlValue = watch('installationPackageUrl');
  const versionValue = watch('version');
  const architectureValue = watch('architecture');

  const clearFile = () => {
    setFileId(null);
    setIsFileUploaded(false);
    setInstallationPackageFile(undefined);
    setUrlMetadata(null);
  };

  const clearUrl = () => {
    setValue('installationPackageUrl', '');
    setUrlMetadata(null);
  };

  // componentDidUpdate that gets triggered when url value changes
  useEffect(() => {
    // If the user currently is on URL import method ensure if the user
    // previously uploaded any file when they were in File Upload, that state
    // should be reset as they are no longer in File upload state
    if (importMethodValue === AddReleaseImportMethod.URL) {
      clearFile();
    }

    if (
      importMethodValue === AddReleaseImportMethod.URL &&
      (isEmptyString(installationPackageUrlValue) || !isValidUrl(installationPackageUrlValue!))
    ) {
      setReviewReleaseDetails(false);
    }
  }, [installationPackageUrlValue, importMethodValue]);

  // componentDidUpdate that gets triggered when file uploaded changes
  useEffect(() => {
    // If the user currently is on File Upload import method ensure if the user
    // previously added any URL when they were in URL import, that state
    // should be reset as they are no longer in URL import state
    if (importMethodValue === AddReleaseImportMethod.FILE_UPLOAD) {
      clearUrl();
    }

    if (
      importMethodValue === AddReleaseImportMethod.FILE_UPLOAD &&
      !isValidFile(installationPackageFile!)
    ) {
      setReviewReleaseDetails(false);
    } else if (
      importMethodValue === AddReleaseImportMethod.FILE_UPLOAD &&
      isValidFile(installationPackageFile!)
    ) {
      formData.append('file', installationPackageFile!, installationPackageFile!.name);
      uploadInstallerFileMutation.mutate(formData);
    }
  }, [importMethodValue, installationPackageFile]);

  // To extract the URL metadata we need to poll the API till it goes out of running state
  useInterval(() => {
    if (isNonEmptyString(resourceUuid) && urlMetadataStatus === UrlArtifactStatus.RUNNING) {
      queryClient.invalidateQueries(releaseArtifactKey.resource(resourceUuid!));
    }
  }, REFETCH_URL_METADATA_MS);

  // When user enters URL and clicks on "Fetch Metadata" button, will fetch metadata for artifact
  const fetchReleaseMetadata = () => {
    if (
      importMethodValue === AddReleaseImportMethod.URL &&
      isValidUrl(installationPackageUrlValue!)
    ) {
      setUrlMetadataStatus(UrlArtifactStatus.EMPTY);
      setIsMetadataLoading(true);
      setReviewReleaseDetails(true);
      extractUrlMetadata.mutate(installationPackageUrlValue!);
    }

    if (
      importMethodValue === AddReleaseImportMethod.FILE_UPLOAD &&
      isValidFile(installationPackageFile!)
    ) {
      setIsMetadataLoading(true);
      setReviewReleaseDetails(true);
      extractFileMetadata.mutate(fileId!);
    }
  };

  // The file to be uploaded is large and can be as long as 1GB, hence using FormData to attach
  // file, file name and pass multipart/form-data header exclusively
  const uploadInstallerFileMutation = useMutation('fileUpload', (formDataSpec: FormData) => {
    return AXIOS_INSTANCE.post(
      `/customers/${currentCustomerUuid}/ybdb_release/upload`,
      formDataSpec,
      {
        headers: { 'Content-Type': 'multipart/form-data' },
        onUploadProgress: (data) => {
          const progress = (data.loaded / data.total) * 100;
          //Set the progress value to show the progress bar
          setProgressValue(Math.round(progress));
        }
      }
    )
      .then((response: any) => {
        setIsFileUploaded(true);
        setFileId(response.data.resourceUUID);
        toast.success(t('releases.addReleaseModal.uploadSuccess'));
      })
      .catch((error) => {
        setFileId(null);
        setIsFileUploaded(false);
        toast.success(t('releases.addReleaseModal.uploadFailure'));
      });
  });

  // When adding new release make a POST API call
  const addRelease = useMutation((payload: any) => ReleasesAPI.createRelease(payload), {
    onSuccess: (data) => {
      toast.success(t('releases.addReleaseModal.addReleaseSucess'));
      onActionPerformed();
      onClose();
    },
    onError: () => {
      onActionPerformed();
      toast.error(t('releases.addReleaseModal.addReleaseFailure'));
    }
  });

  // When adding new architecture or when changing contents of artifact make a PUT API call
  const updateReleaseMetadata = useMutation(
    (payload: any) => ReleasesAPI.updateReleaseMetadata(payload, payload.release_uuid!),
    {
      onSuccess: (response: any) => {
        toast.success(t('releases.addReleaseModal.updateArtifactsSuccess'));
        onActionPerformed();
        onClose();
      },
      onError: () => {
        onActionPerformed();
        toast.error(t('releases.addReleaseModal.updateArtifactsFailure'));
      }
    }
  );

  // When fetching artifact metadata based on release url make a POST API call
  const extractUrlMetadata = useMutation((url: string) => ReleasesAPI.extractReleaseArtifact(url), {
    onSuccess: (response: any) => {
      setResourceUuid(response.resourceUUID);
    },
    onError: () => {
      setReleaseMetadatFetchError(true);
      setIsMetadataLoading(false);
      // When metadata fetch fails, we display a different view where we want
      // the default selection of platform to be linux
      setValue('platform', ReleasePlatform.LINUX);
      setValue('architecture', ReleasePlatformArchitecture.X86);
      setValue('version', '');
      toast.error(t('releases.addReleaseModal.extractMetadataUrlFailure'));
    }
  });

  const setReleaseResponse = (response: any) => {
    // Update the below values for both new release and new architecture
    setValue('architecture', response.architecture);
    setValue('platform', response.platform);
    setValue('sha256', response.sha256);

    setValue('version', response.version);
    setValue('ybType', response.yb_type);
    setValue('releaseType', response.release_type);
    setValue('releaseDate', response.release_date_msecs);
    setValue('releaseNotes', response.release_notes);

    setUrlMetadata({
      version: response.version,
      releaseType: response.release_type,
      releaseDate: response.release_date_msecs,
      platform: response.platform,
      architecture: response.architecture
    });
  };

  useQuery(
    releaseArtifactKey.resource(resourceUuid!),
    () => ReleasesAPI.getUrlReleaseArtifact(resourceUuid!),
    {
      enabled: isNonEmptyString(resourceUuid),
      onSuccess: (response: any) => {
        if (response.status === UrlArtifactStatus.SUCCESS) {
          setUrlMetadataStatus(UrlArtifactStatus.SUCCESS);
          setResourceUuid(null);
          setIsMetadataLoading(false);
          setReleaseResponse(response);
          setReleaseMetadatFetchError(false);
        } else if (
          response.status === UrlArtifactStatus.RUNNING ||
          response.status === UrlArtifactStatus.WAITING
        ) {
          setUrlMetadataStatus(UrlArtifactStatus.RUNNING);
          setIsMetadataLoading(true);
        } else if (response.status === UrlArtifactStatus.FAILURE) {
          setUrlMetadataStatus(UrlArtifactStatus.FAILURE);
          setIsMetadataLoading(false);
          setReleaseMetadatFetchError(true);
          setValue('version', response.version);
          setValue('ybType', response.yb_type);
          setValue('sha256', response.sha256);
          setValue('platform', ReleasePlatform.LINUX);
          setValue('architecture', ReleasePlatformArchitecture.X86);
          toast.error(t('releases.addReleaseModal.extractMetadataUrlFailure'));
        }
      },
      onError: () => {
        setUrlMetadataStatus(UrlArtifactStatus.EMPTY);
        setResourceUuid(null);
        setIsMetadataLoading(false);
        setReleaseMetadatFetchError(true);
        // When metadata fetch fails, we display a different view where we want
        // the default selection of platform to be linux
        setValue('platform', ReleasePlatform.LINUX);
        setValue('architecture', ReleasePlatformArchitecture.X86);
        setValue('version', '');
        toast.error(t('releases.addReleaseModal.extractMetadataUrlFailure'));
      }
    }
  );

  // When fetching artifact metadata based on release url make a POST API call
  const extractFileMetadata = useMutation(
    (fileId: string) => ReleasesAPI.fetchFileReleaseArtifact(fileId!),
    {
      onSuccess: (response: any) => {
        setReleaseMetadatFetchError(false);
        setIsMetadataLoading(false);
        setReleaseResponse(response);
        toast.success(t('releases.addReleaseModal.extractMetadataFileSuccess'));
      },
      onError: () => {
        setIsMetadataLoading(false);
        setReleaseMetadatFetchError(true);
        // When metadata fetch fails, we display a different view where we want
        // the default selection of platform to be linux
        setValue('platform', ReleasePlatform.LINUX);
        setValue('architecture', ReleasePlatformArchitecture.X86);
        setValue('version', '');
        toast.error(t('releases.addReleaseModal.extractMetadataFileFailure'));
      }
    }
  );

  const handleFormSubmit = handleSubmit((formValues) => {
    // Create a new release
    if (isAddRelease) {
      const newReleasePayload: any = {
        version: formValues.version,
        release_tag: formValues.releaseTag,
        yb_type: formValues.ybType ?? ReleaseYBType.YUGABYTEDB,
        artifacts: [
          {
            sha256: formValues.sha256,
            platform: formValues.platform,
            architecture:
              formValues.platform === ReleasePlatform.KUBERNETES ? null : formValues.architecture
          }
        ],
        release_type: formValues.releaseType ?? ReleaseType.PREVIEW,
        release_date_msecs: formValues.releaseDate,
        release_notes: formValues.releaseNotes
      };
      setIsSubmitting(true);
      if (importMethodValue === AddReleaseImportMethod.URL) {
        newReleasePayload.artifacts[0].package_url = formValues.installationPackageUrl;
      } else if (importMethodValue === AddReleaseImportMethod.FILE_UPLOAD) {
        newReleasePayload.artifacts[0].package_file_id = fileId;
      }
      addRelease.mutate(newReleasePayload, { onSettled: () => resetModal() });
    } else {
      // Create a new architecture for existing release
      const newArchitecturePayload: any = {};
      Object.assign(newArchitecturePayload, data);
      newArchitecturePayload.artifacts.push({
        sha256: formValues.sha256,
        platform: formValues.platform,
        architecture:
          formValues.platform === ReleasePlatform.KUBERNETES ? null : formValues.architecture
      });
      newArchitecturePayload.release_type = formValues.releaseType ?? ReleaseType.PREVIEW;
      newArchitecturePayload.release_tag = formValues.releaseTag;
      setIsSubmitting(true);
      const artifactsLength = newArchitecturePayload.artifacts.length;
      if (importMethodValue === AddReleaseImportMethod.URL) {
        newArchitecturePayload.artifacts[artifactsLength - 1].package_url =
          formValues.installationPackageUrl;
      } else if (importMethodValue === AddReleaseImportMethod.FILE_UPLOAD) {
        newArchitecturePayload.artifacts[artifactsLength - 1].package_file_id = fileId;
      }
      updateReleaseMetadata.mutate(newArchitecturePayload, { onSettled: () => resetModal() });
    }
  });

  // When adding new release or new architecture and when call is made, show loading indicator on dialog
  const resetModal = () => {
    setIsSubmitting(false);
  };

  const handleReleaseVersionPart = (event: React.ChangeEvent<HTMLInputElement>) => {
    setValue('version', event.target.value);
    setReleaseVersion(event.target.value);
  };

  const handlePlatformSelect = (val: ReleasePlatformButtonProps) => {
    // When metadata fetch fails for URL or file upload, we manually ask user
    // to select the platform, hence we need to set it
    setDeploymentType(val.value);
    setValue('platform', val.value);
  };

  const handleArchitectureSelect = (val: ReleaseArchitectureButtonProps) => {
    // When metadata fetch fails for URL or file upload, we manually ask user
    // to select the architecture, hence we need to set it
    setArchitecure(val.value);
    setValue('architecture', val.value);
  };

  const handleViewTag = () => {
    setViewTag(!viewTag);
  };

  // Checks if a given url is valid
  const isValidUrl = (installationUrl: string) => {
    try {
      new URL(installationUrl);
    } catch (e) {
      setErrorHelperText('Invalid Url');
      return false;
    }
    setErrorHelperText('');
    return true;
  };

  // checks if given file is valid
  const isValidFile = (file: File) => {
    if (!isNonEmptyString(file?.name)) {
      return false;
    }
    return true;
  };

  const isButtonDisabled = () => {
    // Disable if method is URL but user is in a mode where the URL entered is not valid
    // or when URL metadata extract API fails
    if (importMethodValue === AddReleaseImportMethod.URL) {
      if (isNonEmptyString(errorHelperText)) {
        return true;
      } else if (!isNonEmptyString(versionValue)) {
        return true;
      } else if (!isNonEmptyString(installationPackageUrlValue)) {
        return true;
      } else if (!releaseMetadatFetchError && !urlMetadata) {
        return true;
      }
    }

    // Disable if method is File Upload but user is in a mode where the file uploaded is not valid
    // or when file metadata extract API fails
    if (
      importMethodValue === AddReleaseImportMethod.FILE_UPLOAD &&
      (!isValidFile(installationPackageFile!) || !isNonEmptyString(versionValue))
    ) {
      return true;
    }
    return false;
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
      overrideWidth="800px"
      size="lg"
      titleSeparator
      enableBackdropDismiss
      dialogContentProps={{
        className: helperClasses.root,
        dividers: true
      }}
      titleContentProps={helperClasses.modalTitle}
      isSubmitting={isSubmitting}
      buttonProps={{
        primary: {
          disabled: isButtonDisabled()
        }
      }}
    >
      <FormProvider {...formMethods}>
        <Box data-testid="AddRelease-Container">
          <Box className={helperClasses.bannerBox}>
            {isMetadataLoading && <YBBanner message={t('releases.bannerFileUploadMessage')} />}
          </Box>
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
                    isUploaded={isFileUploaded}
                    uploadedFileName={installationPackageFile?.name}
                    progressValue={progressValue}
                    inputProps={{ accept: ['.gz'] }}
                    label={t('releases.editArchitectureModal.upload')}
                    dataTestId={'AddReleaseModal-InstallerFileUploadButton'}
                    fileSizeLimit={FILE_SIZE_LIMIT}
                    onChange={(e: React.ChangeEvent<HTMLInputElement>) => {
                      if (!e.target?.files?.[0]) {
                        setFileId(null);
                        setIsFileUploaded(false);
                      }
                      setInstallationPackageFile(e.target?.files?.[0]);
                    }}
                  />
                </Box>
                <Box mt={2}>
                  {isFileUploaded && (
                    <YBButton
                      variant="secondary"
                      data-testid="AddReleaseModal-FileUploadMetadataButton"
                      onClick={fetchReleaseMetadata}
                    >
                      {t('releases.fetchMetadata')}
                    </YBButton>
                  )}
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
                    helperText={errorHelperText}
                    className={clsx(
                      isNonEmptyString(errorHelperText) && helperClasses.overrideMuiHelperText
                    )}
                    fullWidth
                  />
                </Box>
              </Box>

              <Box mt={2} ml={2}>
                <YBButton
                  variant="secondary"
                  data-testid="AddReleaseModal-URLMetadataButton"
                  onClick={fetchReleaseMetadata}
                  disabled={isEmptyString(installationPackageUrlValue)}
                >
                  {t('releases.fetchMetadata')}
                </YBButton>
              </Box>
            </Box>
          )}
          {reviewReleaseDetails &&
            (isMetadataLoading ? (
              <YBLoading />
            ) : (
              <>
                <ReviewReleaseMetadata
                  urlMetadata={urlMetadata}
                  releaseMetadatFetchError={releaseMetadatFetchError}
                  deploymentType={deploymentType}
                  architecture={architecture}
                  releaseVersion={releaseVersion}
                  handleReleaseVersionPart={handleReleaseVersionPart}
                  handlePlatformSelect={handlePlatformSelect}
                  handleArchitectureSelect={handleArchitectureSelect}
                />
                {isAddRelease && (
                  <Box className={helperClasses.reviewReleaseMetadataColumn}>
                    <Box
                      mt={4}
                      className={helperClasses.reviewReleaseMetadataRow}
                      style={{ cursor: 'pointer' }}
                      onClick={handleViewTag}
                    >
                      <img src={viewTag ? PathDown : Path} alt="path" />
                      <span
                        className={clsx(helperClasses.largerMetaData, helperClasses.marginLeft)}
                      >
                        {t('releases.addReleaseModal.addReleaseTag')}
                      </span>
                    </Box>
                    {viewTag && (
                      <Box mt={2} className={helperClasses.reviewReleaseMetadataRow}>
                        <span className={helperClasses.largerMetaData}>
                          {t('releases.version')}
                        </span>
                        <YBInputField
                          name={'releaseTag'}
                          control={control}
                          className={clsx(
                            helperClasses.marginLeft,
                            helperClasses.largeInputTextBox
                          )}
                        />
                      </Box>
                    )}
                  </Box>
                )}
              </>
            ))}
        </Box>
      </FormProvider>
    </YBModal>
  );
};
