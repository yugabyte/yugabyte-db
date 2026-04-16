# Dockerfile that creates an image that matches Yugabundle target environment for python module
# generation.
# Get the latest docker image
FROM centos/python-38-centos7

USER root

# Perform general yum updates
RUN yum --enablerepo=extras -y install epel-release python3-pip which
RUN pip3 install --upgrade pip

# Dependency section can be configured every time a
# new Python module is added to Yugabyte Platform
# (if it requires third-party shared dependencies)

# Install psycopg2==2.9.1 third-party shared dependencies
RUN yum -y install postgresql-devel python3-devel gcc libcom_err.x86_64

# Install python-ldap==3.3.1 third-party shared dependencies
RUN yum -y install openldap-devel

# Set up install script
RUN ln -sf /usr/bin/python3 /usr/local/bin/python3
RUN mkdir /devops
COPY . /devops
WORKDIR /devops
ENTRYPOINT ["/devops/bin/install_python_requirements.sh", "--create_package", "--use_dynamic_paths"]
