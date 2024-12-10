# New Executor Integration

The MLPerf Inference client is designed to be extensible. This document describes how to add a new executor to the client.

- Create an executor class that extends `ExecutorBase` (`executor_base.h`).
- Add the executor to the `ExecutorFactory` (`executor_factory.cpp`).
- Generate build configuration with CMake and build the client.

