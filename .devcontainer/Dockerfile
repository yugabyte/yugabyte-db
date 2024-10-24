FROM ubuntu:22.04

ENV DEBIAN_FRONTEND="noninteractive"
ENV TZ="Europe/Istanbul"

ARG PG_MAJOR=17

# install deps
RUN apt-get update && apt-get -y install build-essential libreadline-dev zlib1g-dev    \
                                         flex bison libxml2-dev libxslt-dev libssl-dev \
                                         libxml2-utils xsltproc ccache pkg-config wget \
                                         curl lsb-release sudo nano net-tools git awscli

# install Postgres
RUN sh -c 'echo "deb https://apt.postgresql.org/pub/repos/apt $(lsb_release -cs)-pgdg main" > /etc/apt/sources.list.d/pgdg.list'
RUN wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -
RUN apt-get update && apt-get -y install postgresql-${PG_MAJOR}-postgis-3  \
                                         postgresql-server-dev-${PG_MAJOR} \
                                         postgresql-client-${PG_MAJOR}     \
                                         libpq-dev

# download and install MinIO server and client
RUN wget https://dl.min.io/server/minio/release/linux-amd64/minio
RUN chmod +x minio
RUN mv minio /usr/local/bin/minio

# download and install MinIO admin
RUN wget https://dl.min.io/client/mc/release/linux-amd64/mc
RUN chmod +x mc
RUN mv mc /usr/local/bin/mc

# set up pgrx with non-sudo user
ARG USERNAME=rust
ARG USER_UID=501
ARG USER_GID=$USER_UID
RUN groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -s /bin/bash -m $USERNAME

RUN mkdir /workspaces && chown -R $USER_UID:$USER_GID /workspaces

# set up permissions so that the user below can create extensions
RUN chmod a+rwx `pg_config --pkglibdir` \
    `pg_config --sharedir`/extension \
    /var/run/postgresql/

# add it to sudoers
RUN echo "$USERNAME ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/$USERNAME 

# now it is time to switch to user
USER $USERNAME

# install Rust environment
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | bash -s -- -y
ENV PATH="/home/rust/.cargo/bin:${PATH}"

# install and configure pgrx
ARG PGRX_VERSION=0.12.6
RUN cargo install --locked cargo-pgrx@${PGRX_VERSION}
RUN cargo pgrx init --pg${PG_MAJOR} $(which pg_config)
RUN echo "shared_preload_libraries = 'pg_parquet'" >> $HOME/.pgrx/data-${PG_MAJOR}/postgresql.conf

ENV MINIO_ROOT_USER=admin
ENV MINIO_ROOT_PASSWORD=admin123
ENV AWS_S3_TEST_BUCKET=testbucket
ENV AWS_REGION=us-east-1
ENV AWS_ACCESS_KEY_ID=admin
ENV AWS_SECRET_ACCESS_KEY=admin123
ENV PG_PARQUET_TEST=true
