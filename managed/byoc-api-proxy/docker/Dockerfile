# Copyright (c) YugabyteDB, Inc.
#
# BYOC API proxy image. Built from the release tarball rather than from source, so the image runs
# the same jar that is published as byoc_api_proxy-<version>.tar.gz. Use docker/build-image.sh.
#
# Configuration comes from the environment variables in src/main/resources/application.yaml
# (YBA_UUID, YBA_BASE_URL, PROXIED_APP_BASE_URL, ...). Logs go to stdout.

# Same RF-curated FIPS base as the other YugabyteDB Anywhere images.
ARG BASE_IMAGE=quay.io/yugabyte/rfcurated-python:ubi9-fips-rfcurated

FROM ${BASE_IMAGE}
ARG VERSION

# The base carries no JRE and no unprivileged user. Group 0 lets OpenShift's arbitrary UIDs run it.
RUN dnf --assumeyes --setopt=install_weak_deps=0 install java-17-openjdk-headless \
    && dnf clean all \
    && useradd --uid 10001 --gid 0 --no-create-home --home-dir /opt/yugabyte/byoc-api-proxy \
        byoc-api-proxy

# ADD unpacks the tarball, which holds <version>/bin/byoc-api-proxy.jar.
ADD byoc_api_proxy-${VERSION}.tar.gz /opt/yugabyte/byoc-api-proxy/
RUN ln -s /opt/yugabyte/byoc-api-proxy/${VERSION}/bin/byoc-api-proxy.jar \
        /opt/yugabyte/byoc-api-proxy/byoc-api-proxy.jar

USER 10001
WORKDIR /opt/yugabyte/byoc-api-proxy

# Same JVM flags as the systemd unit installed by yba-ctl.
ENTRYPOINT ["java", \
    "-Djdk.internal.httpclient.disableHostnameVerification=true", \
    "-XX:+ExitOnOutOfMemoryError", \
    "-jar", "/opt/yugabyte/byoc-api-proxy/byoc-api-proxy.jar"]
