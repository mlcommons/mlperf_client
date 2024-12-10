#ifndef MATH_UTILS_H_
#define MATH_UTILS_H_

#include <vector>

namespace math_utils {
std::vector<float> Softmax(const std::vector<float>& logits);
}

#endif  // !MATH_UTILS_H_
