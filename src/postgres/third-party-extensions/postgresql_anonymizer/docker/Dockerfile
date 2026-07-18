
# This instructions must be declared before any FROM
# You can override it and build your own image based another version
# like this: `PG_MAJOR_VERSION=11 make docker_image`
ARG PG_MAJOR_VERSION=14

##
## First Stage: build pg_dump_anon
##
FROM golang:latest AS gobuilder
WORKDIR /go/src
COPY pg_dump_anon .
RUN CGO_ENABLED=0 GOOS=linux go build .

##
## Second Stage
##
ARG PG_MAJOR_VERSION=14
FROM postgres:$PG_MAJOR_VERSION

# An ARG declared before a FROM is outside of a build stage, so it can’t be
# used in any instruction after a FROM. We need to declare it again.
ARG PG_MAJOR_VERSION

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    git \
    make \
    postgresql-server-dev-$PG_MAJOR_VERSION \
    postgresql-plpython3-$PG_MAJOR_VERSION \
    pgxnclient \
    libc6-dev \
    wget \
    python3 \
    python3-pip \
    zip \
 && rm -rf /var/lib/apt/lists/*

# Import source files into the image
WORKDIR /src
COPY . .

# Install PostgreSQL Faker
RUN pip3 install pip --upgrade --break-system-packages && \
    pip3 install --no-cache-dir -r python/requirements.txt --break-system-packages && \
    pgxn install postgresql_faker

# Install anon extension
RUN make clean && make && make install

# Install pg_dump_anon from the previous stage
COPY --from=gobuilder /go/src/pg_dump_anon /usr/bin/

# init script
RUN mkdir -p /docker-entrypoint-initdb.d
COPY ./docker/init_anon.sh /docker-entrypoint-initdb.d/init_anon.sh

# Alternative Entrypoint
COPY docker/anon.sh /anon.sh
