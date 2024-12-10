#include "math_utils.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace math_utils {

std::vector<float> Softmax(const std::vector<float>& logits) {
  std::vector<float> probabilities(logits.size());
  float max_logit = *std::max_element(logits.begin(), logits.end());

  // Compute the exponentials of the normalized logits
  std::transform(
      logits.begin(), logits.end(), probabilities.begin(),
      [max_logit](float logit) { return std::exp(logit - max_logit); });

  // Compute the sum of the exponentials
  float sum = std::accumulate(probabilities.begin(), probabilities.end(), 0.0f);

  // Normalize to get probabilities
  std::transform(probabilities.begin(), probabilities.end(),
                 probabilities.begin(), [sum](float val) { return val / sum; });

  return probabilities;
}

}  // namespace math_utils
