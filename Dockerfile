FROM swe-arena-base AS builder
ARG TARGETARCH

RUN apt-get update && apt-get install -y --no-install-recommends curl ca-certificates && \
    if [ "$TARGETARCH" = "arm64" ]; then \
      curl -L https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-arm64 -o /usr/local/bin/bazel; \
    else \
      curl -L https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64 -o /usr/local/bin/bazel; \
    fi && \
    chmod +x /usr/local/bin/bazel

FROM swe-arena-base
ARG TARGETARCH

# Pull Python
COPY --from=python:3.13-slim-bookworm /usr/local/ /usr/local/
# Pull Bazel
COPY --from=builder /usr/local/bin/bazel /usr/local/bin/bazel
RUN ldconfig

# Install Base build tools, profiling tools (strace, linux-perf, valgrind), and Compilers (LLVM 19)
RUN apt-get update && apt-get install -y --no-install-recommends \
    wget curl gnupg ca-certificates git bash-completion \
    libexpat1 libsqlite3-0 libssl3 zlib1g binutils-gold build-essential \
    strace linux-perf valgrind \
    && mkdir -p /usr/share/keyrings \
    && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm-archive-keyring.gpg \
    && echo "deb [signed-by=/usr/share/keyrings/llvm-archive-keyring.gpg] http://apt.llvm.org/bookworm/ llvm-toolchain-bookworm-19 main" > /etc/apt/sources.list.d/llvm.list \
    && apt-get update && apt-get install -y --no-install-recommends \
    clang-19 clangd-19 lld-19 \
    && ln -sf /usr/bin/clang-19 /usr/bin/clang \
    && ln -sf /usr/bin/clang++-19 /usr/bin/clang++ \
    && ln -sf /usr/bin/clangd-19 /usr/bin/clangd \
    && ln -sf /usr/bin/lld-19 /usr/bin/lld \
    && ln -sf /usr/bin/lld-19 /usr/bin/ld.lld \
    && apt-get purge -y gnupg \
    && apt-get autoremove -y \
    && rm -rf /var/lib/apt/lists/*

# Setup Python Virtual Environment
ENV PATH="/opt/venv/bin:$PATH"
RUN python3 -m venv /opt/venv && \
    /opt/venv/bin/pip install --no-cache-dir --upgrade pip

# Cache directory for Bazel and Workspace definitions
ENV BAZEL_REPOSITORY_CACHE=/tmp/bazel-repo-cache \
    BAZEL_OUTPUT_BASE=/tmp/bazel-global-cache \
    SHELL=/bin/bash

WORKDIR /testbed/atlas

# Copy python dependencies layout
COPY requirements.txt ./
RUN if [ -f requirements.txt ]; then /opt/venv/bin/pip install --no-cache-dir -r requirements.txt; fi

# Final Toolchain Output Verification
RUN python3 --version && bazel --version && clang++ --version && strace -V
