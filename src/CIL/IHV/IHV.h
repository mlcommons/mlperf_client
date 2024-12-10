#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file IHV.h
 *
 * @brief IHV API for benchmarking tool
 *
 * @details file defines the API for IHV libraries to be used with the
 * benchmarking tool. The benchmarking tool will load the IHV library at
 * runtime, and call the functions defined in this file to interact with the IHV
 * library. The IHV library should implement these functions to run inference on
 * the model, and return the output. The IHV library should also log messages
 * using the logger function provided in the API. The IHV library should return
 * API_IHV_RETURN_SUCCESS on success, and non-zero on failure. The IHV library
 * should not exit the process on failure, but should return an error code to
 * the benchmarking tool.
 *
 * The API is C-compatible. It defines 5 functions that need to be implemented
 * by the IHV, mostly doing:
 * - Check API version, ensure compatibility,
 * - Parsing configuration provided in Config.json for this specific EP,
 * - Return device type going to be used, and EP name,
 * - Check if the model format can be loaded by the IHV, and if the EP is
 * supported by the IHV library.
 * - Define EPs handled by the IHV library.
 * - Setting up possible other dependencies needed to create an inference
 * session, like loading the model, setting up input/output tensors, etc.
 * - Creates IHV data, to be used on each call to the IHV code, stored by the
 * main app when the library is in memory.
 * - Creating inference session i.e. load the model and setting up input/output
 * tensors,
 * - Running single inference (iterations are handled by the main executable),
 * - Possible cleanup after benchmark,
 * - Releasing any resources, freeing memory, to safely unload the library.
 *
 * Each call to the IHV library is provided with a basic C struct, which gives
 * a pointer to the logger function used for the model (e.g. Llama2). The IHV
 * does not need to implement any custom logging functionality, and should not
 * use console output directly.
 *
 */

/**
 * @def API_IHV_VERSION
 *
 * @brief API version defined using Year, Month, Day format.
 */
#define API_IHV_VERSION 240426

/**
 * @enum API_IHV_LogLevel
 *
 * @brief Enumerates log levels for API's logging function, describing the
 * severity of the log message.
 *
 * @details Logs will be stored in a file inside Logs folder, depending on model
 * name.
 */
enum API_IHV_LogLevel {
  /**
   * @brief Logs informational messages.
   */
  API_IHV_INFO,
  /**
   * @brief Logs warning messages, indicating potential issues, but won't stop
   * the benchmark.
   */
  API_IHV_WARNING,
  /**
   * @brief Logs error messages, stops the benchmark.
   */
  API_IHV_ERROR,
  /**
   * @brief Logs fatal messages, indicating critical issues that stop the
   * benchmark.
   */
  API_IHV_FATAL,
};

/**
 * @typedef API_IHV_Logger_func
 *
 * @brief Function pointer type for logging purposes.
 *
 * @details This function is used throughout the API to report logs. The
 * function takes a context pointer, log level, and message string as input. The
 * context pointer is used internally, and must be the same as the context
 * pointer passed to the API structs by the caller.
 */
typedef void (*const API_IHV_Logger_func)(void *context, API_IHV_LogLevel level,
                                          const char *message);

/**
 * @typedef API_IHV_Token_Callback_func
 *
 * @bried Token callback function
 */
typedef void (*const API_IHV_Token_Callback_func)(void *obj, uint32_t token);

/**
 * @struct API_IHV_Struct_t
 *
 * @brief Structure defining the IHV configuration, references IHV-specific
 * information.
 *
 * @details This is used in all API functions to provide pointer to the
 * IHV's data. IHV's can use this struct to store custom data during setup. The
 * IHV should return a pointer to this struct in the Setup() function.
 */
struct API_IHV_Struct_t {
  /**
   * @brief The Name of the execution provider.
   */
  const char *ep_name;

  /**
   * @brief The Type of device (e.g., CPU, GPU, NPU), that will be used for
   * inference.
   */
  const char *device_type;

  /**
   * @brief Version of the IHV library.
   */
  const char *version;

