/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#undef max

namespace cil {

namespace IHV {

class NativeQnnExecutionProviderSettings {
 public:
  NativeQnnExecutionProviderSettings() = default;
  inline NativeQnnExecutionProviderSettings(const nlohmann::json& j) {
    FromJson(j);
  }
  ~NativeQnnExecutionProviderSettings() = default;

  const std::string& GetDeviceType() const { return device_type_; }
  const std::optional<int>& GetNumOfThreads() const { return num_of_threads_; }
  const std::optional<int>& GetNumStreams() const { return num_streams_; }

  void FromJson(const nlohmann::json& j) { from_json(j, *this); }

  static void from_json(const nlohmann::json& j,
                        NativeQnnExecutionProviderSettings& obj) {

    if (j.contains("device_type"))
        j.at("device_type").get_to(obj.device_type_);

    if (j.contains("num_of_threads"))
      obj.num_of_threads_ = j.value("num_of_threads", 0);

    if (j.contains("num_streams")) obj.num_streams_ = j.value("num_streams", 0);
  }

  static inline std::string GetDeviceType(const nlohmann::json& j) {
    std::string device_type;
    j.at("device_type").get_to(device_type);
    return device_type;
  }

 private:
  std::string device_type_;
  std::optional<int> num_of_threads_;
  std::optional<int> num_streams_;
};
}  // namespace IHV

}  // namespace cil