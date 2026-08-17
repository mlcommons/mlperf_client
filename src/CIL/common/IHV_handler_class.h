#pragma once

#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

#include "../IHV/IHV.h"  // Include IHV API definitions

#define DECLARE_IHV_HANDLER_BASIC_CLASS(IHV_HANDLER_CLASS_NAME,               \
                                        INFERENCE_CLASS_NAME)                 \
  /* The class encapsulates the implementation of the IHV API. */             \
  class IHV_HANDLER_CLASS_NAME {                                              \
   public:                                                                    \
    /* Constructor: Initializes the object with API setup data.*/             \
    explicit IHV_HANDLER_CLASS_NAME(const struct API_IHV_Setup_t& api);       \
                                                                              \
    /* Destructor : Default destructor, handles cleanup that does not involve \
     * explicit resource management. */                                       \
    ~IHV_HANDLER_CLASS_NAME() = default;                                      \
                                                                              \
    /* Enumerates available devices with device id and full name              \
     * Returns true on success, false otherwise. */                           \
    bool EnumerateDevices(struct API_IHV_DeviceEnumeration_t& api);           \
                                                                              \
    /* Initializes the inference engine, loads models, allocates resources,   \
     * etc. Returns true if initialization is successful, false otherwise */  \
    bool Init(const struct API_IHV_Init_t& api);                              \
                                                                              \
    /* Prepares state for new inference. Called every time before Infer call. \
     * Returns true on success, false otherwise. */                           \
    bool Prepare(const struct API_IHV_Simple_t& api);                         \
                                                                              \
    /* Executes inference on the provided data. Modifies the API_IHV_Infer_t  \
     * structure with the output results. Returns true if inference is        \
     * successful, false otherwise. */                                        \
    bool Infer(struct API_IHV_Infer_t& api);                                  \
                                                                              \
    /*Resets state for new inference. Called every time after Infer call.     \
     * Returns true on success, false otherwise. */                           \
    bool Reset(const struct API_IHV_Simple_t& api);                           \
                                                                              \
    /* Cleans up resources allocated during Init. Returns true if resources   \
     * are successfully released, false otherwise. */                         \
    bool Deinit(const struct API_IHV_Deinit_t& api);                          \
                                                                              \
    /* Getter for the device type used in this instance of inference. Returns \
     * a constant reference to a string containing the device type. */        \
    const std::string& GetDeviceType() const { return device_type_; }         \
                                                                              \
   private:                                                                   \
    std::shared_ptr<INFERENCE_CLASS_NAME>                                     \
        inference_; /*Shared pointer to the inference implementation  */      \
                                                                              \
    std::string                                                               \
        device_type_; /* Type of device used for inference, e.g., CPU, GPU */ \
  };

#define DEFINE_API_IHV_SETUP_BASIC_IMPL(IHV_HANDLER_CLASS_NAME, IHV_NAME, \
                                        IHV_VERSION)                      \
  const API_IHV_Struct_t* API_IHV_Setup(const API_IHV_Setup_t* api) {     \
    std::string ep_name = api->ep_name;                                   \
                                                                          \
    if (!ep_name.compare(IHV_NAME) &&                                     \
        !ep_name.compare(std::string("IHV ") + IHV_NAME)) {               \
      auto error = "EP " + ep_name + " is not supported by this IHV";     \
      api->logger(api->context, API_IHV_LogLevel::API_IHV_FATAL,          \
                  error.c_str());                                         \
      return nullptr;                                                     \
    }                                                                     \
                                                                          \
    auto ihv_struct = new API_IHV_Struct_t();                             \
    ihv_struct->ep_name = IHV_NAME;                                       \
    ihv_struct->version = IHV_VERSION;                                    \
                                                                          \
    auto ihv_handler = new IHV_HANDLER_CLASS_NAME(*api);                  \
                                                                          \
    ihv_struct->ihv_data = ihv_handler;                                   \
    ihv_struct->device_type = ihv_handler->GetDeviceType().c_str();       \
    ihv_struct->api_version = API_IHV_VERSION;                            \
    return ihv_struct;                                                    \
  }

#define DEFINE_API_IHV_ENUMERATE_DEVICES_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)  \
  int API_IHV_EnumerateDevices(struct API_IHV_DeviceEnumeration_t* api) {    \
    auto ihv_handler =                                                       \
        static_cast<IHV_HANDLER_CLASS_NAME*>(api->ihv_struct->ihv_data);     \
    return ihv_handler->EnumerateDevices(*api) ? API_IHV_RETURN_SUCCESS : 1; \
  }