  /**
   * @brief Pointer to custom data for IHV's use, allowing flexibility for
   * additional context, e.g. pointer to a singleton object.
   */
  void *ihv_data;
};

/**
 * @struct API_IHV_Setup_t
 *
 * @brief Structure for API setup information to initialize the IHV library and
 * configure initial settings.
 */
struct API_IHV_Setup_t {
  /**
   * @brief API version to ensure compatibility. Read-only.
   */
  const unsigned api_version;

  /**
   * @brief Context for the logger function. Read-only.
   */
  void *context;

  /**
   * @brief Function pointer to the logger function. Read-only.
   */
  API_IHV_Logger_func logger;

  /**
   * @brief Version of the executable that is using this API. Read-only.
   */
  const char *executable_version;

  /**
   * @brief Name of the execution provider expected to be run by the IHV
   * library. Read-only.
   */
  const char *ep_name;

  /**
   * @brief The Name of the model to be used. Read-only.
   */
  const char *model_name;

  /**
   * @brief Filesystem path to the model. Read-only.
   */
  const char *model_path;

  /**
   * @brief Filesystem path to the additional dependencies directory. Read-only.
   */
  const char *deps_dir;

  /**
   * @brief Execution provider settings in JSON string format. Read-only.
   */
  const char *ep_settings;
};

/**
 * @struct API_IHV_Init_t
 *
 * @brief Structure for initializing the IHV library and setting up the
 * session.
 *
 * @details This structure passed to the Init() function to initialize the
 * session or benchmark. Prepares for model loading and resource allocation.
 * This can be used to load the model, allocate necessary memory for
 * inference, setup input/output layers, etc.
 */
struct API_IHV_Init_t {
  /**
   * @brief Context for the logger function. Read-only.
   */
  void *context;

  /**
   * @brief Function pointer to the logger function. Read-only.
   */
  API_IHV_Logger_func logger;

  /**
   * @brief Pointer to IHV configuration set up earlier.
   */
  const struct API_IHV_Struct_t *const ihv_struct;

  /**
   * @brief Model configuration parameters for the llama2.
   */
  const char *model_config;
};

/**
 * @enum API_IHV_Callback_Type
 *
 * @brief Type of callback function provided to IHV library.
 */
enum API_IHV_Callback_Type {
  API_IHV_CB_None,
  API_IHV_CB_Token,  // API_IHV_Token_Callback_func
};

/**
 * @struct API_IHV_Callback_t
 *
 * @bried Callback information structure.
 */
struct API_IHV_Callback_t {
  /**
   * @brief Callback function specification provided as function field.
   */
  API_IHV_Callback_Type type;

  /**
   * @brief Object instance that provided callback function.
   */
  void *object;

  /**
   * @brief Callback function pointer.
   */
  void *function;
};

/**
 * @struct API_IHV_IO_Data_t
 *
 * @brief Structure for inferencing IO data.
 *
 * @details This struct is used to pass input data to the IHV library and get
 * the output data.
 */
struct API_IHV_IO_Data_t {
  /**
   * @brief Input data, read-only.
   */
  const void *const input;

  /**
   * @brief Size of input data.
   *         - Llama2 - number of input (uint32_t) tokens
   */
  const unsigned input_size;

  /**
   * @brief Output data allocated by the IHV library, could be a pointer to a
   * buffer which is kept alive by the IHV library until Deinit() is called.
   */
  const void *output;

  /**
   * @brief Size of output data
   *         - Llama2 - number of tokens with timestamps
   */
  unsigned output_size;

  /**
   * @brief Callback info.
   */
  struct API_IHV_Callback_t callback;
};

/**
 * @struct API_IHV_Infer_t
 * @brief Structure for inferencing function.
 *
 * @details The struct is used to transfer the IO data alongside with
 * resources necessary for interacting with the IHV.
 */
struct API_IHV_Infer_t {
  /**
   * @brief Context for the logger function. Read-only.
   */
  void *const context;

  /**
   * @brief Function pointer to the logger function. Read-only.
   */
  API_IHV_Logger_func logger;

