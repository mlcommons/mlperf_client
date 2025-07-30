#pragma once

#include <nlohmann/json.hpp>

namespace cil{
namespace IHV {

class MLXExecutionProviderSettings {
public:
    MLXExecutionProviderSettings() = default;
    inline MLXExecutionProviderSettings(const nlohmann::json& j) {
        FromJson(j);
    }

    ~MLXExecutionProviderSettings() = default;

    void FromJson(const nlohmann::json& j) {
        from_json(j, *this);
    }

    static void from_json(
        const nlohmann::json& j,
        MLXExecutionProviderSettings& obj) {
        if (j.contains("device_type")) {
            j.at("device_type").get_to(obj.device_type_);
        }
    }

    static inline std::string GetDeviceType(const nlohmann::json& j) {
        std::string device_type;
        j.at("device_type").get_to(device_type);
        return device_type;
    }

    const std::string& GetDeviceType() const {
        return device_type_;
    }

private:
    std::string device_type_;
};

}
}