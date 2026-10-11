# -----------------------------------------------------------------------------
# Stage 1: Builder
# This stage contains all build tools and compiles the binary
# -----------------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /build

# Copy source code
COPY . .

# Configure and build
RUN cmake -G Ninja -DCMAKE_BUILD_TYPE=Release . && ninja

# -----------------------------------------------------------------------------
# Stage 2: Runtime
# This stage contains only the minimal runtime and the compiled binary
# -----------------------------------------------------------------------------
FROM ubuntu:24.04

# Install minimal runtime dependencies (if any)
# Currently only libstdc++ is needed, which is in the base image
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# Copy the compiled binary from the builder stage
COPY --from=builder /build/strutdb-server /usr/local/bin/strutdb-server

# Make the binary executable
RUN chmod +x /usr/local/bin/strutdb-server

# Expose the default port (6379)
EXPOSE 6379

# Set the entrypoint to the server binary
ENTRYPOINT ["/usr/local/bin/strutdb-server"]
