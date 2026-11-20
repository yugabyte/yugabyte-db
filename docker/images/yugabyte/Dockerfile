# syntax=docker/dockerfile:1.3-labs
ARG BASE_IMAGE=docker.io/library/almalinux:8
# How the final image is assembled (see the end of this file):
#   flattened - copy the built filesystem into a single layer on top of scratch. Files the build
#               deletes from the base image (e.g. the rfcurated base's rf-* repos) are then really
#               gone, instead of only hidden while still present in the base layers.
#   layered   - keep the base image layers untouched and add ours on top. Required for Red Hat
#               certification: preflight's BasedOnUbi check looks for the unmodified Red Hat
#               base layer. `ybcloud.sh docker image create --openshift_certifiable` selects it.
ARG FINAL_STAGE=flattened
# Use following command to create almalinux based image
# docker build --build-arg BASE_IMAGE=docker.io/library/almalinux:8
# Use following command to create UBI-8 based docker image
# docker build --build-arg BASE_IMAGE=registry.access.redhat.com/ubi8/ubi
# Use following command to create a UBI-9 based image that can pass Red Hat certification
# docker build --build-arg BASE_IMAGE=registry.access.redhat.com/ubi9/ubi \
#   --build-arg FINAL_STAGE=layered --build-arg USER=yugabyte

FROM $BASE_IMAGE as stage1

ENV YB_HOME=/home/yugabyte
WORKDIR $YB_HOME

COPY packages/yugabyte*.tar.gz /tmp/

RUN <<EOF
#!/usr/bin/env bash
set -ex
dnf install -y file
tar -xvf /tmp/yugabyte*$(uname -m)*.tar.gz --strip 1
rm -rf /tmp/yugabyte*
for a in $(find . -exec file {} \; | grep -i elf | cut -f1 -d:); do
  strip --strip-unneeded $a || true
done
EOF

RUN tar -xvf share/ybc-*.tar.gz \
    && rm -rf ybc-*/conf/

# stage2 adds exactly two layers to the base image: the platform (OS packages, Python modules,
# locales, tini, licenses) and the YugabyteDB release. Every RUN and COPY adds a layer, so new steps
# belong in one of the two RUNs below, not in a RUN of their own.
FROM $BASE_IMAGE as stage2
ARG BASE_IMAGE
ARG VERSION=Development
ARG RELEASE=Unknown
ARG USER=root
ARG YBC_VERSION=Unknown

LABEL name="YugabyteDB" \
    maintainer="YugaByte" \
    vendor="YugabyteDB, Inc" \
    release=$RELEASE \
    version=$VERSION \
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
ARG yum_install="yum install --disableplugin=subscription-manager -y"
ARG yum_upgrade="yum upgrade --disableplugin=subscription-manager -y"
ARG RF_HARDENED_BASE_PATTERN=ubi9-fips-rfcurated
ARG TINI_VERSION=v0.19.0
ARG ghr_url=https://raw.githubusercontent.com/yugabyte/yugabyte-db/master

# Layer 1 of 2: the platform. Nothing here depends on the YugabyteDB release.
RUN <<'EOF'
#!/usr/bin/env bash
set -ex
is_rf_base=false
if [[ "$BASE_IMAGE" == *"$RF_HARDENED_BASE_PATTERN"* ]]; then
  is_rf_base=true
fi

# Add the almalinux8 package repo if we are building for ubi8.
if [[ "$BASE_IMAGE" == "registry.access.redhat.com/ubi8"* ]]; then
  cat <<- "EOR" > /etc/yum.repos.d/almalinux8.repo
[almalinux8-appstream]
name=Almalinux8-appstream
baseurl=https://repo.almalinux.org/almalinux/8/AppStream/$basearch/os/
gpgcheck=1
enabled=1
gpgkey=https://repo.almalinux.org/almalinux/RPM-GPG-KEY-AlmaLinux-8

[almalinux8-baseos]
name=Almalinux8-baseos
baseurl=https://repo.almalinux.org/almalinux/8/BaseOS/$basearch/os/
gpgcheck=1
enabled=1
gpgkey=https://repo.almalinux.org/almalinux/RPM-GPG-KEY-AlmaLinux-8
EOR
fi

# Ensure our base system is fully up to date
if ! $is_rf_base; then
  $yum_upgrade
else
  echo "Skipping yum upgrade for hardened base image: $BASE_IMAGE"
fi

# Install required packages that are universal across RHEL versions
packages=(
  bind-utils
  diffutils
  gettext
  glibc-all-langpacks
  glibc-langpack-en
  glibc-locale-source
  less
  ncurses-devel
  net-tools
  openssl
  openssl-devel
  rsync
  procps
  tcpdump
  which
  hostname
)
# Not in the UBI 9 repos. Nothing in the release links ncurses 5, and the support bundle runs mpstat
# (sysstat) only when it is installed.
if [[ "$BASE_IMAGE" != "registry.access.redhat.com/ubi9"* ]]; then
  packages+=(iotop ncurses-compat-libs sysstat)
fi
if ! $is_rf_base; then
  packages+=(python3.11-devel python3.11-pip redhat-rpm-config)
fi
$yum_install "${packages[@]}"

if ! $is_rf_base; then
  if grep -q '^PLATFORM_ID="platform:el8"' /etc/os-release; then
    alternatives --set python3 /usr/bin/python3.11
    alternatives --set python /usr/bin/python3.11
  else
    # EL9 has no python3 alternative, and /usr/bin/python3 belongs to the base image's python3
    # (3.9) RPM: repointing it fails preflight HasModifiedFiles. Put python3.11 first on PATH
    # instead, through links that no RPM owns.
    ln -s /usr/bin/python3.11 /usr/local/bin/python3
    ln -s /usr/bin/python3.11 /usr/local/bin/python
  fi
