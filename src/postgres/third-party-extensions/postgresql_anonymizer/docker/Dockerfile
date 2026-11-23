
# This instructions must be declared before any FROM
# You can override it and build your own image based another version
# like this:
#
#     PG_MAJOR_VERSION=17 ANON_MINOR_VERSION=3.1.0 make docker_image
#
ARG DOCKER_PG_MAJOR_VERSION=18

FROM postgres:$DOCKER_PG_MAJOR_VERSION

# An ARG declared before a FROM is outside of a build stage, so it can’t be
# used in any instruction after a FROM. We need to declare it again.
ARG DOCKER_PG_MAJOR_VERSION=18

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates \
      wget \
 && rm -rf /var/lib/apt/lists/*

# Install anon extension
ARG ANON_MINOR_VERSION
ARG DEB=postgresql_anonymizer_${DOCKER_PG_MAJOR_VERSION}_${ANON_MINOR_VERSION}_amd64.deb
RUN wget https://gitlab.com/api/v4/projects/7709206/packages/generic/release-assets/${ANON_MINOR_VERSION}/${DEB} \
 && dpkg -i ${DEB}

# init script
RUN mkdir -p /docker-entrypoint-initdb.d
COPY ./docker/init_anon.sh /docker-entrypoint-initdb.d/init_anon.sh

# Entrypoint for Anonymized Dumps
COPY docker/dump.sh /dump.sh
