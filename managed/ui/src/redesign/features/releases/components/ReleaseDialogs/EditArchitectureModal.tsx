import { useEffect, useState } from 'react';
import { useForm, FormProvider } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { Box, MenuItem, Typography, makeStyles } from '@material-ui/core';
import { YBInputField, YBModal, YBSelect } from '../../../../components';
import { YBFileUpload } from '../../../../components/YBFileUpload/YBFileUpload';
import { isNonEmptyString } from '../../../../../utils/ObjectUtils';
import {
  AddReleaseImportMethod,
  ReleaseArtifacts,
  ReleaseFormFields,
  ReleasePlatform,
  ReleasePlatformArchitecture,
  Releases
} from '../dtos';
import { IMPORT_METHOD_OPTIONS } from '../../helpers/constants';

import KubernetesLogo from '../../../../../redesign/assets/kubernetes.svg';

interface EditKubernetesModalProps {
  data: Releases | null;
  open: boolean;
  modalTitle: string;
  releaseArchitecture: ReleasePlatformArchitecture;
  onClose: () => void;
  onActionPerformed: () => void;
}

const FILE_SIZE_LIMIT = 1048576000; // 1000 MB

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
  }
}));

export const EditKubernetesModal = ({
  data,
  open,
  modalTitle,
  releaseArchitecture,
  onClose,
  onActionPerformed
}: EditKubernetesModalProps) => {
  const { t } = useTranslation();
  const helperClasses = useStyles();

  const artifact =
    releaseArchitecture === ReleasePlatformArchitecture.KUBERNETES
      ? data?.artifacts?.find(
          (artifact: ReleaseArtifacts) => artifact.platform === ReleasePlatform.KUBERNETES
        )
      : data?.artifacts?.find(
          (artifact: ReleaseArtifacts) => artifact.architecture === releaseArchitecture
        );
  const importMethod = artifact?.location.package_url
    ? AddReleaseImportMethod.URL
    : AddReleaseImportMethod.FILE_UPLOAD;

  const [defaultImportMethod, setDefaultImportMethod] = useState<AddReleaseImportMethod | string>(
    importMethod
  );

  const formMethods = useForm<ReleaseFormFields>({
    defaultValues: {
      importMethod: importMethod,
      installationPackageUrl: artifact?.location.package_url ?? '',
      signatureUrl: artifact?.location.signature_url ?? '',
      // TODO: Check with Daniel if the file will be returned since installationPackageFile type is File
      installationPackageFile: artifact?.location.package_file_path,
      // TODO: Check with Daniel if the file will be returned since SignatureFile type is File
      signatureFile: artifact?.location.signature_file_path,
      releaseTag: data?.release_tag,
      version: data?.version,
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
  const signatureUrlValue = watch('signatureUrl');
  const installationPackageFileValue = watch('installationPackageFile');
  const signatureFileValue = watch('signatureFile');

  useEffect(() => {
    if (
      importMethodValue === AddReleaseImportMethod.URL &&
      isNonEmptyString(signatureUrlValue) &&
      isNonEmptyString(installationPackageUrlValue)
    ) {
      // TODO: Make an API call to ensure it returns the list of releases - fetchUrlReleaseArtifact (GET) from api.ts
    }

    if (importMethodValue === AddReleaseImportMethod.FILE_UPLOAD && signatureFileValue) {
      // TODO: Make an API call to ensure it returns the list of releases - fetchFileReleaseArtifact (GET) from api.ts
    }

    if (importMethodValue === AddReleaseImportMethod.FILE_UPLOAD && installationPackageFileValue) {
      // TODO: Make an API call to ensure it returns the list of releases - uploadReleaseArtifact (POST) from api.ts
    }
  }, [
    importMethodValue,
    installationPackageUrlValue,
    signatureUrlValue,
    installationPackageFileValue,
    signatureFileValue
  ]);

  const handleFormSubmit = handleSubmit((formValues) => {
    // TODO: Write useMutation to make API call to ensure we can edit the release succesfully  - updateReleaseMetadata (PUT) from api.ts
    // Confirm the above with Shubin
    // TODO: onSuccess on above mutation call, ensure to call onActionPerformed() which will get fresh set of releasaes
    // to be displayed in ReleaseList page
  });

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
      titleContentProps={helperClasses.modalTitle}
      buttonProps={{
        primary: {
          disabled: !isDirty
        }
      }}
    >
      <FormProvider {...formMethods}>
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
                'data-testid': `EditKubernetesModal-ImportSelect`
              }}
              onChange={(e) => {
                const selectedImportMethod = e.target.value;
                setDefaultImportMethod(selectedImportMethod);
                setValue('importMethod', selectedImportMethod);
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
                    isUploaded={true}
                    dataTestId={'EditKubernetesModal-InstallerFileUploadButton'}
                    uploadedFileName={artifact?.location.package_file_path}
                    // inputProps={{ multiple: zendeskFileAttachmentCount > 1 }}
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
                  {'Supported file type: tar.gz'}
                </Typography>
                <Box mt={2}>
                  <YBFileUpload
                    name={'signatureFile'}
                    className={helperClasses.upload}
                    label={t('releases.editArchitectureModal.upload')}
                    isUploaded={true}
                    dataTestId={'EditKubernetesModal-InstallerFileUploadButton'}
                    // inputProps={{ multiple: zendeskFileAttachmentCount > 1 }}
                    uploadedFileName={artifact?.location.signature_file_path}
                    fileSizeLimit={FILE_SIZE_LIMIT}
                    onChange={(e: React.ChangeEvent<HTMLInputElement>) => {}}
                  />
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
                    defaultValue={artifact?.location.package_url}
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
                    defaultValue={artifact?.location.signature_url}
                    name="signatureUrl"
                    fullWidth
                  />
                </Box>
              </Box>
            </Box>
          )}
        </Box>
      </FormProvider>
    </YBModal>
  );
};
