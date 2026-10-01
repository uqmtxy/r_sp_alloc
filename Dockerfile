FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libgoogle-perftools-dev \
    libjemalloc-dev \
    libmimalloc-dev \
    linux-tools-common \
    linux-tools-generic \
    numactl \
    pkg-config \
    procps \
    python3 \
    python3-pip \
    time \
    valgrind \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /lab

COPY bench ./bench
COPY scripts ./scripts
COPY Makefile README.md ./

RUN make

CMD ["/bin/bash"]
