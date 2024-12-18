# install dependencies and build
FROM ubuntu:24.10 AS build

RUN apt-get -y update && apt-get -y install sudo 
ARG BUILD_DEPS='libboost-all-dev git libicu-dev libssl-dev cmake gcc g++ ninja-build'
RUN sudo apt-get -y install $BUILD_DEPS

WORKDIR /usr/src
RUN git clone https://github.com/SteveTownsend/prometheus-cpp.git
WORKDIR /usr/src/prometheus-cpp

# fetch third-party dependencies
RUN git submodule init
RUN git submodule update
RUN mkdir _build
WORKDIR /usr/src/prometheus-cpp/_build

# run cmake, build, install the libraries and headers
RUN cmake .. -DBUILD_SHARED_LIBS=ON -DENABLE_PUSH=OFF -DENABLE_COMPRESSION=OFF && \
  cmake --build . --parallel 4 && \
  cmake --install .

WORKDIR /usr/src/firehose-client
COPY . .
RUN cmake --preset=Release-GCC && cmake --build --preset=rel-gcc && \
  rm -rf /var/lib/apt/lists/* && \
  apt-get purge -y --auto-remove $buildDeps

# prepare the runtime environment
FROM ubuntu:24.10

RUN apt-get -y update && apt-get -y install sudo wget gpg
ARG RUNTIME_DEPS='libicu-dev libssl-dev cmake'
RUN sudo apt-get -y install $RUNTIME_DEPS 

COPY --from=build /usr/src/prometheus-cpp /usr/src/prometheus-cpp
WORKDIR /usr/src/prometheus-cpp/_build
RUN cmake --install .

WORKDIR /usr/src/firehose-client
COPY --from=build /usr/src/firehose-client/release-gcc/firehose_client /usr/src/firehose-client
COPY --from=build /usr/src/firehose-client/config /usr/src/firehose-client/config

RUN wget -q -O - https://apt.grafana.com/gpg.key | gpg --dearmor > /etc/apt/keyrings/grafana.gpg && \
  echo "deb [signed-by=/etc/apt/keyrings/grafana.gpg] https://apt.grafana.com stable main" | tee /etc/apt/sources.list.d/grafana.list && \
  sudo apt-get update && apt-get install -y promtail && \
  mkdir --parents /etc/promtail/positions &&
cp /usr/src/firehose/config/config.yml /etc/promtail/config.yml

RUN rm -rf /usr/src/prometheus-cpp && \
  rm -rf /var/lib/apt/lists/* && \
  sudo apt-get purge -y --auto-remove cmake

RUN mkdir -p ./logs
ENTRYPOINT ["/usr/src/firehose-client/firehose_client", "/usr/src/firehose-client/config/live_config.yml"]
EXPOSE 59090
ENV EXPOSER_PORT=59090
# CMD ["./live_config.yml"]

LABEL org.opencontainers.image.source=https://github.com/SteveTownsend/nafo-forum-moderation
LABEL org.opencontainers.image.description="NAFO Moderation Firehose Client"
LABEL org.opencontainers.image.licenses=MIT
