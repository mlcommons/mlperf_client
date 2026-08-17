#include "image_inference.h"

#include <log4cxx/logger.h>

#include <cassert>
#include <iomanip>
#include <memory>
#include <sstream>

#include "api_handler.h"
#include "utils.h"

namespace cil::infer {

#if IHV_SUBPROCESS
namespace {

// Runs inside the IHV subprocess. Records image-generation step timestamps
// when the IHV callback fires, so timing is captured before crossing the IPC
// boundary. Offsets are nanoseconds from inference start; the parent rebases
// them onto its own clock.
class ImageCallbackAdapter : public cil::API_Handler::CallbackAdapter {
 public:
  API_IHV_Callback_t GetCallback() override {
    return {API_IHV_Callback_Type::API_IHV_CB_ImageStep, this,
            reinterpret_cast<void*>(&StepCallbackStatic)};
  }

  void Start() override { start_ = Clock::now(); }

  void Finish() override { total_ns_ = ElapsedNs(); }

  nlohmann::json Serialize() const override {
    nlohmann::json data;
    data["step_offsets_ns"] = step_offsets_ns_;
    data["submodule_durations_ms"] = submodule_durations_ms_;
    data["total_ns"] = total_ns_;
    if (have_generation_start_) {
      data["generation_start_ns"] = generation_start_ns_;
    }
    if (have_generation_end_) {
      data["generation_end_ns"] = generation_end_ns_;
    }
    return data;
  }

 private:
  using Clock = ImageInference::Clock;

  static void StepCallbackStatic(void* object,
                                 const API_IHV_ImageStep_Event_t* event) {
    assert(object != nullptr);
    assert(event != nullptr);
    auto* self = static_cast<ImageCallbackAdapter*>(object);
    switch (event->phase) {
      case API_IHV_ImageStep_Start:
        self->have_generation_start_ = true;
        self->generation_start_ns_ = self->ElapsedNs();
        break;
      case API_IHV_ImageStep_Step:
        self->step_offsets_ns_.emplace_back(self->ElapsedNs());
        break;
      case API_IHV_ImageStep_End:
        self->have_generation_end_ = true;
        self->generation_end_ns_ = self->ElapsedNs();
        break;
      case API_IHV_ImageStep_Submodule:
        if (event->submodule != nullptr) {
          self->submodule_durations_ms_[event->submodule].push_back(
              event->duration_ms);
        }
        break;
    }
  }

  int64_t ElapsedNs() const {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                               start_)
        .count();
  }

  Clock::time_point start_{};
  bool have_generation_start_ = false;
  bool have_generation_end_ = false;
  int64_t generation_start_ns_ = 0;
  int64_t generation_end_ns_ = 0;
  std::vector<int64_t> step_offsets_ns_;
  std::unordered_map<std::string, std::vector<double>> submodule_durations_ms_;
  int64_t total_ns_ = 0;
};

[[maybe_unused]] const bool kImageCallbackAdapterRegistered = [] {
  cil::API_Handler::CallbackAdapter::Register(
      API_IHV_CB_ImageStep,
      [] { return std::make_unique<ImageCallbackAdapter>(); });
  return true;
}();

}  // namespace
#endif  // IHV_SUBPROCESS

ImageInference::ImageInference(const std::string& scenario_name,
                               const std::string& model_base_name,
                               const std::string& model_path,
                               const std::string& deps_dir, EP ep,
                               const nlohmann::json& ep_settings,
                               const std::string& library_path,
                               const std::string& logger_name)
    : BaseInference(model_path, deps_dir, ep, ep_settings, scenario_name,
                    model_base_name,
                    logger_name.empty()
                        ? utils::StringReplaceChar(model_base_name, '.', '_') +
                              "_image_executor"
                        : logger_name,
                    library_path) {}

ImageInference::~ImageInference() = default;

bool ImageInference::IsValid() const { return api_handler_->IsLoaded(); }

void ImageInference::ClearErrorMessage() {
  if (api_handler_) {
    api_handler_->ClearErrorMessage();
  }
  SetErrorMessage("");
}

void ImageInference::Init(const std::string& config) {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage("IHV not loaded!: " + library_path_);
    return;
  }

  auto init_start = Clock::now();

  if (!api_handler_->Init(config)) {
    SetErrorMessage("Failed to initialize IHV");
    return;
  }

  auto init_end = Clock::now();
  last_init_seconds_ =
      std::chrono::duration<double>(init_end - init_start).count();
  LogTime("IHV Init (model load) time: ", init_start, init_end);
  SetErrorMessage("");
}

void ImageInference::Prepare() {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage("IHV not loaded!: " + library_path_);
    return;
  }
  if (!api_handler_->Prepare()) {
    SetErrorMessage("Failed to prepare IHV");
  }
}

void ImageInference::Reset() {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage("IHV not loaded!: " + library_path_);
    return;
  }
  if (!api_handler_->Reset()) {
    SetErrorMessage("Failed to reset IHV");
  }
}

