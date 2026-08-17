#include "txt2img_executor.h"

#include <log4cxx/logger.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <numeric>
#include <sstream>
#include <thread>

#include "image_inference.h"
#include "utils.h"

namespace fs = std::filesystem;

namespace cil::infer {

void Txt2ImgExecutor::RunScenario(EP ep, const nlohmann::json& settings) {
  std::unique_ptr<ImageInference> inference;
  try {
    inference = std::make_unique<ImageInference>(
        scenario_name_, model_base_name_, model_path_, deps_dir_, ep, settings,
        library_path_, executor_logger_->getName());
  } catch (const std::exception& e) {
    benchmark_result_.error_message = e.what();
    status_ = Status::kFailed;
    return;
  }

  if (auto error_message = inference->GetErrorMessage();
      !error_message.empty()) {
    benchmark_result_.error_message = inference->GetErrorMessage(false);
    benchmark_result_.ep_error_messages = inference->GetEPErrorMessages();
    status_ = Status::kFailed;
    return;
  }

  {
    std::lock_guard<std::mutex> lock(task_description_mutex_);
    task_description_ = ep_name_ + " on " + inference->GetDeviceType() +
                        " for " + display_name_;
  }

  if (status_ == Status::kCanceled) return;

  status_ = Status::kRunning;

  int sessions_count = (iterations_ + iterations_warmup_) * total_prompts_;
  int current_session_index = 0;

  std::string last_model_config;
  bool model_initialized = false;

  LOG4CXX_INFO(executor_logger_,
               "Running " << ep_name_ << " on " << inference->GetDeviceType()
                          << " for " << display_name_ << " with " << iterations_
                          << " iterations.");

  std::vector<double> end_to_end_durations;
  std::vector<double> steps_per_second_values;
  std::vector<double> first_step_seconds_values;
  std::unordered_map<std::string, std::vector<double>> submodule_durations_ms;
  double setup_load_seconds = 0.0;
  double setup_warmup_seconds = 0.0;

  const auto& required_categories = BenchmarkResult::kImageRequiredCategories;

  std::vector<std::vector<size_t>> skipped_prompts;
  bool has_skipped_prompts = false;
  bool has_skipped_required_prompts = false;

  for (const std::string& input_path : input_paths_) {
    if (status_ == Status::kCanceled) return;

    std::ifstream input_file(input_path);
    nlohmann::json input_json;
    input_file >> input_json;
    input_file.close();

    std::vector<std::string> input_prompt_jsons;
    for (const auto& prompt : input_json["prompts"]) {
      if (prompt.is_string()) {
        input_prompt_jsons.push_back(
            nlohmann::json{{"prompt", prompt.get<std::string>()}}.dump());
      } else {
        nlohmann::json obj;
        obj["prompt"] = prompt.value("positive", "");
        if (prompt.contains("negative"))
          obj["negative_prompt"] = prompt["negative"];
        input_prompt_jsons.push_back(obj.dump());
      }
    }

    std::string category = input_json.value("category", "Image Generation");

    uint32_t width = 1024;
    uint32_t height = 1024;
    uint32_t num_images_per_prompt = 1;
    if (input_json.contains("model_config") &&
        input_json["model_config"].contains("image_generation")) {
      const auto& img_cfg = input_json["model_config"]["image_generation"];
      width = img_cfg.value("width", width);
      height = img_cfg.value("height", height);
      num_images_per_prompt = img_cfg.value("num_images_per_prompt", num_images_per_prompt);
    }

    std::string model_config = input_json["model_config"].dump();

    if (!model_initialized) {
      inference->Init(model_config);
      last_model_config = model_config;
    } else if (model_config != last_model_config) {
      inference->Deinit();
      inference->Init(model_config);
      last_model_config = model_config;
    }

    if (auto error_message = inference->GetErrorMessage();
        !error_message.empty()) {
      benchmark_result_.error_message = inference->GetErrorMessage(false);
      benchmark_result_.ep_error_messages = inference->GetEPErrorMessages();
      status_ = Status::kFailed;
      return;
    }

    setup_load_seconds += inference->GetLastInitSeconds();
    model_initialized = true;

    bool is_required_category =
        required_categories.empty() ||
        std::ranges::find(required_categories, category) !=
            required_categories.end();

    std::vector<size_t> skipped_prompts_for_input;
    size_t prompt_index = 0;
    for (size_t i = 0; i < input_prompt_jsons.size(); ++i) {
      const auto& prompt_json = input_prompt_jsons[i];
      LOG4CXX_DEBUG(executor_logger_, "Prompt JSON: " + prompt_json);
      LOG4CXX_DEBUG(executor_logger_, "Category: " + category);

      bool is_success = true;
      bool empty_output = false;
      bool image_saved = false;
      uint32_t num_images_saved = 0;

      auto check_error = [&]() {
        if (const auto& infer_error_message = inference->GetErrorMessage();
            !infer_error_message.empty()) {
          benchmark_result_.error_message = inference->GetErrorMessage(false);
          benchmark_result_.ep_error_messages = inference->GetEPErrorMessages();
          is_success = false;
          return true;
        }
        return false;
      };

      ImageInference::Result last_result{};
      int iter = 0;
      for (; iter < iterations_ + iterations_warmup_;
           ++iter, ++current_session_index,
           progress_ = current_session_index * 100 / sessions_count) {
        if (status_ == Status::kCanceled) {
          inference->Deinit();
          return;
        }

        inference->Prepare();
        if (check_error()) break;

        std::this_thread::sleep_for(inference_delay_);
        auto result = inference->Run(prompt_json, width, height);
        result.category = category;

        bool infer_error = check_error();

        inference->Reset();

        if (infer_error || check_error()) break;

        bool is_warmup = iter < iterations_warmup_;

        auto iter_e2e = std::chrono::duration<double>(result.infer_end -
                                                      result.infer_start);
        result.num_generated_images =
            result.num_generated_images > 0
                ? result.num_generated_images
                : num_images_per_prompt;
        size_t num_images = static_cast<size_t>(result.num_generated_images);
        LOG4CXX_DEBUG(executor_logger_, "iter: " + std::to_string(iter) 
            + " num_generated_images: " + std::to_string(num_images));
        const size_t bytes_per_image =
            static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 3;

        if (is_warmup) {
          setup_warmup_seconds += iter_e2e.count();
        } else {
          // Each Run() produces num_images_per_prompt images back-to-back;
          // normalize end-to-end time to per-image so Avg End-to-End and
          // Images/Min count every generated image (no-op when N == 1).

          end_to_end_durations.push_back(
              iter_e2e.count() /
              static_cast<double>(num_images > 0 ? num_images : 1));

          if (!result.step_timestamps.empty() &&
              result.generation_start != ImageInference::Timestamp{} &&
              result.generation_end != ImageInference::Timestamp{}) {
            if (auto gen_dur = std::chrono::duration<double>(
                    result.generation_end - result.generation_start);
                gen_dur.count() > 0.0) {
              steps_per_second_values.push_back(
                  static_cast<double>(result.step_timestamps.size()) /
                  gen_dur.count());
            }

            if (auto first_step = std::chrono::duration<double>(
                    result.step_timestamps.front() - result.generation_start);
                first_step.count() >= 0.0) {
              first_step_seconds_values.push_back(first_step.count());
            }
          }

          for (auto& [name, durations] : result.submodule_durations_ms) {
            auto& bucket = submodule_durations_ms[name];
            bucket.insert(bucket.end(), durations.begin(), durations.end());
          }
        }

        if (result.pixel_data.empty()) {
          empty_output = true;
        }

        // Save the sample image on the LAST iteration, not the first.
        // PNG encode + disk write idles the GPU; doing it after iter 0
        // stalled the GPU right before the first *measured* iter, which
        // then started on dropped boost clocks. After the last iter the
        // only thing that follows is the next prompt's warmup iter (which
        // is discarded), so the save gap costs nothing measured.
        if (!result.pixel_data.empty() &&
            iter == iterations_ + iterations_warmup_ - 1) {
          auto now_sys = std::chrono::system_clock::now();
          auto tt = std::chrono::system_clock::to_time_t(now_sys);
          std::tm tm_buf{};
#ifdef _WIN32
          localtime_s(&tm_buf, &tt);
#else
          localtime_r(&tt, &tm_buf);
#endif
          // The IHV packs num_images_per_prompt images back-to-back (each
          // width*height*3 bytes); save every one.
          for (uint32_t img_k = 0; img_k < num_images; ++img_k) {
            std::ostringstream filename_stream;
            filename_stream << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << "_"
                            << ep_name_ << "_prompt" << prompt_index;
            // Single-image EPs keep the original filename (no _img suffix) so
            // their output is unchanged; only multi-image runs get the index.
            if (num_images > 1) filename_stream << "_img" << img_k;
            filename_stream << ".png";
            if (SaveImageOrFail(filename_stream.str(),
                                result.pixel_data.data() + img_k * bytes_per_image,
                                result.width, result.height)) {
              image_saved = true;
              ++num_images_saved;
            }
          }
        }

        last_result = result;
      }

      ++prompt_index;
      current_prompt_ = static_cast<int>(prompt_index);

      if (image_saved) {
        // Remember the human-readable prompt that produced the just-saved
        // image, paired by index with output_image_paths. Recorded only when
        // the image was actually saved, so the two vectors stay aligned.
        const auto& prompt_node = input_json["prompts"][i];
        std::string prompt_text =
            prompt_node.is_string()
                ? prompt_node.get<std::string>()
                : prompt_node.value("positive", std::string{});
        for (uint32_t img_k = 0; img_k < num_images_saved; ++img_k) {
          benchmark_result_.input_prompts.push_back(prompt_text);
          benchmark_result_.input_categories.push_back(category);
          benchmark_result_.input_history.emplace_back();
        }
      }

      if (!is_success || empty_output) {
        if (skip_failed_required_prompts_ || !is_required_category) {
          skipped_prompts_for_input.push_back(i);
          has_skipped_prompts = true;
          if (is_required_category) has_skipped_required_prompts = true;
          current_session_index += iterations_ + iterations_warmup_ - iter;
          progress_ = current_session_index * 100 / sessions_count;
          inference->ClearErrorMessage();
          inference->Deinit();
          inference->Init(model_config);
          continue;
        } else {
          status_ = Status::kFailed;
          if (benchmark_result_.error_message.empty()) {
            benchmark_result_.error_message =
                "Empty output in image generation result.";
          }
          inference->Deinit();
          return;
        }
      }
    }

    skipped_prompts.push_back(skipped_prompts_for_input);
  }

  if (model_initialized) {
    inference->Deinit();
  }

  if (status_ == Status::kRunning) {
    status_ = Status::kCompleted;
  }

  if (has_skipped_prompts)
    LOG4CXX_WARN(executor_logger_,
                 "Some prompts were skipped due to failures.");

  if (has_skipped_required_prompts) {
    LOG4CXX_WARN(executor_logger_,
                 "Some required prompts were skipped; overall metrics "
                 "may be unreliable.");
  }

  benchmark_result_.is_image_benchmark = true;
  benchmark_result_.benchmark_success = status_ == Status::kCompleted;
  benchmark_result_.device_type = inference->GetDeviceType();
  benchmark_result_.skipped_prompts = std::move(skipped_prompts);

  if (!end_to_end_durations.empty()) {
    const auto [min_it, max_it] =
        std::ranges::minmax_element(end_to_end_durations);
    const double sum = std::accumulate(end_to_end_durations.begin(),
                                       end_to_end_durations.end(), 0.0);
    const auto count = static_cast<double>(end_to_end_durations.size());
    const double avg_e2e = sum / count;

    double variance_sum = 0.0;
    for (double v : end_to_end_durations) {
      const double d = v - avg_e2e;
      variance_sum += d * d;
    }
    const double stdev = count > 1.0 ? std::sqrt(variance_sum / count) : 0.0;

    benchmark_result_.avg_end_to_end_seconds = avg_e2e;
    benchmark_result_.min_end_to_end_seconds = *min_it;
    benchmark_result_.max_end_to_end_seconds = *max_it;
    benchmark_result_.stdev_end_to_end_seconds = stdev;
    benchmark_result_.images_per_minute = avg_e2e > 0.0 ? 60.0 / avg_e2e : 0.0;
    benchmark_result_.duration = avg_e2e;
  }

  if (!steps_per_second_values.empty()) {
    benchmark_result_.steps_per_second =
        std::accumulate(steps_per_second_values.begin(),
                        steps_per_second_values.end(), 0.0) /
        static_cast<double>(steps_per_second_values.size());
  }

  if (!first_step_seconds_values.empty()) {
    benchmark_result_.first_step_seconds =
        std::accumulate(first_step_seconds_values.begin(),
                        first_step_seconds_values.end(), 0.0) /
        static_cast<double>(first_step_seconds_values.size());
  }

  benchmark_result_.setup_load_seconds = setup_load_seconds;
  benchmark_result_.setup_warmup_seconds = setup_warmup_seconds;
  benchmark_result_.submodule_timings_ms = std::move(submodule_durations_ms);

  if (!input_paths_.empty()) {
    std::ifstream f(input_paths_[0]);
    nlohmann::json j;
    f >> j;
    if (j.contains("model_config") &&
        j["model_config"].contains("image_generation")) {
      const auto& cfg = j["model_config"]["image_generation"];
      benchmark_result_.image_width = cfg.value("width", 0u);
      benchmark_result_.image_height = cfg.value("height", 0u);
      benchmark_result_.num_inference_steps =
          cfg.value("num_inference_steps", 0u);
    }
  }
}

}  // namespace cil::infer