#define DEFINE_API_IHV_INIT_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)           \
  int API_IHV_Init(const API_IHV_Init_t* api) {                          \
    auto ihv_handler =                                                   \
        static_cast<IHV_HANDLER_CLASS_NAME*>(api->ihv_struct->ihv_data); \
    return ihv_handler->Init(*api) ? API_IHV_RETURN_SUCCESS : 1;         \
  }

#define DEFINE_API_IHV_PREPARE_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)        \
  int API_IHV_Prepare(const API_IHV_Simple_t* api) {                     \
    auto ihv_handler =                                                   \
        static_cast<IHV_HANDLER_CLASS_NAME*>(api->ihv_struct->ihv_data); \
    return ihv_handler->Prepare(*api) ? API_IHV_RETURN_SUCCESS : 1;      \
  }

#define DEFINE_API_IHV_INFER_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)          \
  int API_IHV_Infer(struct API_IHV_Infer_t* api) {                       \
    auto ihv_handler =                                                   \
        static_cast<IHV_HANDLER_CLASS_NAME*>(api->ihv_struct->ihv_data); \
    return ihv_handler->Infer(*api) ? API_IHV_RETURN_SUCCESS : 1;        \
  }

#define DEFINE_API_IHV_RESET_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)          \
  int API_IHV_Reset(const API_IHV_Simple_t* api) {                       \
    auto ihv_handler =                                                   \
        static_cast<IHV_HANDLER_CLASS_NAME*>(api->ihv_struct->ihv_data); \
    return ihv_handler->Reset(*api) ? API_IHV_RETURN_SUCCESS : 1;        \
  }

#define DEFINE_API_IHV_DEINIT_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)         \
  int API_IHV_Deinit(const API_IHV_Deinit_t* api) {                      \
    auto ihv_handler =                                                   \
        static_cast<IHV_HANDLER_CLASS_NAME*>(api->ihv_struct->ihv_data); \
    return ihv_handler->Deinit(*api) ? API_IHV_RETURN_SUCCESS : 1;       \
  }

#define DEFINE_API_IHV_RELEASE_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)        \
  void API_IHV_Release(const API_IHV_Release_t* api) {                   \
    if (api->ihv_struct == nullptr) return;                              \
                                                                         \
    auto ihv_handler =                                                   \
        static_cast<IHV_HANDLER_CLASS_NAME*>(api->ihv_struct->ihv_data); \
                                                                         \
    delete ihv_handler;                                                  \
    delete api->ihv_struct;                                              \
  }

#define DEFINE_API_IHV_GET_API_VERSION_BASIC_IMPL() \
  unsigned API_IHV_GetAPIVersion(void) { return API_IHV_VERSION; }

#define DEFINE_API_IHV_BASIC_IMPL(IHV_HANDLER_CLASS_NAME, IHV_NAME,   \
                                  IHV_VERSION)                        \
  DEFINE_API_IHV_GET_API_VERSION_BASIC_IMPL()                         \
  DEFINE_API_IHV_SETUP_BASIC_IMPL(IHV_HANDLER_CLASS_NAME, IHV_NAME,   \
                                  IHV_VERSION)                        \
  DEFINE_API_IHV_ENUMERATE_DEVICES_BASIC_IMPL(IHV_HANDLER_CLASS_NAME) \
  DEFINE_API_IHV_INIT_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)              \
  DEFINE_API_IHV_PREPARE_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)           \
  DEFINE_API_IHV_INFER_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)             \
  DEFINE_API_IHV_RESET_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)             \
  DEFINE_API_IHV_DEINIT_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)            \
  DEFINE_API_IHV_RELEASE_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)

// Returns a process-lifetime, valid-but-empty device list (count == 0).
// EnumerateDevices hands this back when the IHV's inference object failed to
// construct: the constructor has already logged the underlying error, so the
// host should treat the EP as having no devices rather than a FATAL failure.
// Returns nullptr only if the one-time allocation fails.
inline const API_IHV_DeviceList_t* IHVEmptyDeviceList() {
  static API_IHV_DeviceList_t* const kEmptyDeviceList = []() {
    auto* dl = static_cast<API_IHV_DeviceList_t*>(
        std::malloc(sizeof(API_IHV_DeviceList_t)));
    if (dl != nullptr) {
      dl->count = 0;
    }
    return dl;
  }();
  return kEmptyDeviceList;
}

