FROM eclipse-temurin:17-jdk as build

# Copy the source code
COPY . ts-backend

# Compile and create JAR
RUN cd ts-backend && ./gradlew installDist -x check --no-daemon

RUN ls -la /ts-backend/build/install/ts/

FROM eclipse-temurin:17-jdk
#RUN apt-get update && apt-get dist-upgrade -y && java -version

WORKDIR /opt/yugabyte/ts-backend

COPY --from=build /ts-backend/build/install/ts/ /opt/yugabyte/ts-backend

EXPOSE 8080
ENTRYPOINT ["bin/ts"]
