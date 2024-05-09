import { useEffect, useState } from 'react';
import { useForm, FormProvider } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useSelector } from 'react-redux';
import { useInterval } from 'react-use';
import { toast } from 'react-toastify';
import clsx from 'clsx';
import { Box, MenuItem, Typography, makeStyles } from '@material-ui/core';
import { YBLoading } from '../../../../../components/common/indicators';
import { YBBanner } from '../YBBanner';
import { YBButton, YBInputField, YBModal, YBSelect } from '../../../../components';
import { YBFileUpload } from '../../../../components/YBFileUpload/YBFileUpload';
import { ReviewReleaseMetadata } from './ReviewReleaseMetadata';
import { AXIOS_INSTANCE, ReleasesAPI, releaseArtifactKey } from '../../api';
import { isEmptyString, isNonEmptyString } from '../../../../../utils/ObjectUtils';
import {
  AddReleaseImportMethod,
  ReleaseArchitectureButtonProps,
  ReleaseArtifacts,
  ReleaseFormFields,
  ReleasePlatform,
  ReleasePlatformArchitecture,
  ReleasePlatformButtonProps,
  ReleaseYBType,
  Releases,
  UrlArtifactStatus
} from '../dtos';
import {
  FILE_SIZE_LIMIT,
  IMPORT_METHOD_OPTIONS,
  REFETCH_URL_METADATA_MS
} from '../../helpers/constants';

import KubernetesLogo from '../../../../../redesign/assets/kubernetes.svg';

interface EditArchitectureModalProps {
  data: Releases;
  open: boolean;
  modalTitle: string;
  releaseArchitecture: ReleasePlatformArchitecture | null;
  onClose: () => void;
  onActionPerformed: () => void;
}

