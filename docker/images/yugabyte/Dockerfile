ARG BASE_IMAGE=docker.io/library/almalinux:8
# Use following command to create docker image
# docker build --build-arg BASE_IMAGE=docker.io/centos:7 .
# Use following command to create UBI-based docker image
# docker build --build-arg BASE_IMAGE=registry.access.redhat.com/ubi7/ubi:7.9
# Use following command to create almalinux based image
# docker build --build-arg BASE_IMAGE=docker.io/library/almalinux:8
# Use following command to create UBI-8 based docker image
# docker build --build-arg BASE_IMAGE=registry.access.redhat.com/ubi8/ubi

FROM $BASE_IMAGE as intermediate

ENV YB_HOME=/home/yugabyte
WORKDIR $YB_HOME

COPY packages/yugabyte*.tar.gz /tmp/

RUN tar -xvf /tmp/yugabyte*$(uname -m)*.tar.gz --strip 1 \
    && rm -rf /tmp/yuabyte* \
    && bin/post_install.sh

FROM $BASE_IMAGE
ARG BASE_IMAGE
ARG VERSION=Development
ARG RELEASE=Unknown
ARG USER=root
# `--build-arg JAVA_VERSION=11` to use java 11
ARG JAVA_VERSION=1.8.0

LABEL name="YugabyteDB" \
    maintainer="YugaByte" \
    vendor="Yugabyte Inc" \
    release=$RELEASE \
    version=$VERSION \
    java_version=$JAVA_VERSION \
    summary="YugabytDB Image" \
    description="YugabyteDB is a free and open-source, distributed, relational, NewSQL database \
        management system designed to handle large amounts of data spanning across multiple \
        availability zones and geographic regions while providing single-digit latency, \
        high availability, and no single point of failure."

ENV container=yugabyte-db
ENV YB_HOME=/home/yugabyte
ENV YB_MANAGED_DEVOPS_USE_PYTHON3=1
ENV YB_DEVOPS_USE_PYTHON3=1
WORKDIR $YB_HOME

# Create some yum aliases for use in this Dockerfile
ARG yum_install="yum install --disableplugin=subscription-manager"
ARG yum_upgrade="yum upgrade --disableplugin=subscription-manager"

# Install required python packages first
# this should leave us with python3.8 as the default python3
RUN set -x ; \
    if [[ "$BASE_IMAGE" == "centos:7" ]]; then \
      $yum_install -y install centos-release-scl-rh && \
      yum_install="$yum_install --enablerepo=centos-sclo-rh"; \
    fi && \
    if [[ "$BASE_IMAGE" == "registry.access.redhat.com/ubi7/ubi"* \
          || "$BASE_IMAGE" == "centos:7" ]]; then \
      $yum_install -y \
        rh-python38 \
        rh-python38-python-setuptools \
        rh-python38-python-devel \
        rh-python38-python-pip \
        rh-python38-python-psutil \
      && echo "source /opt/rh/rh-python38/enable" >> /etc/bashrc \
      && source /opt/rh/rh-python38/enable; \
    else \
      $yum_install -y  \
        python38-devel \
        python38-pip \
        python38-psutil; \
    fi \
    && pip3 install --upgrade pip \
    && pip3 install --upgrade lxml

# Install required packages that are universal across RHEL versions
# This pulls in python3.6 as a dependency and then makes it default python even overriding
# anything we may have set previously so we need to set py3.8 as default at the end of this block.
RUN $yum_install -y \
      bind-utils \
      diffutils \
      glibc-all-langpacks \
      glibc-langpack-en \
      glibc-locale-source \
      java-${JAVA_VERSION}-openjdk-headless \
      less \
      ncurses-compat-libs \
      ncurses-devel \
      net-tools \
      openssl \
      openssl-devel \
      redhat-rpm-config \
      rsync \
      procps \
      which \
    && alternatives --set python3 /usr/bin/python3.8 \
    && alternatives --set python /usr/bin/python3.8 \
    && yum clean all && rm -rf /var/cache/yum

COPY CentOS.repo /etc/yum.repos.d/CentOS.repo
COPY almalinux8*.repo /etc/yum.repos.d/

RUN /bin/bash -O extglob -c 'rm -f /etc/yum.repos.d/almalinux8-!($(uname -m)).repo'

ARG utils="s3cmd sysstat iotop tcpdump fio"
RUN set -x; \
    if [[ "$BASE_IMAGE" == "registry.access.redhat.com/ubi7"*  ]]; then \
        $yum_install -y http://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm && \
        $yum_install -y --enablerepo=epel-* --enablerepo=centos-* $utils && \
        $yum_upgrade -y --enablerepo=epel-* --enablerepo=centos-*; \
    elif [[ "$BASE_IMAGE" == "registry.access.redhat.com/ubi8"* ]]; then \
        $yum_install -y http://dl.fedoraproject.org/pub/epel/epel-release-latest-8.noarch.rpm && \
        $yum_install -y --enablerepo=epel-* --enablerepo=almalinux8-* $utils && \
        $yum_upgrade -y --enablerepo=epel-* --enablerepo=almalinux8-*; \
    elif [[ "$BASE_IMAGE" == *"almalinux"* ]]; then \
        $yum_install -y epel-release && \
        $yum_install -y $utils && \
        $yum_upgrade -y; \
    else \
        $yum_install -y http://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm && \
        $yum_install -y $utils && \
        $yum_upgrade -y ; \
    fi \
    && yum clean all && rm -rf /var/cache/yum

