FROM node:alpine
MAINTAINER YugaByte
ENV container=yugaware-ui

# Create app directory
WORKDIR /opt/yugaware-ui

RUN npm install -g static-server 

# Bundle app source
COPY build /opt/yugaware-ui

EXPOSE 4000

# Start production version via pushstate server
CMD [ "static-server", "/opt/yugaware-ui", "--port", "4000" ]
