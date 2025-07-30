#include "io_report_reader.h"

namespace {
bool CFStringEquals(CFStringRef a, const char* b) {
  if (!a) return false;
  CFStringRef b_str =
      CFStringCreateWithCString(nullptr, b, kCFStringEncodingUTF8);
  bool result = CFStringCompare(a, b_str, 0) == kCFCompareEqualTo;
  CFRelease(b_str);
  return result;
}
}  // namespace

extern "C" {
IOReportSubscriptionRef IOReportCreateSubscription(
    void* allocator, CFMutableDictionaryRef desired_channels,
    CFMutableDictionaryRef* subscribed_channels_out, uint64_t channel_id,
    CFTypeRef options);
CFMutableDictionaryRef IOReportCopyChannelsInGroup(CFStringRef group,
                                                   CFStringRef subgroup,
                                                   uint64_t flags,
                                                   uint64_t mask1,
                                                   uint64_t mask2);
void IOReportMergeChannels(CFDictionaryRef dst_channels,
                           CFDictionaryRef src_channels, CFTypeRef options);

CFDictionaryRef IOReportCreateSamples(
    IOReportSubscriptionRef subscription,
    CFMutableDictionaryRef subscribed_channels, CFTypeRef options);
CFDictionaryRef IOReportCreateSamplesDelta(CFDictionaryRef previous_sample,
                                           CFDictionaryRef current_sample,
                                           CFTypeRef options);
long IOReportSimpleGetIntegerValue(CFDictionaryRef channel_sample, int index);
int IOReportStateGetCount(CFDictionaryRef channel_sample);
uint64_t IOReportStateGetResidency(CFDictionaryRef channel_sample, int index);
CFStringRef IOReportStateGetNameForIndex(CFDictionaryRef channel_sample,
                                         int index);
CFStringRef IOReportChannelGetUnitLabel(CFDictionaryRef channel_sample);
}

IOReportReader::IOReportReader()
    : subscription_(nullptr),
      channels_(nullptr),
      subscribed_channels_(nullptr),
      previous_sample_(nullptr) {
  channels_ = IOReportCopyChannelsInGroup(
      CFSTR("GPU Stats"), CFSTR("GPU Performance States"), 0, 0, 0);
  if (!channels_) return;

  CFDictionaryRef energy_channels =
      IOReportCopyChannelsInGroup(CFSTR("Energy Model"), nullptr, 0, 0, 0);

  if (energy_channels) {
    IOReportMergeChannels(channels_, energy_channels, nullptr);
    CFRelease(energy_channels);
  }

  subscription_ = IOReportCreateSubscription(nullptr, channels_,
                                             &subscribed_channels_, 0, nullptr);
  if (!subscription_) {
    CFRelease(channels_);
    channels_ = nullptr;
    return;
  }

  previous_sample_ = IOReportCreateSamples(subscription_, channels_, nullptr);
  previous_sample_time_ = std::chrono::steady_clock::now();
}

IOReportReader::~IOReportReader() {
  if (previous_sample_) CFRelease(previous_sample_);
  if (subscribed_channels_) CFRelease(subscribed_channels_);
  if (channels_) CFRelease(channels_);
  if (subscription_) CFRelease(subscription_);
}

std::pair<double, double> IOReportReader::GetGpuAndNpuUsage() {
  if (!isValid()) return std::make_pair(-1.0, -1.0);

  auto now = std::chrono::steady_clock::now();
  double duration_sec =
      std::chrono::duration<double>(now - previous_sample_time_).count();
  previous_sample_time_ = now;

  CFDictionaryRef current_sample =
      IOReportCreateSamples(subscription_, channels_, nullptr);
  if (!current_sample) return std::make_pair(-1.0, -1.0);

  CFDictionaryRef delta =
      IOReportCreateSamplesDelta(previous_sample_, current_sample, nullptr);
  CFRelease(previous_sample_);
  previous_sample_ = current_sample;

  if (!delta) return std::make_pair(-1.0, -1.0);

  CFArrayRef channels_array = static_cast<CFArrayRef>(
      CFDictionaryGetValue(delta, CFSTR("IOReportChannels")));
  if (!channels_array) {
    CFRelease(delta);
    return std::make_pair(-1.0, -1.0);
  }

  int64_t gpu_active_residency = 0;
  int64_t gpu_total_residency = 0;
  double ane_energy_joules = 0.0;
  const double ane_max_power = 8.0;  // assumption (from tool asitop)

  for (CFIndex i = 0; i < CFArrayGetCount(channels_array); ++i) {
    CFDictionaryRef chan =
        static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(channels_array, i));
    if (!chan) continue;

    CFStringRef group = static_cast<CFStringRef>(
        CFDictionaryGetValue(chan, CFSTR("IOReportGroupName")));
    if (!group) continue;

    if (CFStringEquals(group, "GPU Stats")) {
      int state_count = IOReportStateGetCount(chan);
      for (int j = 0; j < state_count; ++j) {
        CFStringRef state_name = IOReportStateGetNameForIndex(chan, j);
        int64_t residency = IOReportStateGetResidency(chan, j);
        gpu_total_residency += residency;

        if (!CFStringEquals(state_name, "IDLE") &&
            !CFStringEquals(state_name, "OFF") &&
            !CFStringEquals(state_name, "DOWN")) {
          gpu_active_residency += residency;
        }
      }
    } else if (CFStringEquals(group, "Energy Model")) {
      CFArrayRef legend = static_cast<CFArrayRef>(
          CFDictionaryGetValue(chan, CFSTR("LegendChannel")));
      if (!legend) continue;

      bool is_ane = false;
      for (CFIndex j = 0; j < CFArrayGetCount(legend); ++j) {
        CFTypeRef val = CFArrayGetValueAtIndex(legend, j);
        if (CFGetTypeID(val) != CFStringGetTypeID()) continue;
        CFStringRef str = static_cast<CFStringRef>(val);
        if (CFStringFind(str, CFSTR("ANE"), 0).location != kCFNotFound) {
          is_ane = true;
          break;
        }
      }

      if (is_ane) {
        int64_t raw_energy = IOReportSimpleGetIntegerValue(chan, 0);
        CFStringRef unit = IOReportChannelGetUnitLabel(chan);
        double joules = 0.0;

        if (CFStringEquals(unit, "mJ")) {
          joules = static_cast<double>(raw_energy) / 1e3;
        } else if (CFStringEquals(unit, "uJ")) {
          joules = static_cast<double>(raw_energy) / 1e6;
        } else if (CFStringEquals(unit, "nJ")) {
          joules = static_cast<double>(raw_energy) / 1e9;
        }

        ane_energy_joules += joules;
      }
    }
  }

  CFRelease(delta);

  double gpu_usage = (gpu_total_residency > 0)
                         ? static_cast<double>(gpu_active_residency) /
                               gpu_total_residency * 100.0
                         : -1.0;

  double ane_power =
      (duration_sec > 0.0) ? ane_energy_joules / duration_sec : 0.0;
  double ane_usage = ane_power / ane_max_power * 100.0;

  return std::make_pair(gpu_usage, ane_usage);
}

bool IOReportReader::isValid() const {
  return subscription_ && channels_ && subscribed_channels_ && previous_sample_;
}
