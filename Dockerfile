# Build stage: compile the C++20 binary on a slim bookworm base.
FROM debian:bookworm-slim AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ cmake make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include/ include/
COPY src/ src/
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"

# Runtime stage: only the compiled binary and the dashboard template.
FROM debian:bookworm-slim
COPY templates/ /opt/dev-atlas/templates/
COPY --from=build /src/build/dev_atlas /usr/local/bin/dev_atlas
WORKDIR /workspace
ENTRYPOINT ["dev_atlas"]
CMD ["serve", "--port", "8090"]
