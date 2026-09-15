# MLPerf Client Benchmark: Implementation and Run Rules

For reference:

- <https://github.com/mlcommons/policies/blob/master/submission_rules.adoc>
- <https://github.com/mlcommons/policies/blob/master/MLPerf_Results_Messaging_Guidelines.adoc>
- <https://github.com/mlcommons/inference_policies/blob/master/inference_rules.adoc>
- <https://github.com/mlcommons/training_policies/blob/master/training_rules.adoc>

Items we need to consider:

- Outline of the submission process, key milestones, and what they mean
- Submission requirements in terms of functionality, documentation, accuracy validation, and so forth
- Limitations on releasing modified versions of MLPerf Client around the time of an official release (our other benchmarks have score publication blackouts around publication dates)
- How MLPerf Client rules relate to general MLPerf rules documents

## Topics

1. Benchmark types - Standard, Custom, and [Experimental]
2. Submission Process
   1. Quantization recipe
   2. Accuracy logs and review
   3. Runtime general availability and bundling
3. Accuracy
4. Software and Hardware requirements
5. Benchmark Run Rules
6. Results Publication and Claims

## 1. Overview

MLPerf benchmarks are distinctive because they're built by a coalition of the key players in a given market, all of whom contribute time and resources to create a shared standard set of tests. MLPerf Client is true to this template thanks to the generous participation of companies like AMD, Intel, Microsoft, NVIDIA, and Qualcomm alongside PC OEMs.

This document describes how to implement one or more benchmarks in the MLPerf Client Suite and how to use those implementations to measure the performance of an ML system performing inference.