  /**
   * @brief Pointer to hardware information struct created during setup.
   */
  const struct API_IHV_Struct_t *const ihv_struct;

  /**
   * @brief Pointer to the IO data
   */
  struct API_IHV_IO_Data_t *const io_data;
};

/**
 * @struct API_IHV_Deinit_t
 *
 * @brief Structure for deinitializing the session. This is called to clean up
 * resources post-inference.
 *
 * @details After inferencing is done on all iterations, the benchmarking tool
 * will call the Deinit() function with this struct to cleanup inference
 * resources. At this point, the IHV library should release all resources
 * allocated during Init(). The IHV library should not release the resources
 * allocated during Setup(). This is a good place to release output buffers
 * returned from Infer().
 *
 */
struct API_IHV_Deinit_t {
  /**
   * @brief Context for the logger function. Read-only.
   */
  void *context;
  /**
   * @brief  Function pointer to the logger function.
   */
  API_IHV_Logger_func logger;
  /**
   * @brief Pointer to IHV configuration.
   */
  const struct API_IHV_Struct_t *const ihv_struct;
};

/**
 * @struct API_IHV_Release_t
 *
 * @brief Structure used by Release() function, for releasing all resources.
 *
 * @details After the benchmarking is done, the benchmarking tool will call
 * the release function with this struct, to release all resources allocated
 * during setup, and unload the IHV library.
 */
struct API_IHV_Release_t {
  /**
   * @brief Context for the logger function.
   */
  void *context;
  /**
   * @brief Function pointer to the logger function.
   */
  API_IHV_Logger_func logger;

  /**
   * @brief Pointer to IHV configuration to be deleted by the IHV library.
   */
  const struct API_IHV_Struct_t *const ihv_struct;
};

struct API_IHV_Simple_t {
  /**
   * @brief Context for the logger function. Read-only.
   */
  void *context;
  /**
   * @brief  Function pointer to the logger function.
   */
  API_IHV_Logger_func logger;
  /**
   * @brief Pointer to IHV configuration.
   */
  const struct API_IHV_Struct_t *const ihv_struct;
};

/**
 * @def API_IHV_RETURN_SUCCESS
 *
 * @brief Macro for successful return status.
 */
#define API_IHV_RETURN_SUCCESS 0

/**
 * @name IHV API Functions
 * @{
 *
 * @brief Function prototypes that must be implemented by the IHV vendor.
 *
 * @details The benchmarking tool will call these functions to interact with the
 * IHV library. The IHV library should implement these functions, and export
 * them in the shared library. The benchmarking tool will load the shared
 * library at runtime, and call these functions to interact with the IHV
 * library.
 *
 * The benchmarking tool will call the functions in the following order:
 * 1. API_IHV_Setup()
 * 2. API_IHV_Init()
 * 3. loop:
 *   3.1 API_IHV_Prepare()
 *   3.2 API_IHV_Infer()
 *   3.3 API_IHV_Reset()
 * 4. API_IHV_Deinit()
 * 5. API_IHV_Release()
 */
/**
 * @typedef API_IHV_Setup_func
 *
 * @brief Function pointer type for the Setup function.
 *
 * @details  Setup function prototype; must return a pointer to
 * API_IHV_Struct_t, initialized with IHV specific information. Expected
 * exported function name is API_IHV_Setup.
 *
 * The benchmarking tool will call this function to initialize the IHV
 * library, after a successful load of the shared library during runtime. At
 * this point, the IHV should save the model path, execution provider name and
 * settings. The IHV should not load the model or allocate any resources at
 * this point, however it should check if the execution provider name is
 * supported, and settings are valid. The IHV should provide the device type
 * e.g. CPU, and execution provider name for confirmation (even if is supports
 * multiple EPs). The IHV should return a pointer to the API_IHV_Struct_t
 * struct, which will be passed to all other API functions. It's IHV
 * responsibility to delete the struct in the Release() function. The IHV can
 * store custom data in the ihv_data field of the struct, if needed.
 */