void ImageInference::Deinit() {
  if (!api_handler_->IsLoaded()) return;
  if (!api_handler_->Deinit()) {
    SetErrorMessage("Failed to deinit IHV");
  }
}

void ImageInference::ImageStepCallback(void* object,
                                       const API_IHV_ImageStep_Event_t* event) {
  assert(object != nullptr);
  assert(event != nullptr);
  auto* self = static_cast<ImageInference*>(object);
  const auto now = Clock::now();

  switch (event->phase) {
    case API_IHV_ImageStep_Start:
      self->generation_start_ = now;
      self->step_timestamps_.clear();
      if (event->total_steps > 0)
        self->step_timestamps_.reserve(event->total_steps);
      break;
    case API_IHV_ImageStep_Step:
      self->step_timestamps_.push_back(now);
      break;
    case API_IHV_ImageStep_End:
      self->generation_end_ = now;
      break;
    case API_IHV_ImageStep_Submodule:
      if (event->submodule != nullptr) {
        self->submodule_durations_ms_[event->submodule].push_back(
            event->duration_ms);
      }
      break;
  }
}

ImageInference::Result ImageInference::Run(const std::string& prompt_json,
                                           uint32_t width, uint32_t height) {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage("IHV not loaded!: " + library_path_);
    return {};
  }

  generation_start_ = {};
  generation_end_ = {};
  step_timestamps_.clear();
  submodule_durations_ms_.clear();

  API_IHV_Callback_t cb{API_IHV_Callback_Type::API_IHV_CB_ImageStep, this,
                        reinterpret_cast<void*>(&ImageStepCallback)};

  API_IHV_IO_Data_t io_data = {prompt_json.c_str(),
                               static_cast<unsigned>(prompt_json.size()),
                               nullptr, 0, cb};

  const auto infer_start = Clock::now();
  if (!api_handler_->Infer(io_data)) {
    SetErrorMessage("Failed to run image inference");
    return {};
  }
  auto infer_end = Clock::now();

#if IHV_SUBPROCESS
  // In subprocess mode, step timestamps are recorded inside the subprocess
  // (free of IPC latency) and returned as offsets from inference start.
  if (const auto& adapter_data = api_handler_->GetCallbackAdapterData();
      adapter_data.is_object() && adapter_data.contains("step_offsets_ns")) {
    auto rebase = [&](int64_t offset_ns) {
      return infer_start + std::chrono::duration_cast<Clock::duration>(
                               std::chrono::nanoseconds(offset_ns));
    };
    step_timestamps_.clear();
    for (int64_t offset_ns :
         adapter_data["step_offsets_ns"].get<std::vector<int64_t>>()) {
      step_timestamps_.emplace_back(rebase(offset_ns));
    }
    if (adapter_data.contains("generation_start_ns")) {
      generation_start_ =
          rebase(adapter_data["generation_start_ns"].get<int64_t>());
    }
    if (adapter_data.contains("generation_end_ns")) {
      generation_end_ =
          rebase(adapter_data["generation_end_ns"].get<int64_t>());
    }
    if (adapter_data.contains("submodule_durations_ms")) {
      submodule_durations_ms_ =
          adapter_data["submodule_durations_ms"]
              .get<std::unordered_map<std::string, std::vector<double>>>();
    }
    if (adapter_data.contains("total_ns")) {
      infer_end = infer_start + std::chrono::duration_cast<Clock::duration>(
                                    std::chrono::nanoseconds(
                                        adapter_data["total_ns"].get<int64_t>()));
    }
  }
#endif

  LogTime("Image generation completed in ", infer_start, infer_end);

  if (!step_timestamps_.empty()) {
    const size_t num_steps = step_timestamps_.size();
    auto gen_duration =
        std::chrono::duration<double>(generation_end_ - generation_start_);
    double steps_per_sec =
        gen_duration.count() > 0.0
            ? static_cast<double>(num_steps) / gen_duration.count()
            : 0.0;
    std::stringstream ss;
    ss << num_steps << " denoising steps, " << std::fixed
       << std::setprecision(2) << steps_per_sec << " steps/sec";
    LOG4CXX_INFO(GetLogger(), ss.str());
  }

  SetErrorMessage("");

  std::vector<uint8_t> pixels;
  if (io_data.output != nullptr && io_data.output_size > 0) {
    const auto* src = static_cast<const uint8_t*>(io_data.output);
    pixels.assign(src, src + io_data.output_size);
  }

  const size_t bytes_per_image =
      static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
  uint32_t num_generated_images = 0;
  if (bytes_per_image > 0 && !pixels.empty()) {
    num_generated_images =
        static_cast<uint32_t>(pixels.size() / bytes_per_image);
  }

  return Result{
      .pixel_data = std::move(pixels),
      .width = width,
      .height = height,
      .num_generated_images = num_generated_images,
      .generation_start = generation_start_,
      .generation_end = generation_end_,
      .infer_start = infer_start,
      .infer_end = infer_end,
      .step_timestamps = step_timestamps_,
      .submodule_durations_ms = std::move(submodule_durations_ms_),
  };
}

}  // namespace cil::infer
