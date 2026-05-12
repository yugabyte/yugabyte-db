import boto3
from typing import List, Dict, Optional
from urllib.parse import quote


class S3BucketCrawler:
    """
    Helper class to recursively fetch S3 objects' metadata under a given prefix (folder).
    """

    def __init__(self, aws_access_key_id: Optional[str] = None,
                 aws_secret_access_key: Optional[str] = None, aws_region: Optional[str] = None):
        session_kwargs = {}
        if aws_access_key_id and aws_secret_access_key:
            session_kwargs['aws_access_key_id'] = aws_access_key_id
            session_kwargs['aws_secret_access_key'] = aws_secret_access_key
        if aws_region:
            session_kwargs['region_name'] = aws_region
        self.session = boto3.session.Session(**session_kwargs)
        self.s3 = self.session.client("s3")

    def list_all_objects(self, bucket_name: str, prefix: str = "") -> List[Dict]:
        """
        Recursively retrieves all metadata for objects under the given prefix (folder).
        Returns a list of S3 object metadata dicts from ListObjectsV2.
        """
        objects = []
        continuation_token = None
        while True:
            list_kwargs = {
                "Bucket": bucket_name,
                "Prefix": prefix,
                "MaxKeys": 1000,
            }
            if continuation_token:
                list_kwargs["ContinuationToken"] = continuation_token
            response = self.s3.list_objects_v2(**list_kwargs)
            contents = response.get("Contents", [])
            objects.extend(contents)
            if response.get("IsTruncated"):
                continuation_token = response["NextContinuationToken"]
            else:
                break
        return objects

    def get_folder_files_metadata(self, bucket_name: str, folder_prefix: str = "") -> List[Dict]:
        """
        Recursively get metadata for all files in the given S3 "folder" (prefix).
        Ignores 'directories' as S3 is object storage - everything is a key.
        """
        all_objects = self.list_all_objects(bucket_name=bucket_name, prefix=folder_prefix)
        # Optionally, filter out 'directory' objects (keys that end with '/')
        file_objects = [obj for obj in all_objects if not obj["Key"].endswith('/')]
        return file_objects

    def build_s3_uri(self, bucket_name: str, key: str, aws_region: Optional[str] = None) -> str:
        """
        Build a full S3 URI from bucket name and object key.
        Format: s3://bucket-name/key or s3://bucket-name/key (with region for cross-region)
        """
        return f"s3://{bucket_name}/{key}"
