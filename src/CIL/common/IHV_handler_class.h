#pragma once

#include <memory>
#include <string>

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

#define DEFINE_API_IHV_BASIC_IMPL(IHV_HANDLER_CLASS_NAME, IHV_NAME,   \
                                  IHV_VERSION)                        \
  DEFINE_API_IHV_SETUP_BASIC_IMPL(IHV_HANDLER_CLASS_NAME, IHV_NAME,   \
                                  IHV_VERSION)                        \
  DEFINE_API_IHV_ENUMERATE_DEVICES_BASIC_IMPL(IHV_HANDLER_CLASS_NAME) \
  DEFINE_API_IHV_INIT_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)              \
  DEFINE_API_IHV_PREPARE_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)           \
  DEFINE_API_IHV_INFER_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)             \
  DEFINE_API_IHV_RESET_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)             \
  DEFINE_API_IHV_DEINIT_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)            \
  DEFINE_API_IHV_RELEASE_BASIC_IMPL(IHV_HANDLER_CLASS_NAME)

#define DEFINE_IHV_CLASS_ENUMERATE_DEVICES_IMPL(CLASS_NAME)             \
  bool CLASS_NAME::EnumerateDevices(API_IHV_DeviceEnumeration_t& api) { \
    if (inference_ == nullptr) {                                        \
      api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL,          \
                 "EnumerateDevices: Inference object is nullptr!");     \
      return false;                                                     \
    }                                                                   \
                                                                        \
    api.device_list = inference_->EnumerateDevices();                   \
                                                                        \
    if (nullptr == api.device_list) {                                   \
      api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL,          \
                 "EnumerateDevices: No devices found.");                \
      return false;                                                     \
    }                                                                   \
                                                                        \
    if (auto error_message = inference_->GetErrorMessage();             \
        !error_message.empty()) {                                       \
      api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,          \
                 error_message.c_str());                                \
      return false;                                                     \
    }                                                                   \
                                                                        \
    return true;                                                        \
  }