typedef const struct API_IHV_Struct_t *(*API_IHV_Setup_func)(
    const struct API_IHV_Setup_t *api);

/**
 * @typedef API_IHV_Init_func
 *
 * @brief Function pointer type for the Init function.
 *
 * @details Initialization function prototype; must return
 * API_IHV_RETURN_SUCCESS on success. Expected exported function name is
 * API_IHV_Init.
 *
 * The IHV should load the model, allocate necessary memory for inference,
 * setup input/output layers, etc. The IHV should return
 * API_IHV_RETURN_SUCCESS on success, and non-zero on failure.
 */
typedef int (*API_IHV_Init_func)(const struct API_IHV_Init_t *api);

/**
 * @typedef API_IHV_Infer_func
 *
 * @brief Function pointer type for the Inference function.
 *
 * @details This function processes the input data and produces output.
 * Expected exported function name is API_IHV_Infer.
 *
 * The IHV should use the input data provided in the API_IHV_Infer_t struct to
 * run inference on the model. The IHV should set the output pointer and it's
 * size. The IHV must keep the output buffer alive until the Deinit function
 * is called. The IHV should return API_IHV_RETURN_SUCCESS on success, and
 * non-zero on failure. Note that the output buffer could be the same for all
 * iterations, the benchmark tool would copy it's content if needed.
 */
typedef int (*API_IHV_Infer_func)(struct API_IHV_Infer_t *api);

/**
 * @typedef API_IHV_Deinit_func
 *
 * @brief Function pointer type for the Deinit function.
 *
 * @details Deinitialization function cleans up resources allocated during the
 * Init() function. Expected exported function name is API_IHV_Deinit.
 *
 * The IHV should release all resources allocated during the Init function.
 * The IHV should not release the resources allocated during the Setup
 * function yet. The IHV should return API_IHV_RETURN_SUCCESS on success, and
 * non-zero on failure. This is a good place to release output buffers
 * returned from Infer(), and unload the model file.
 */
typedef int (*API_IHV_Deinit_func)(const struct API_IHV_Deinit_t *api);

/**
 * @typedef API_IHV_Prepare_func & API_IHV_Reset_func
 *
 * @brief Prepare and reset inference session between runs.
 */
typedef int (*API_IHV_Prepare_func)(const struct API_IHV_Simple_t *api);
typedef int (*API_IHV_Reset_func)(const struct API_IHV_Simple_t *api);

/**
 * @typedef API_IHV_Release_func
 *
 * @brief Function pointer type for the Release function.
 *
 * @details Release function releases all resources allocated during the Setup
 * function and unloads the IHV library. Expected exported function name is
 * API_IHV_Release.
 *
 * The IHV should release all resources allocated during the Setup function,
 * and unload the IHV library. The IHV library will be unloaded after this, so
 * make sure that resources are released properly, to prevent memory-leaks or
 * file still opened.
 */
typedef void (*API_IHV_Release_func)(const struct API_IHV_Release_t *api);
/**@} */

#ifdef __cplusplus
}
#endif

#ifdef _WIN32
#define EXPORT_API __declspec(dllexport)
#else
#define EXPORT_API __attribute__((visibility("default")))
#endif

// Macro to define exports for IHV API functions.

#define EXPORT_API_IHV                                                \
  EXPORT_API const struct API_IHV_Struct_t *API_IHV_Setup(            \
      const struct API_IHV_Setup_t *api);                             \
  EXPORT_API int API_IHV_Init(const struct API_IHV_Init_t *api);      \
  EXPORT_API int API_IHV_Prepare(const struct API_IHV_Simple_t *api); \
  EXPORT_API int API_IHV_Infer(struct API_IHV_Infer_t *api);          \
  EXPORT_API int API_IHV_Reset(const struct API_IHV_Simple_t *api);   \
  EXPORT_API int API_IHV_Deinit(const struct API_IHV_Deinit_t *api);  \
  EXPORT_API void API_IHV_Release(const struct API_IHV_Release_t *api);
