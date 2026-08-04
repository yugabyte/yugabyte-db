#!/usr/bin/env bash

./openapi_bundle.sh && \
./openapi_lint.sh && \
java -jar ../../../../openapi-generator/modules/openapi-generator-cli/target/openapi-generator-cli.jar generate \
  -i ../conf/openapi.yml \
  -g go-echo-server \
  -t ../conf/templates/go-echo-server \
  --additional-properties serverPort=1323,hideGenerationTimestamp=true,enumClassPrefix=true,moduleName=apiserver,packageName=YugabyteDB-UI,outputPath=/cmd/server \
  -o ../cmd/server