// Defines CLASS_NAME::EnumerateDevices. A null inference_ means the IHV's
// constructor already failed and logged the reason; this then reports an empty
// device list and succeeds, so the host cleanly sees "no devices for this EP"
// instead of a spurious FATAL diagnostic.
#define DEFINE_IHV_CLASS_ENUMERATE_DEVICES_IMPL(CLASS_NAME)              \
  bool CLASS_NAME::EnumerateDevices(API_IHV_DeviceEnumeration_t& api) {  \
    if (inference_ == nullptr) {                                         \
      api.device_list = IHVEmptyDeviceList();                            \
      if (api.device_list == nullptr) {                                  \
        api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL,         \
                   "EnumerateDevices: failed to allocate device list."); \
        return false;                                                    \
      }                                                                  \
      api.logger(api.context, API_IHV_LogLevel::API_IHV_INFO,            \
                 "EnumerateDevices: inference object unavailable; "      \
                 "reporting an empty device list.");                     \
      return true;                                                       \
    }                                                                    \
                                                                         \
    api.device_list = inference_->EnumerateDevices();                    \
                                                                         \
    if (nullptr == api.device_list) {                                    \
      api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL,           \
                 "EnumerateDevices: No devices found.");                 \
      return false;                                                      \
    }                                                                    \
                                                                         \
    if (auto error_message = inference_->GetErrorMessage();              \
        !error_message.empty()) {                                        \
      api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,           \
                 error_message.c_str());                                 \
      return false;                                                      \
    }                                                                    \
                                                                         \
    return true;                                                         \
  }

#define IHV_VALIDATE_SCENARIO_WITH_ALIASES(API, SCENARIO_NAME,            \
                                           IS_CANONICAL_OUT, ...)         \
  do {                                                                    \
    if ((SCENARIO_NAME).empty()) {                                        \
      (API).logger((API).context, API_IHV_LogLevel::API_IHV_FATAL,        \
                   "Scenario name is empty");                             \
      return;                                                             \
    }                                                                     \
    (IS_CANONICAL_OUT) =                                                  \
        (SCENARIO_NAME) == "txt2txt" || (SCENARIO_NAME) == "txt2img";     \
    bool _ihv_is_alias = false;                                           \
    for (std::string_view _ihv_alias :                                    \
         std::initializer_list<std::string_view>{__VA_ARGS__}) {          \
      if (_ihv_alias == (SCENARIO_NAME)) {                                \
        _ihv_is_alias = true;                                             \
        break;                                                            \
      }                                                                   \
    }                                                                     \
    if (!(IS_CANONICAL_OUT) && !_ihv_is_alias) {                          \
      (API).logger(                                                       \
          (API).context, API_IHV_LogLevel::API_IHV_FATAL,                 \
          ("Scenario " + (SCENARIO_NAME) + " is not supported").c_str()); \
      return;                                                             \
    }                                                                     \
  } while (0)

#define IHV_VALIDATE_SCENARIO(API, SCENARIO_NAME, IS_CANONICAL_OUT)          \
  IHV_VALIDATE_SCENARIO_WITH_ALIASES(API, SCENARIO_NAME, IS_CANONICAL_OUT,   \
                                     "llama3", "phi4mini", "phi4reason",     \
                                     "qwen3", "phi4", "flux2klein")

#define IHV_LOG_MODEL_NOT_SUPPORTED(API, MODEL_NAME, SCENARIO_NAME,    \
                                    IS_CANONICAL_SCENARIO)             \
  (API).logger(                                                        \
      (API).context, API_IHV_LogLevel::API_IHV_FATAL,                  \
      ("Model " + (MODEL_NAME) +                                       \
       ((IS_CANONICAL_SCENARIO) ? " using scenario " + (SCENARIO_NAME) \
                                : std::string{}) +                     \
       " is not supported")                                            \
          .c_str())

// Validates that MODEL_BASE_NAME is one of the model_base_names this IHV is
// known to support (passed as a variadic list of string literals). If
// MODEL_BASE_NAME is empty (e.g. during EnumerateDevices) the check is skipped.
// On miss, logs a "Model not supported" message and returns from the enclosing
// function. Intended for IHVs whose internal code dispatches on model identity
// (e.g. MLX, NativeQNN). Model-agnostic IHVs should not use this.
#define IHV_VALIDATE_MODEL_BASE_NAME(API, MODEL_BASE_NAME, SCENARIO_NAME,      \
                                     IS_CANONICAL_SCENARIO, ...)               \
  do {                                                                         \
    if (!(MODEL_BASE_NAME).empty()) {                                          \
      bool _ihv_model_supported = false;                                       \
      for (std::string_view _ihv_supported :                                   \
           std::initializer_list<std::string_view>{__VA_ARGS__}) {             \
        if (_ihv_supported == (MODEL_BASE_NAME)) {                             \
          _ihv_model_supported = true;                                         \
          break;                                                               \
        }                                                                      \
      }                                                                        \
      if (!_ihv_model_supported) {                                             \
        IHV_LOG_MODEL_NOT_SUPPORTED((API), (MODEL_BASE_NAME), (SCENARIO_NAME), \
                                    (IS_CANONICAL_SCENARIO));                  \
        return;                                                                \
      }                                                                        \
    }                                                                          \
  } while (0)