There are separate rules for the submission, review, and publication process for all MLPerf benchmarks [here](https://github.com/mlperf/policies/blob/master/submission_rules.adoc).

The MLPerf name and logo are trademarks. In order to refer to a result using the MLPerf name, the result must conform to the letter and spirit of the rules specified in this document. The MLPerf organization reserves the right to solely determine if a use of its name or logo is acceptable.

### 1.1. Definitions (read this section carefully)

The following definitions are used throughout this document:

A **sample** is the unit on which inference is run. E.g., an image, or a sentence.

A **query** is a set of N samples that are issued to an inference system together. N is a positive integer. For example, a single query contains 8 images.

**Quality** always refers to a model's ability to produce "correct" outputs.

A **system under test** consists of a defined set of hardware and software resources that will be measured for performance. The hardware resources may include processors, accelerators, memories, disks, and interconnects. The software resources may include an operating system, compilers, libraries, and drivers that significantly influence the running time of a benchmark.

A **reference implementation** is a specific implementation of a benchmark provided by the MLPerf organization. The reference implementation is the canonical implementation of a benchmark. All valid submissions of a benchmark must be equivalent to the reference implementation.

A **run** is a complete execution of a benchmark implementation on a system under the control of the load generator that consists of completing a set of inference queries, including data pre- and post-processing, meeting a latency requirement and a quality requirement in accordance with a scenario.

A **run result** consists of the scenario-specific metric.

## 2. Benchmark workloads

MLPerf Client v2.0 comprises three workload types. Components within each are classified as Base, Extended, or Experimental per Section 3.4.

### 2.1. Large language model tests

The LLM tests measure a system's performance across a range of natural-language tasks that vary the lengths of the input prompts and output responses to simulate different types of language model use.

| Tasks | Models | Datasets | Quality metric |
| --- | --- | --- | --- |
| Creative writing, Content generation, Structured Text, Code analysis, Summarization (intermediate, substantial\*) | Llama 3.1 8B Instruct, Phi 4 Mini Instruct, Phi 4 Reasoning 14B\*, Qwen 3 8B\* | OpenOrca, GovReport, MLPerf Client source code | MMLU / IFEval score |

\* Extended or Experimental component (see Section 3.4).

The LLM tests report two performance metrics:

- **Time to first token (TTFT):** the wait time in seconds before the system produces the first token in response to each prompt. Lower is better. Following widespread industry practice, TTFT is reported separately.
- **Tokens per second (TPS):** the average rate at which the remaining tokens in the response are produced, excluding the first. Higher is better.

The latency for a complete response is TTFT + TPS × (number of tokens in the response).

### 2.2. Image generation tests

The image generation tests evaluate the ability of PCs to run generative visual models locally. The tests use four distinct prompts, each producing four separate images to simulate typical real-world usage, and all generated images are produced at a standard 1024x1024 resolution. Different platforms may employ different backends; the working group ensures comparability by assessing visual quality during development.

| Tasks | Model | Metric |
| --- | --- | --- |
| Text-to-image generation | Flux 2 Klein 4B\* | Images per minute |

\* Experimental component (see Section 3.4).

**Images per minute:** the count of how many images can be created per minute. Higher is better.

### 2.3. Agentic AI tests

The agentic AI tests measure the performance of AI agents in real-world scenarios, including Software Engineering (SWE) and Data Analyst tasks. These tests measure end-to-end performance across multi-step workflows involving both LLM inference and tool calls, and report a breakdown of LLM inference and tool execution times.

| Tasks | LLM Models | Metric |
| --- | --- | --- |
| SWE Agent, Data Analyst Agent\* | Llama 3.1 8B, Qwen 3 8B\* | End-to-end duration (seconds) |

\* Experimental component (see Section 3.4).

**End-to-end duration:** the total time in seconds required to complete agentic tasks, covering all turns of LLM inference and tool executions. Lower is better.

## 3. Submission rules for MLPerf Client v2.0

### 3.1. Submission timeline

For each submission cycle, the working group will pass through the following milestones:

**Code freeze on the main benchmark** – By this point, we will finalize the code on the benchmark application harness and the benchmark definition, including the following components:

- The core benchmark application
- Prompt sets
- Base models
- Accuracy tests and thresholds
- Scoring rubric
- Choice of experimental models
- Choice of experimental acceleration paths

**Submission deadline** – Submitters must provide all of the required assets for their submissions to the working group, as specified below.

**Review period** – Begins just after the submission deadline. During this period, we will schedule meetings for the submitters to review each other's submissions, ask questions, and raise any relevant objections.

**Deadline to raise objections** – Part of the way into the review period, we stop accepting objections to submissions because we have to give submitters time to resolve any issues before the review period ends.

**Deadline to withdraw** – Any submitter may choose to withdraw a submission until the deadline to withdraw at the very end of the review period. Withdrawn submissions will not ship in the next version of MLPerf Client, and their code and config files will not be released on the GitHub repo. Once the deadline to withdraw passes, all remaining accepted submissions are considered final, and MLCommons will create the final RC build of the benchmark application for testing and release staging.

**Benchmark publication date** – The date when the new release of MLPerf Client is released to the public alongside source code. New versions will be posted to the MLPerf Client public GitHub repo and promoted via the MLCommons website and beyond.

The v2.0 submission will follow the timeline shared in the "Client v2.0 release timeline" tab shared in this document: [Final: Public MLCommons Overview Calendar 2026](https://docs.google.com/spreadsheets/d/11k_7m1N4z8D95ynsRE0jwtPuBpqjILhP-bbKSSQ8rSQ/edit?usp=sharing)

### 3.2. Submission assets needed

Unlike the MLPerf data center benchmarks, MLPerf Client submissions are not composed of benchmark scores to be published by MLCommons. Instead, MLPerf Client submitters provide one or more software execution path implementations to be integrated into the MLPerf Client application. These implementations will be reviewed by other submitters and, if approved, will be distributed with the next public release of MLPerf Client.

Submitters will be required to supply a packaged set of assets with their submissions on the submission deadline, as spelled out below.

- Acceleration path implementations and config files specifying how they run
- Any libraries and supporting components
- Model recipes and, optionally, model files
- Accuracy logs and scores for each supported model and acceleration path on a typical IP
- A set of performance results on typical hardware
  - Performance results should include results for all supported models and prompts.
  - If submitting a path or config that supports NPU, CPU, or GPU acceleration, the submitter should provide a set of performance results from a typical processor of that type.
  - If the path supports multiple IP types, the submitter should provide a set of performance results from a typical processor of each type.
  - For hybrid implementations, the submitter should provide a set of performance results from a typical hybrid system.

Please note that the performance results will not be published as official results by MLCommons. They are for the working group to use in understanding the acceleration paths and in setting expectations with the press and reviewers. The submission is the acceleration path implementation and configuration.

### 3.3. General availability of software components

Any runtimes, libraries, and model creation tools (all relevant software components) used in a submission should be generally available to the public by the submission date, at least as a public beta or RC release that is supported via GitHub or another user-facing venue.

### 3.4. Benchmark element types

The key components of the MLPerf Client benchmark are classified under one of three possible labels.

**Base** – These elements are a required part of the base benchmark. They must be included in a standard submission, and support for them is needed in order to produce a "tested by MLCommons" result.

**Extended** – These components are not a part of the base benchmark and are not required to be included in a standard submission. They are still present for those who wish to test with them, and submissions may include accelerated paths for extended models or prompts. A model or prompt might be in the extended category because it's too large to run reliably on some client systems, among other reasons. Though optional, extended elements can contribute to an official "tested by MLCommons" MLPerf Client benchmark score.

**Experimental** – These elements are not a required part of the base benchmark and are considered experimental for one reason or another. They may be relatively new models or software acceleration paths, workload elements that push the size of the context window or the capabilities of the models, or just not quite fully vetted by the industry yet. Experimental elements will not contribute to an official "tested by MLCommons" MLPerf Client benchmark score.

**Custom** - MLPerf Client's source code and config file format are freely available to the public and can be modified at will. MLCommons encourages wide use and experimentation with the MLPerf Client benchmark as a tool. However, the results produced by any config file or build of the benchmark not released by MLCommons will not be considered an official MLPerf Client benchmark score.

### 3.5. Required and optional support

Some elements of the benchmark must be supported by all submissions, while others, dubbed experimental, may be optionally supported.

Model support requirements for v2.0 include:

| Model | Type | Status |
| --- | --- | --- |
| Llama 3.1 8B Instruct | Base | Required |
| Phi 4 Mini Instruct | Base | Required |
| Phi 4 Reasoning 14B | Extended | Optional |
| Qwen 3 8B | Experimental | Optional |
| Flux 2 Klein 4B (image generation) | Experimental | Optional |

Prompt support requirements for v2.0:

| Prompt category | Type | Status | Contributes to overall score? |
| --- | --- | --- | --- |
| Code analysis | Base | Required | Yes |
| Content generation | Base | Required | Yes |
| Creative writing | Base | Required | Yes |
| Structured Text | Base | Required | Yes |
| Summarization, intermediate (~4K) | Base | Required | Yes |
| Summarization, substantial (~8K) | Extended | Optional | No |

Requirements of note:

- Support for all base models is required for a submission.
- Support for all base prompt categories is required for a submission, and is required to produce an overall score.
- Support for 4K prompt lengths is mandatory as of v2.0.
- Support for the substantial (~8K) summarization prompts is optional; results for this category are reported separately and do not contribute to the overall score.
- Support for extended and experimental models is optional.
- Support for any other experimental features is optional.

### 3.6. Accuracy

Because optimization work can trade off accuracy for performance, MLPerf Client requires that each submitted acceleration path must meet a specific accuracy threshold for each model or test scenario.

For new models, the working group sets each threshold. One key consideration is the model's reported accuracy score in its native format, prior to quantization or other modifications. As a rule of thumb, our target may be to set a threshold 7% below the original model's score. However, the working group may choose to define a looser or stricter threshold when presented with data to support such a decision.

For MLPerf Client v2.0, the accuracy thresholds are as follows:

| Model | MMLU | IFEval (loose) | Tier |
| --- | --- | --- | --- |
| Llama 3.1 8B Instruct | 62 | 39 | Base |
| Phi 4 Mini Instruct | 61 | 31 | Base |
| Phi 4 Reasoning 14B | 75 | N/a | Extended |
| Qwen 3 8B | 67 | 25 | Experimental |

The CLI versions of MLPerf Client can run an accuracy test for each model, and it outputs logs that show the score in our chosen accuracy test. Each submitter must submit logs from this test from at least one relevant system and accelerator type for each config submitted. As with performance scores, if multiple IP types are supported by an acceleration path, the submitter should provide a log from a single typical system for each type (GPU, NPU, or CPU.)

Logs and scores should be placed in the shared document for accuracy results

## 4. Results publication and claims

### 4.1. Official configurations and executables

The MLPerf Client benchmark was conceived as more than just a static test. It offers extensive control and configurability over how it runs, and the MLPerf Client working group encourages its use for experimentation and testing beyond the included configurations.

However, scores generated with modified executables, libraries, or configuration files are not to be considered comparable to the fully tested and approved configurations shipped with the benchmark application.

When the benchmark begins to run with one of the standard config files, you should see a notification in the output that notes "Configuration tested by MLCommons." This note means that both the executable program and the JSON config file you are using have been tested and approved by the MLPerf Client working group ahead of the release of the benchmark application. This testing process involves peer review among the organizations participating in the development of the MLPerf Client benchmark. It also includes MMLU-based accuracy testing. Only scores generated with this "configuration tested" notice should be considered valid MLPerf Client scores.

Results from custom configurations or modified versions of the MLPerf Client benchmark application will be labeled as "not tested by MLCommons" and should not be considered valid MLPerf Client scores.
