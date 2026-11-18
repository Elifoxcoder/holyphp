FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends gcc libc6-dev libsqlite3-dev liburing-dev util-linux coreutils \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --uid 10001 --no-create-home --shell /usr/sbin/nologin hphp

COPY hphp /usr/local/bin/hphp
RUN chmod 0555 /usr/local/bin/hphp
ENV HOME=/tmp
WORKDIR /workspace
USER 10001:10001
ENTRYPOINT ["/usr/local/bin/hphp"]
