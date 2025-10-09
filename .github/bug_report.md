name: 🐛 Bug report
description: Report a problem or unexpected behavior when running MLPerf Client
title: "[BUG]: "
labels: [bug]
assignees: []

body:
  - type: markdown
    attributes:
      value: |
        Thanks for taking the time to fill out a bug report!
        Please provide as much detail as possible to help us reproduce and fix the issue.

  - type: input
    id: summary
    attributes:
      label: Summary
      description: Briefly describe the problem.
      placeholder: The client crashes when loading the Llama 3.1 8B model.
    validations:
      required: true

  - type: dropdown
    id: interface
    attributes:
      label: Interface type
      description: Which version of the application are you using?
      options:
        - Command-line interface (CLI)
        - Graphical user interface (GUI)
        - Other / Not sure
    validations:
      required: true

  - type: input
    id: version
    attributes:
      label: Application version
      description: Which release of MLPerf Client are you using?
      placeholder: v1.5 or commit hash
    validations:
      required: true

  - type: input
    id: os
    attributes:
      label: Operating system
      description: OS name and version.
      placeholder: Ubuntu 24.04 LTS, Windows 11 Pro 23H2, etc.
    validations:
      required: true

  - type: textarea
    id: hardware
    attributes:
      label: Hardware configuration
      description: Describe the system hardware where the issue occurred.
      placeholder: |
        Example:
        - System type: Desktop
        - CPU: AMD Ryzen 7 7800X3D
        - GPU: NVIDIA RTX 4090
        - RAM: 64 GB DDR5
        - Storage: NVMe SSD
    validations:
      required: true

  - type: textarea
    id: steps
    attributes:
      label: Steps to reproduce
      description: How can we reproduce this issue?
      placeholder: |
        1. Run `mlperf-windows.exe -c \Llama3.1\NVIDIA_ORTGenAI-DML_GPU.json`
        2. Observe the error output
    validations:
      required: true

  - type: textarea
    id: expected
    attributes:
      label: Expected behavior
      description: What did you expect to happen?
      placeholder: The benchmark should complete successfully and report a score.

  - type: textarea
    id: logs
    attributes:
      label: Logs or screenshots
      description: Add relevant output, logs, or screenshots.
      render: shell

  - type: input
    id: contact
    attributes:
      label: Contact information (optional)
      description: Provide a way to reach you if we need more details.
      placeholder: your.email@example.com
    validations:
      required: false