RUN set -x; \
    languages=("en_US" "de_DE" "es_ES" "fr_FR" "it_IT" "ja_JP" \
               "ko_KR" "pl_PL" "ru_RU" "sv_SE" "tr_TR" "zh_CN"); \
    for lang in "${languages[@]}"; do \
      localedef --quiet --force --inputfile="${lang}" --charmap=UTF-8 "${lang}.UTF-8"; \
    done

RUN set -x; \
    if [[ "$BASE_IMAGE" == "registry.access.redhat.com/ubi"* ]]; then \
      useradd --home-dir $YB_HOME \
              --uid 10001 \
              --shell /sbin/nologin \
              --no-create-home \
              --no-user-group yugabyte; \
    fi

COPY --from=intermediate $YB_HOME $YB_HOME

RUN ln -s /home/yugabyte/bin/ysqlsh /usr/local/bin/ysqlsh && \
    ln -s /home/yugabyte/bin/cqlsh /usr/local/bin/ycqlsh && \
    ln -s /home/yugabyte/bin/yugabyted /usr/local/bin/yugabyted && \
    ln -s /home/yugabyte/bin/yb-admin /usr/local/bin/yb-admin && \
    ln -s /home/yugabyte/bin/yb-ts-cli /usr/local/bin/yb-ts-cli

# In the normal EE flows, we expect /home/yugabyte/{master,tserver} to exist and have both links
# to all the components in the unpacked tar.gz, as well as an extra link to the log path for the
# respective server.
RUN export dirs=$(ls /home/yugabyte) && mkdir $YB_HOME/{master,tserver} && \
    # Link all YB pieces.
    for dir in $dirs; do ln -s $YB_HOME/$dir $YB_HOME/master/$dir; done && \
    for dir in $dirs; do ln -s $YB_HOME/$dir $YB_HOME/tserver/$dir; done && \
    # Link the logs.
    ln -s /mnt/disk0/yb-data/master/logs $YB_HOME/master/logs && \
    ln -s /mnt/disk0/yb-data/tserver/logs $YB_HOME/tserver/logs && \
    # Create and link the cores.
    mkdir -p /mnt/disk0/cores && ln -s /mnt/disk0/cores $YB_HOME/cores

# Copy licenses
ARG ghr_url=https://raw.githubusercontent.com/yugabyte/yugabyte-db/master
RUN mkdir /licenses && \
    curl ${ghr_url}/LICENSE.md -o /licenses/LICENSE.md && \
    curl ${ghr_url}/licenses/APACHE-LICENSE-2.0.txt -o /licenses/APACHE-LICENSE-2.0.txt && \
    curl ${ghr_url}/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt \
      -o /licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# Install azcopy
ARG AZV=10.19.0
RUN set -x; \
    [[ "$(uname -m)" == "aarch64" ]] && arch='arm64' || arch='amd64'; \
    pkg_name="azcopy_linux_${arch}_${AZV}"; \
    mkdir /tmp/az && \
    curl -o /tmp/az/${pkg_name}.tar.gz \
      "https://downloads.yugabyte.com/mirror/azcopy/${pkg_name}.tar.gz" && \
    tar -xzf /tmp/az/${pkg_name}.tar.gz \
      --strip-components=1 \
      -C /usr/bin \
      ${pkg_name}/azcopy && \
    rm -rf /tmp/az

# Install gsutil
ARG GSV=4.60
RUN set -x; \
    mkdir /tmp/gsutils && \
    curl -o /tmp/gsutils/gsutil_${GSV}.tar.gz \
      https://storage.googleapis.com/pub/gsutil_${GSV}.tar.gz && \
    tar -xzf /tmp/gsutils/gsutil_${GSV}.tar.gz -C /usr/local/ && \
    chown :root -R /usr/local/gsutil

# Configure azcopy and gsutil
RUN mkdir /home/yugabyte/.boto \
    && echo -e "[GSUtil]\nstate_dir=/tmp/gsutil" > /home/yugabyte/.boto/config \
    && ln -s /usr/local/gsutil/gsutil /usr/bin/gsutil \
    && mkdir -m 777 /tmp/azcopy

ENV BOTO_PATH=/home/yugabyte/.boto/config
ENV AZCOPY_JOB_PLAN_LOCATION=/tmp/azcopy/jobs-plan
ENV AZCOPY_LOG_LOCATION=/tmp/azcopy/logs

ARG TINI_VERSION=v0.19.0
RUN set -x; \
    [[ "$(uname -m)" == "aarch64" ]] && arch='arm64' || arch='amd64'; \
    curl -L -o /sbin/tini \
      https://github.com/krallin/tini/releases/download/${TINI_VERSION}/tini-${arch} \
    && chmod +x /sbin/tini \
    && curl -L -o /tmp/tini.sha256sum \
      https://github.com/krallin/tini/releases/download/${TINI_VERSION}/tini-${arch}.sha256sum \
    && egrep $(sha256sum /sbin/tini) /tmp/tini.sha256sum

# Expose the required ports.
EXPOSE 7000 7100 7200 9000 9100 12000 11000 6379 9042 5433 10100

# Required for Red Hat certification
USER ${USER}

# Create the data directories.
VOLUME ["/mnt/disk0", "/mnt/disk1"]

ENTRYPOINT ["/sbin/tini", "--"]
