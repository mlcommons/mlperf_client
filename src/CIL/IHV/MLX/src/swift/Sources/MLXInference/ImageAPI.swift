import Foundation
import MLX

// C bridge for txt2img (diffusion) inference, e.g. FLUX.2 Klein.
//
// This mirrors the LLM bridge in API.swift but produces RGB pixels instead of
// tokens. The diffusion pipeline itself (Qwen3 text encoder + Flux2 transformer
// + VAE decoder + flow-match scheduler, ported from flux-2-swift-mlx) is wired
// in behind `FluxImagePipeline`. Until that port lands, `img_create` returns nil
// so the C++ ImageInference reports an unsupported-model error cleanly rather
// than emitting a bogus image.

/// Step callback marshalled across the C boundary, invoked once per denoising
/// step so the harness can timestamp progress.
public typealias ImageStepCallback = @convention(c) (
    UnsafeMutableRawPointer?, UInt32, UInt32
) -> Void

@_cdecl("img_create")
public func img_create(
    _ modelDir: UnsafePointer<CChar>?,
    _ modelName: UnsafePointer<CChar>?,
    _ deviceType: UnsafePointer<CChar>?,
    _ quantization: UnsafePointer<CChar>?
) -> UnsafeMutableRawPointer? {
    guard let modelDir, let modelName else {
        print("Error: img_create received nil arguments")
        return nil
    }

    let deviceTypeString = deviceType.map { String(cString: $0) } ?? "GPU"
    if deviceTypeString == "CPU" {
        Device.setDefault(device: Device(DeviceType.cpu))
    } else {
        Device.setDefault(device: Device(DeviceType.gpu))
    }

    let dir = String(cString: modelDir)
    let name = String(cString: modelName).lowercased()
    _ = quantization  // quant is baked into the pre-quantized model (model_index.json)

    do {
        let pipeline = try FluxImagePipeline.load(modelDirectory: dir, modelName: name)
        return Unmanaged.passRetained(pipeline).toOpaque()
    } catch {
        FileHandle.standardError.write(Data("[flux] load threw: \(error)\n".utf8))
        return nil
    }
}

@_cdecl("img_generate")
public func img_generate(
    _ imgPtr: UnsafeMutableRawPointer?,
    _ prompt: UnsafePointer<CChar>?,
    _ negativePrompt: UnsafePointer<CChar>?,
    _ width: UInt32,
    _ height: UInt32,
    _ steps: UInt32,
    _ guidance: Float,
    _ seed: Int64,
    _ stepCallback: ImageStepCallback?,
    _ callbackCtx: UnsafeMutableRawPointer?,
    _ outWidth: UnsafeMutablePointer<UInt32>?,
    _ outHeight: UnsafeMutablePointer<UInt32>?
) -> UnsafeMutablePointer<UInt8>? {
    guard let imgPtr, let prompt else { return nil }

    let pipeline = Unmanaged<FluxImagePipeline>.fromOpaque(imgPtr).takeUnretainedValue()
    let promptStr = String(cString: prompt)
    let negativeStr = negativePrompt.map { String(cString: $0) } ?? ""

    let onStep: (Int, Int) -> Void = { step, total in
        stepCallback?(callbackCtx, UInt32(step), UInt32(total))
    }

    do {
        let image = try pipeline.generate(
            prompt: promptStr,
            negativePrompt: negativeStr,
            width: Int(width),
            height: Int(height),
            steps: Int(steps),
            guidance: guidance,
            seed: seed,
            onStep: onStep)

        outWidth?.pointee = UInt32(image.width)
        outHeight?.pointee = UInt32(image.height)

        // Hand ownership of the RGB8 buffer to the caller (freed via img_free_image).
        let count = image.rgb.count
        let buffer = UnsafeMutablePointer<UInt8>.allocate(capacity: count)
        image.rgb.withUnsafeBufferPointer { src in
            buffer.update(from: src.baseAddress!, count: count)
        }
        return buffer
    } catch {
        return nil
    }
}

@_cdecl("img_free_model")
public func img_free_model(_ imgPtr: UnsafeMutableRawPointer?) {
    guard let imgPtr else { return }
    Unmanaged<FluxImagePipeline>.fromOpaque(imgPtr).release()
}

@_cdecl("img_free_image")
public func img_free_image(_ ptr: UnsafeMutablePointer<UInt8>?) {
    ptr?.deallocate()
}
