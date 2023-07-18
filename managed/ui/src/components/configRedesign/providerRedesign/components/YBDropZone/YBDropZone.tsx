/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { useDropzone } from 'react-dropzone';
import clsx from 'clsx';

import { YBButton } from '../../../../../redesign/components';

import styles from './YBDropZone.module.scss';

export type YBDropZoneProps = {
  actionButtonText?: string;
  className?: string;
  dataTestId?: string;
  descriptionText?: string;
  disabled?: boolean;
  dragOverText?: string;
  name?: string;
  showHelpText?: boolean;
} & (
  | {
      multipleFiles: false;
      value?: File;
      onChange?: (file: File | undefined) => void;
    }
  | {
      multipleFiles: true;
      value?: File[];
      onChange?: (file: File[]) => void;
    }
);

const DROP_ZONE_ICON = <i className={`fa fa-upload ${styles.dropZoneIcon}`} />;

export const YBDropZone = ({
  actionButtonText,
  className,
  dataTestId,
  descriptionText,
  disabled,
  dragOverText,
  multipleFiles,
  onChange,
  name,
  showHelpText = true,
  value
}: YBDropZoneProps) => {
  const onDropAccepted = (acceptedFiles: File[]) => {
    onChange &&
      (multipleFiles === true
        ? onChange(value ? acceptedFiles.concat(value) : acceptedFiles)
        : onChange(acceptedFiles[0]));
  };
  const onClearUploads = () => {
    onChange && (multipleFiles === true ? onChange([]) : onChange(undefined));
  };
  const { getRootProps, getInputProps, open, isDragActive } = useDropzone({
    onDropAccepted,
    multiple: multipleFiles,
    noClick: true,
    noKeyboard: true,
    disabled: disabled
  });

  const storedFiles = value ? (multipleFiles ? value : [value]) : [];
  const dataTestIdPrefix = dataTestId && dataTestId !== '' ? dataTestId : 'YBDropZone';
  return storedFiles.length === 0 ? (
    <div
      className={clsx(styles.dropZoneContainer, isDragActive && styles.activeDrag, className)}
      {...getRootProps()}
    >
      <input name={name} {...getInputProps()} />
      {DROP_ZONE_ICON}
      <YBButton variant="secondary" onClick={open} data-testid={`${dataTestIdPrefix}-uploadButton`}>
        <i className="fa fa-upload" />
        {actionButtonText ?? 'Upload'}
      </YBButton>
      {showHelpText && (
        <div className={styles.subText}>
          {isDragActive
            ? dragOverText ?? 'Drop file here to upload.'
            : descriptionText ?? 'Drag a file into the box to upload.'}
        </div>
      )}
    </div>
  ) : (
    <div className={clsx('updateFilesContainer', styles.updateFilesContainer)}>
      <div className={styles.acceptedFilesMetadataContainer}>
        {storedFiles.map((acceptedFile) => (
          <li>{acceptedFile.name}</li>
        ))}
      </div>
      <YBButton variant="secondary" onClick={open} data-testid={`${dataTestIdPrefix}-uploadButton`}>
        <i className="fa fa-upload" />
        {'Upload'}
      </YBButton>
      <YBButton
        variant="secondary"
        onClick={onClearUploads}
        data-testid={`${dataTestIdPrefix}-clearUploadsButton`}
      >
        <i className="fa fa-trash" />
        {'Clear'}
      </YBButton>
    </div>
  );
};
