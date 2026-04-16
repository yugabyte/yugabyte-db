To publish this jar to the S3 repo repository.yugabyte.com, run

```
mvn deploy -s ../settings.xml -DskipTests
```

with appropriate AWS_* environment variables set to authenticate to the S3 repo.