const useStyles = makeStyles((theme) => ({
  root: {
    padding: `${theme.spacing(3)}px ${theme.spacing(4.5)}px`
  },
  modalTitle: {
    marginLeft: theme.spacing(2.25)
  },
  flexRow: {
    display: 'flex',
    flexDirection: 'row',
    marginTop: theme.spacing(0.5)
  },
  flexColumn: {
    display: 'flex',
    flexDirection: 'column'
  },
  helperText: {
    marginTop: theme.spacing(1),
    fontSize: '12px',
    color: '#818182'
  },
  importTypeText: {
    marginLeft: theme.spacing(0.5),
    alignSelf: 'center'
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
    color: '#232329'
  },
  instructionsSmallText: {
    marginTop: theme.spacing(1),
    fontSize: '11.5px',
    color: theme.palette.grey[700]
  },
  upload: {
    marginTop: theme.spacing(2)
  },
  importTypeSelect: {
    marginTop: theme.spacing(1)
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
  marginLeft: {
    marginLeft: theme.spacing(2)
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

export const EditArchitectureModal = ({
  data,
  open,
  modalTitle,
  releaseArchitecture,
  onClose,
  onActionPerformed
}: EditArchitectureModalProps) => {
  const { t } = useTranslation();
  const helperClasses = useStyles();
  const queryClient = useQueryClient();
  const formData = new FormData();

  const artifact =
    releaseArchitecture === null
      ? data?.artifacts?.find(
          (artifact: ReleaseArtifacts) => artifact.platform === ReleasePlatform.KUBERNETES
        )
      : data?.artifacts?.find(
          (artifact: ReleaseArtifacts) => artifact.architecture === releaseArchitecture
        );
  const importMethod = artifact?.package_url
    ? AddReleaseImportMethod.URL
    : AddReleaseImportMethod.FILE_UPLOAD;

  // Selector variable
  const currentCustomerInfo = useSelector((state: any) => state.customer.currentCustomer.data);
  const currentCustomerUuid = currentCustomerInfo?.uuid;

  // State variables
  const [defaultImportMethod, setDefaultImportMethod] = useState<AddReleaseImportMethod | string>(
    importMethod
  );
  const [installationPackageFile, setInstallationPackageFile] = useState<File | undefined>(
    undefined
  );
  const [isMetadataLoading, setIsMetadataLoading] = useState<boolean>(false);
  const [urlMetadataStatus, setUrlMetadataStatus] = useState<UrlArtifactStatus>(
    UrlArtifactStatus.EMPTY
  );
  const [progressValue, setProgressValue] = useState<number>(0);
  const [isFileUploaded, setIsFileUploaded] = useState<boolean>(false);
  const [packageFileId, setPackageFileId] = useState<string | undefined>(artifact?.package_file_id);
  const [resourceUuid, setResourceUuid] = useState<string | null>(null);
  const [urlMetadata, setUrlMetadata] = useState<any>(null);
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const [errorHelperText, setErrorHelperText] = useState<string>('');
  const [reviewReleaseDetails, setReviewReleaseDetails] = useState<boolean>(false);
  const [releaseMetadatFetchError, setReleaseMetadatFetchError] = useState<boolean>(false);
  const [releaseVersion, setReleaseVersion] = useState<string>('');
  const [deploymentType, setDeploymentType] = useState<any>(artifact?.platform);
  const [architecture, setArchitecure] = useState<any>(artifact?.architecture);

  // useForm hook definition
  const formMethods = useForm<ReleaseFormFields>({
    defaultValues: {
      importMethod: importMethod,
      ybType: data?.yb_type ?? ReleaseYBType.YUGABYTEDB,
      installationPackageUrl: artifact?.package_url ?? '',
      signatureUrl: artifact?.signature_url ?? '',
      installationPackageFile: installationPackageFile,
      releaseTag: data?.release_tag,
      version: data?.version,
      sha256: artifact?.sha256,
      architecture: artifact?.architecture,
      platform: artifact?.platform,
      releaseDate: data?.release_date,
      releaseNotes: data?.release_notes,
      releaseType: data?.release_type
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });

  const {
    formState: { isDirty },
    watch,
    control,
    setValue,
    handleSubmit
  } = formMethods;

  // Watchers
  const importMethodValue = watch('importMethod');
  const installationPackageUrlValue = watch('installationPackageUrl');
  const versionValue = watch('version');

  // Since it is the edit view, on the component mount, we need to prepopulate the metadata
  useEffect(() => {
    setReviewReleaseDetails(true);
    setUrlMetadata({
      version: data?.version,
      releaseType: data?.release_type,
      releaseDate: data?.release_date,
      platform: artifact?.platform,
      architecture: artifact?.architecture
    });
  }, []);

  // componentDidUpdate that gets triggered when url value changes
  useEffect(() => {
    if (
      importMethodValue === AddReleaseImportMethod.URL &&
      (isEmptyString(installationPackageUrlValue) || !isValidUrl(installationPackageUrlValue!))
    ) {
      setReviewReleaseDetails(false);
    }
    if (importMethodValue === AddReleaseImportMethod.URL) {
      artifact?.package_url !== installationPackageUrlValue
        ? setReviewReleaseDetails(false)
        : setReviewReleaseDetails(true);
    }
  }, [importMethodValue, installationPackageUrlValue]);

  // componentDidUpdate that gets triggered when file uploaded changes
  useEffect(() => {
    if (importMethodValue === AddReleaseImportMethod.FILE_UPLOAD) {
      if (isNonEmptyString(artifact?.package_file_id)) {
        artifact?.package_file_id !== packageFileId
          ? setReviewReleaseDetails(false)
          : setReviewReleaseDetails(true);
      } else {
        setReviewReleaseDetails(false);
      }
    }

    if (
      importMethodValue === AddReleaseImportMethod.FILE_UPLOAD &&
      isValidFile(installationPackageFile!)
    ) {
      // When new file is being uploaded, do not show review details
      setReviewReleaseDetails(false);
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
      setIsMetadataLoading(false);
      setReviewReleaseDetails(true);
      extractUrlMetadata.mutate(installationPackageUrlValue!);
    }

    if (
      importMethodValue === AddReleaseImportMethod.FILE_UPLOAD &&
      isValidFile(installationPackageFile!)
    ) {
      setIsMetadataLoading(true);
      setReviewReleaseDetails(true);
      extractFileMetadata.mutate(packageFileId!);
    }
  };

  const setReleaseResponse = (response: any) => {
    // Update the below values for both new release and new architecture
    setValue('architecture', response.architecture);
    setValue('platform', response.platform);
    setValue('sha256', response.sha256);
    setValue('version', response.version);

    setUrlMetadata({
      version: response.version,
      releaseType: response.release_type,
      releaseDate: response.release_date,
      platform: response.platform,
      architecture: response.architecture
    });
  };

  // When adding new architecture or when changing contents of existing artifact make a PUT API call
  const updateReleaseMetadata = useMutation(
    (payload: any) => ReleasesAPI.updateReleaseMetadata(payload, payload.release_uuid!),
    {
      onSuccess: (response: any) => {
        toast.success('Updated release artifacts successfully');
        onActionPerformed();
        onClose();
      },
      onError: () => {
        toast.error('Failed to update release artifacts');
      }
    }
  );

  const extractUrlMetadata = useMutation((url: string) => ReleasesAPI.extractReleaseArtifact(url), {
    onSuccess: (response: any) => {
      setResourceUuid(response.resourceUUID);
    },
    onError: () => {
      // When metadata fetch fails, we display a different view where we want
      // the default selection of platform to be linux
      setValue('version', '');
      setIsMetadataLoading(false);
      setReleaseMetadatFetchError(true);
      toast.error('Failed to extract metadata from URL');
    }
  });

  // When fetching artifact metadata based on release url make a POST API call
  const extractFileMetadata = useMutation(
    (fileId: string) => ReleasesAPI.fetchFileReleaseArtifact(fileId!),
    {
      onSuccess: (response: any) => {
        setReleaseMetadatFetchError(false);
        setIsMetadataLoading(false);
        setReleaseResponse(response);
        toast.success('Extracted metadata from file successfully');
      },
      onError: () => {
        setIsMetadataLoading(false);
        setReleaseMetadatFetchError(true);
        // When metadata fetch fails, we display a different view where we want
        // the default selection of platform to be linux
        setValue('platform', ReleasePlatform.LINUX);
        setValue('architecture', '');
        setValue('version', '');
        toast.error('Failed to extract metadata from the file');
      }
    }
  );

  // When fetching artifact metadata based on release url make a POST API call
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
          toast.success('Extracted metadata from URL successfully');
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
          toast.error('Failed to extract metadata from URL');
        }
      },
      onError: () => {
        setUrlMetadataStatus(UrlArtifactStatus.EMPTY);
        setResourceUuid(null);
        setIsMetadataLoading(false);
        setReleaseMetadatFetchError(true);
        // When metadata fetch fails, we display a different view where we want
        // the default selection of platform to be linux
        setValue('version', '');
        toast.error('Failed to extract metadata from URL');
      }
    }
  );

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
        setPackageFileId(response.data.resourceUUID);
        setValue('version', '');
        toast.success('Uploaded file successully');
      })
      .catch((error) => {
        setPackageFileId(undefined);
        setIsFileUploaded(false);
        toast.error('Not able to upload file, please try again');
      });
  });

  const handleReleaseVersionPart = (event: React.ChangeEvent<HTMLInputElement>) => {
    setValue('version', event.target.value);
    setReleaseVersion(event.target.value);
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

  // Change the content of existing artifacts
  const handleFormSubmit = handleSubmit((formValues) => {
    const newArchitecturePayload: any = {};
    Object.assign(newArchitecturePayload, data);
    const artifactsLength = newArchitecturePayload.artifacts.length;
    setIsSubmitting(true);

    newArchitecturePayload.artifacts[artifactsLength - 1].sha256 = formValues.sha256;
    newArchitecturePayload.artifacts[artifactsLength - 1].platform = formValues.platform;
    newArchitecturePayload.artifacts[artifactsLength - 1].architecture = formValues.architecture;
    if (importMethodValue === AddReleaseImportMethod.URL) {
      newArchitecturePayload.artifacts[artifactsLength - 1].package_url =
        formValues.installationPackageUrl;
      delete newArchitecturePayload.artifacts[artifactsLength - 1].package_file_id;
      delete newArchitecturePayload.artifacts[artifactsLength - 1].file_name;
    } else if (importMethodValue === AddReleaseImportMethod.FILE_UPLOAD) {
      newArchitecturePayload.artifacts[artifactsLength - 1].package_file_id = packageFileId;
      delete newArchitecturePayload.artifacts[artifactsLength - 1].package_url;
    }
    updateReleaseMetadata.mutate(newArchitecturePayload, { onSettled: () => resetModal() });
  });

  // When adding new release or new architecture and when call is made, show loading indicator on dialog
  const resetModal = () => {
    setIsSubmitting(false);
  };

  // Checks if given file is valid
  const isValidFile = (file: File) => {
    if (!isNonEmptyString(file?.name)) {
      return false;
    }
    return true;
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

  const onSelectChange = (selectedOption: string) => {
    // If user edits a release where the default import is URL but when user toggles to
    // File Upload and when again toggle to URL, retain URL values but remove file upload
    // values as it is still not a artifact
    if (
      selectedOption === AddReleaseImportMethod.FILE_UPLOAD &&
      importMethod !== AddReleaseImportMethod.FILE_UPLOAD
    ) {
      setReviewReleaseDetails(false);
      setPackageFileId(undefined);
      setInstallationPackageFile(undefined);
      // If user edits a release where the default import is File Upload but when user toggles to
      // URL and when again toggle to File Upload, retain File Upload values but remove URL
      // values as it is still not a artifact
    } else if (
      selectedOption === AddReleaseImportMethod.URL &&
      importMethod !== AddReleaseImportMethod.URL
    ) {
      setReviewReleaseDetails(false);
      setValue('installationPackageUrl', '');
    }
    setDefaultImportMethod(selectedOption);
    setValue('importMethod', selectedOption);
  };

  const isButtonDisabled = () => {
    // Disable if method is URL but user is in a mode where the values
    // have not been changed in edit mode
    if (
      defaultImportMethod === AddReleaseImportMethod.URL &&
      artifact?.package_url === installationPackageUrlValue
    ) {
      return true;
    }

    // Disable if method is URL but user is in a mode where the values
    // have changed but extract metadata API has failed
    if (
      defaultImportMethod === AddReleaseImportMethod.URL &&
      artifact?.package_url !== installationPackageUrlValue
    ) {
      if (!reviewReleaseDetails || isNonEmptyString(errorHelperText)) {
        return true;
      } else if (reviewReleaseDetails && isEmptyString(versionValue)) {
        return true;
      }
    }

    // Disable if method is File Upload but user is in a mode where the values
    // have not changed and same file exists
    if (
      defaultImportMethod === AddReleaseImportMethod.FILE_UPLOAD &&
      artifact?.package_file_id === packageFileId &&
      !packageFileId
    ) {
      return true;
    }

    // Disable if method is File Upload but user is in a mode where the file is
    // not valid or if file extract metadata API has failed
    if (
      defaultImportMethod === AddReleaseImportMethod.FILE_UPLOAD &&
      (!isValidFile(installationPackageFile!) || !isNonEmptyString(versionValue))
    ) {
      return true;
    }

    return false;
  };

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={modalTitle}
      onSubmit={handleFormSubmit}
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.save')}
      size="md"
      titleSeparator
      enableBackdropDismiss
      dialogContentProps={{
        className: helperClasses.root,
        dividers: true
      }}
      isSubmitting={isSubmitting}
      titleContentProps={helperClasses.modalTitle}
      buttonProps={{
        primary: {
          disabled: isButtonDisabled()
        }
      }}
    >
      <FormProvider {...formMethods}>
        <Box className={helperClasses.bannerBox}>
          {isMetadataLoading && <YBBanner message={t('releases.bannerFileUploadMessage')} />}
        </Box>
        <Box className={helperClasses.flexColumn}>
          {artifact?.platform === ReleasePlatform.KUBERNETES && (
            <>
              <Typography variant={'body1'}>{t('releases.architecture.type')}</Typography>
              <Box className={helperClasses.flexRow}>
                <img src={KubernetesLogo} alt="kubernetes" />
                <Typography variant={'body2'} className={helperClasses.importTypeText}>
                  {t('releases.kubernetes')}
                </Typography>
              </Box>
            </>
          )}
          <Box mt={3}>
            <Typography variant={'body2'}>{t('releases.architecture.importMethod')}</Typography>
            <YBSelect
              className={helperClasses.importTypeSelect}
              fullWidth
              value={defaultImportMethod}
              inputProps={{
                'data-testid': `EditArchitectureModal-ImportSelect`
              }}
              onChange={(e) => {
                onSelectChange(e.target.value);
              }}
            >
              {IMPORT_METHOD_OPTIONS?.map((importMethod) => (
                <MenuItem key={importMethod.label} value={importMethod.value}>
                  {importMethod.label}
                </MenuItem>
              ))}
            </YBSelect>
          </Box>
          {defaultImportMethod === AddReleaseImportMethod.FILE_UPLOAD && (
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
                    label={t('releases.editArchitectureModal.upload')}
                    isUploaded={
                      isNonEmptyString(installationPackageFile?.name)
                        ? isFileUploaded
                        : isNonEmptyString(artifact?.package_file_id)
                    }
                    inputProps={{ accept: ['.gz'] }}
                    uploadedFileName={artifact?.file_name ?? installationPackageFile?.name}
                    progressValue={progressValue}
                    fileSizeLimit={FILE_SIZE_LIMIT}
                    dataTestId={'EditArchitectureModal-InstallerFileUploadButton'}
                    onChange={(e: React.ChangeEvent<HTMLInputElement>) => {
                      if (!e.target?.files?.[0]) {
                        setPackageFileId(undefined);
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
          {defaultImportMethod === AddReleaseImportMethod.URL && (
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
                    defaultValue={artifact?.package_url}
                    helperText={errorHelperText}
                    className={clsx(
                      isNonEmptyString(errorHelperText) && helperClasses.overrideMuiHelperText
                    )}
                    fullWidth
                  />
                </Box>
              </Box>

              <Box mt={2}>
                <YBButton
                  variant="secondary"
                  data-testid="EditArchitectureModal-ReleaseMetadataButton"
                  onClick={fetchReleaseMetadata}
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
              </>
            ))}
        </Box>
      </FormProvider>
    </YBModal>
  );
};
