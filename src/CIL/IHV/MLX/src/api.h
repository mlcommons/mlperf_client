#pragma once

#include <cstdint>

extern "C" {
    void* llm_create(const char* configPath, const char* weightsDir, const char* modelName, const char* deviceType = "GPU");
    void* llm_create_kvcache(void* llmPtr, const char* modelName);
    int32_t llm_predict(
        void* llmPtr,
        const uint32_t* dataPtr,
        int64_t length,
        void* cachePtr,
        const char* modelName,
        const char* searchType = "greedy",
        bool prepare = false
    );
    void llm_free_model(void* llmPtr, const char* modelName);
    void llm_free_kvcache(void* cachesPtr);
    void llm_free_logits(const float* ptr);

    void llm_clear_cache();
}