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
    char* llm_get_gpu_name();

    void llm_clear_cache();
    void llm_free_string(char* ptr);

    // ---- Image generation (txt2img, e.g. FLUX.2 Klein) ----------------------
    // Loads a diffusion pipeline from a local model directory. Returns an opaque
    // handle (Unmanaged pointer) or nullptr on failure.
    void* img_create(const char* modelDir, const char* modelName,
                     const char* deviceType, const char* quantization);

    // Per-denoising-step progress callback (used by the harness to timestamp
    // each step). `ctx` is passed back verbatim.
    typedef void (*img_step_callback)(void* ctx, uint32_t step,
                                      uint32_t total_steps);

    // Runs the diffusion loop and returns a freshly allocated, row-major RGB8
    // buffer of size (*outWidth * *outHeight * 3). Caller owns the buffer and
    // must release it with img_free_image. Returns nullptr on failure.
    uint8_t* img_generate(void* imgPtr, const char* prompt,
                          const char* negativePrompt, uint32_t width,
                          uint32_t height, uint32_t steps, float guidance,
                          int64_t seed, img_step_callback stepCallback,
                          void* callbackCtx, uint32_t* outWidth,
                          uint32_t* outHeight);

    void img_free_model(void* imgPtr);
    void img_free_image(uint8_t* ptr);
}