fi
python3 -m pip install --upgrade pip
python3 -m pip install --upgrade lxml
python3 -m pip install --upgrade s3cmd
python3 -m pip install --upgrade psutil
python3 -m pip install --upgrade watchdog
if $is_rf_base; then
  yum remove --disableplugin=subscription-manager -y python3-pip
else
  rpm -e python3.11-pip
fi
rm -rf /usr/share/python3-wheels/pip-9.0.3-py2.py3-none-any.whl

languages=("en_US" "de_DE" "es_ES" "fr_FR" "it_IT" "ja_JP"
           "ko_KR" "pl_PL" "ru_RU" "sv_SE" "tr_TR" "zh_CN")
for lang in "${languages[@]}"; do
  localedef --quiet --force --inputfile="${lang}" --charmap=UTF-8 "${lang}.UTF-8"
done

if [[ "$BASE_IMAGE" == "registry.access.redhat.com/ubi"* ]] || $is_rf_base; then
  useradd --home-dir $YB_HOME \
          --uid 10001 \
          --shell /sbin/nologin \
          --no-create-home \
          --no-user-group yugabyte
fi

# Copy licenses
mkdir /licenses
curl -fsSL ${ghr_url}/LICENSE.md -o /licenses/LICENSE.md
curl -fsSL ${ghr_url}/licenses/APACHE-LICENSE-2.0.txt -o /licenses/APACHE-LICENSE-2.0.txt
curl -fsSL ${ghr_url}/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt \
  -o /licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

[[ "$(uname -m)" == "aarch64" ]] && arch='arm64' || arch='amd64'
curl -fsSL -o /sbin/tini \
  https://github.com/krallin/tini/releases/download/${TINI_VERSION}/tini-${arch}
chmod +x /sbin/tini
curl -fsSL -o /tmp/tini.sha256sum \
  https://github.com/krallin/tini/releases/download/${TINI_VERSION}/tini-${arch}.sha256sum
egrep $(sha256sum /sbin/tini) /tmp/tini.sha256sum
rm -f /tmp/tini.sha256sum

rm -rf ~/.cache
yum clean all
rm -rf /var/cache/yum /var/cache/dnf
# Remove repos for hardened images. In a layered image they would remain in the base layers; the
# flattened final stage drops them for good.
if $is_rf_base; then
  rm -rf /etc/yum.repos.d/rf-*
fi
EOF

# Layer 2 of 2: the YugabyteDB release. stage1 is bind-mounted instead of COPY'd so that the release
# and the links below land in a single layer.
RUN --mount=type=bind,from=stage1,source=/home/yugabyte,target=/mnt/stage1 <<'EOF'
#!/usr/bin/env bash
set -ex
cp -a /mnt/stage1/. $YB_HOME/

# Create with correct permissions the openssl-config directory which will be used for fips
# credentials generated via post-install.sh
mkdir -p $YB_HOME/openssl-config
chown -R 10001:0 $YB_HOME/openssl-config
chmod -R g=u $YB_HOME/openssl-config

for a in ysqlsh ycqlsh yugabyted yb-admin yb-tsi-cli; do
  ln -s /home/yugabyte/bin/$a /usr/local/bin/$a
done

# In the normal EE flows, we expect /home/yugabyte/{master,tserver} to exist and have both links
# to all the components in the unpacked tar.gz, as well as an extra link to the log path for the
# respective server.
dirs=$(ls /home/yugabyte | grep -v "^ybc-")
mkdir $YB_HOME/{master,tserver}
# Link all YB pieces.
for dir in $dirs; do ln -s $YB_HOME/$dir $YB_HOME/master/$dir; done
for dir in $dirs; do ln -s $YB_HOME/$dir $YB_HOME/tserver/$dir; done
# Link the logs.
ln -s /mnt/disk0/yb-data/master/logs $YB_HOME/master/logs
ln -s /mnt/disk0/yb-data/tserver/logs $YB_HOME/tserver/logs
# Create and link the cores.
mkdir -p /mnt/disk0/cores && ln -s /mnt/disk0/cores $YB_HOME/cores

mkdir $YB_HOME/controller
# Find ybc-* directory
YBC_DIR=$(find "$YB_HOME" -maxdepth 1 -type d -name 'ybc-*')
# Link bin directory
ln -s ${YBC_DIR}/bin $YB_HOME/controller/bin
# Link the logs
ln -s /mnt/disk0/ybc-data/controller/logs $YB_HOME/controller/logs

chown 10001:0 $YB_HOME
chmod g=u $YB_HOME
EOF

FROM stage2 AS layered

FROM scratch AS flattened

COPY --from=stage2 / /

FROM ${FINAL_STAGE}
ARG VERSION=Development
ARG RELEASE=Unknown
ARG USER=root
ARG YBC_VERSION=Unknown

LABEL name="YugabyteDB" \
    maintainer="YugaByte" \
    vendor="YugabyteDB, Inc" \
    release=$RELEASE \
    version=$VERSION \
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

# Expose the required ports.
EXPOSE 7000 7100 7200 9000 9100 12000 11000 6379 9042 5433 10100 18018

# Required for Red Hat certification
USER ${USER}

# Create the data directories.
VOLUME ["/mnt/disk0", "/mnt/disk1"]

ENTRYPOINT ["/sbin/tini", "--"]
