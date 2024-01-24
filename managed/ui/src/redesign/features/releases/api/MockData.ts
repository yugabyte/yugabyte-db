import { ReleaseState, ReleasePlatform, ReleasePlatformArchitecture, ReleaseType } from "../components/dtos";

export const RELEASE_LIST_DATA = [
  {
    "uuid": "1",
    "release_tag": "hello",
    "schema": "v1", // Hardcoded to v1. Only changes when the metadata schema is updated.  
    "version": "2.20.1.0",
    "yb_type": "yugabyteDB", // If we want to codify the difference between ybdb and yba in metadata
    "artifacts": [
      {
        "location": {
          "package_url": "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/",
          "signature_url": "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
        },
        // "import_method": 'URL',
        "sha256": "<sha256>",
        "platform": ReleasePlatform.LINUX, // linux or vm?
        "architecture": ReleasePlatformArchitecture.ARM, // Only required for linux?
        "signature": "<signature in hex>", // Required once implemented, additional work needed
        // "download_url": "<url>" // This is <only> for the global metadata. Not relevant for air-gapped
      },
      {
        "location": {
          "fileID": "1234",
          "package_file_path": "yugabyte-2.20.1.0-b2-linux-x86_64.tar.gz",
          "signature_file_path": "2.20.1.0-b2-signature.tar.gz"
        },
        // "import_method": 'File Path',
        "sha256": "<sha256>", 
        "platform": ReleasePlatform.KUBERNETES, // linux or vm?
        "architecture": '', // Only required for linux?
        "signature": "<signature in hex>", // Required once implemented, additional work needed
        // "download_url": "<url>" // This is <only> for the global metadata. Not relevant for air-gapped
      }
    ],
    // Optional metadata
    "release_type": ReleaseType.STS,
    "release_date": "09/20/2023",
    "in_use": true,
    "release_notes": "https://docs.yugabyte.com/preview/releases/release-notes/v2.14/#v2.14.14.0",
    "state": ReleaseState.ACTIVE,
    "universes": [{
        "name": 'Universe Name 01',
        "creation_date": "April-25-2023 21:08:33 UTC-400"
      },
      {
        "name": 'Universe Name 02',
        "creation_date": "April-25-2023 21:08:33 UTC-400"
      },
      {
        "name": 'Universe Name 03',
        "creation_date": "April-25-2023 21:08:33 UTC-400"
      }
    ]
  },
    {
      "uuid": "2",
    "schema": "v1", // Hardcoded to v1. Only changes when the metadata schema is updated.  
    "version": "2.14.13.0",
    "release_tag": "",
    "yb_type": "yugabyteDB", // If we want to codify the difference between ybdb and yba in metadata
    "artifacts": [
      {
        // "import_method": 'URL',
        "location": {
          "package_url": "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/",
          "signature_url": "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
        },
        "sha256": "<sha256>", 
        "platform": ReleasePlatform.LINUX, // linux or vm?
        "architecture": ReleasePlatformArchitecture.X86, // Only required for linux?
        "signature": "<signature in hex>", // Required once implemented, additional work needed
        // "download_url": "<url>" // This is <only> for the global metadata. Not relevant for air-gapped
      },
      {
        // "import_method": 'URL',
        "location": {
          "package_url": "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/",
          "signature_url": "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
        },
        "sha256": "<sha256>", 
        "platform": ReleasePlatform.KUBERNETES, // linux or vm?
        "architecture": '', // Only required for linux?
        "signature": "<signature in hex>", // Required once implemented, additional work needed
        // "download_url": "<url>" // This is <only> for the global metadata. Not relevant for air-gapped
      }
    ],
    // Optional metadata
    "release_type": ReleaseType.LTS,
    "release_date": "08/30/2023",
    "in_use": true,
    "release_notes": "https://docs.yugabyte.com/preview/releases/release-notes/v2.14/#v2.14.14.0",
    "state": ReleaseState.ACTIVE,
    "universes": [{
        "name": 'Universe Name 01',
        "creation_date": "April-25-2023 21:08:33 UTC-400"
      },
      {
        "name": 'Universe Name 02',
        "creation_date": "April-25-2023 21:08:33 UTC-400"
      }
    ]
  },
    {
      "uuid": "3",
      "release_tag": "",
    "schema": "v1", // Hardcoded to v1. Only changes when the metadata schema is updated.  
    "version": "2.16.12.0",
    "yb_type": "yugabyteDB", // If we want to codify the difference between ybdb and yba in metadata
    "artifacts": [
      {
        // "import_method": 'File Path',
        "location": {
          "fileID": "1234",
          "package_file_path": "yugabyte-2.16.12.0-b37-linux-x86_64.tar.gz",
          "signature_file_path": "2.16.12.0-b37-signature.tar.gz"
        },
        "sha256": "<sha256>", 
        "platform": ReleasePlatform.LINUX, // linux or vm?
        "architecture": ReleasePlatformArchitecture.ARM, // Only required for linux?
        "signature": "<signature in hex>", // Required once implemented, additional work needed
        // "download_url": "<url>" // This is <only> for the global metadata. Not relevant for air-gapped
      }
    ],
    // Optional metadata
    "release_type": ReleaseType.PREVIEW,
    "release_date": "09/20/2023",
    "in_use": false,
    "release_notes": "https://docs.yugabyte.com/preview/releases/release-notes/v2.14/#v2.14.14.0",
    "state": ReleaseState.DISABLED,
    "universes": []
  },
  {
    "release_tag": "",
    "uuid": "4",
    "schema": "v1", // Hardcoded to v1. Only changes when the metadata schema is updated.  
    "version": "2.18.6.0",
    "yb_type": "yugabyteDB", // If we want to codify the difference between ybdb and yba in metadata
    "artifacts": [
      {
        // "import_method": 'File Path',
        "location": {
          "fileID": "1234",
          "package_file_path": "yugabyte-2.18.6.0-b2-linux-x86_64.tar.gz",
          "signature_file_path": "2.18.6.0-b2-signature.tar.gz"
        },
        "sha256": "<sha256>", 
        "platform": ReleasePlatform.LINUX, // linux or vm?
        "architecture": ReleasePlatformArchitecture.ARM, // Only required for linux?
        "signature": "<signature in hex>", // Required once implemented, additional work needed
        // "download_url": "<url>" // This is <only> for the global metadata. Not relevant for air-gapped
      }
    ],
    // Optional metadata
    "release_type": '',
    "release_date": "09/20/2023",
    "in_use": false,
    "release_notes": "https://docs.yugabyte.com/preview/releases/release-notes/v2.14/#v2.14.14.0",
    "state": ReleaseState.DISABLED,
    "universes": []
  },
];
