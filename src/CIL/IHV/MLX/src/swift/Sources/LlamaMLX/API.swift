import Cmlx
import Foundation
import MLX


@_cdecl("llm_clear_cache")
public func llm_clear_cache() {
    GPU.clearCache()
}

@_cdecl("llm_create")
public func llm_create(
    _ configPath: UnsafePointer<CChar>?,
    _ weightsDir: UnsafePointer<CChar>?,
    _ modelName: UnsafePointer<CChar>?,
    _ deviceType: UnsafePointer<CChar>?) -> UnsafeMutableRawPointer? {

    guard let configPath = configPath else {
        print("Error: configPath was nil")
        return nil
    }
    guard let weightsDir = weightsDir else {
        print("Error: weightsDir was nil")
        return nil
    }
    guard let modelName = modelName else {
        print("Error: modelName was nil")
        return nil
    }
    guard let deviceType = deviceType else {
        print("Error: DeviceType was nil")
        return nil
    }

    do {
        let deviceTypeString = String(cString: deviceType)
        if (deviceTypeString == "CPU") {
            Device.setDefault(device: Device(DeviceType.cpu))
        }
        else {
            Device.setDefault(device: Device(DeviceType.gpu))
        }
        let configPathStr = String(cString: configPath)
        let weightsDirStr = String(cString: weightsDir)
        let url = URL(fileURLWithPath: configPathStr)

        let data = try Data(contentsOf: url)

        let name = String(cString: modelName).lowercased()
        if name == "llama2" || name == "llama3" {
            let config = try JSONDecoder().decode(LlamaConfiguration.self, from: data)
            let model = LlamaModel(config)
            let baseConfig = try JSONDecoder().decode(BaseConfiguration.self, from: data)

            try loadWeights(modelDirectory: URL(fileURLWithPath: weightsDirStr), model: model, quantization: baseConfig.quantization)

            return Unmanaged.passRetained(model).toOpaque()
        }
        else if name == "phi3.5" || name == "phi4" {
            let config = try JSONDecoder().decode(Phi3Configuration.self, from: data)
            let model = Phi3Model(config)
            let baseConfig = try JSONDecoder().decode(BaseConfiguration.self, from: data)

            try loadWeights(modelDirectory: URL(fileURLWithPath: weightsDirStr), model: model, quantization: baseConfig.quantization)

            return Unmanaged.passRetained(model).toOpaque()
        }
        else {
            print("Unsupported model: \(name)")
            return nil
        }
    }
    catch {
        print("Failed to create model", error)
        return nil
    }
}

@_cdecl("llm_create_kvcache")
public func llm_create_kvcache(_ llmPtr: UnsafeMutableRawPointer?, _ modelName: UnsafePointer<CChar>?) -> UnsafeMutableRawPointer? {
    guard
        let llmPtr = llmPtr,
        let modelName = modelName
    else {
        return nil
    }
    let model: LanguageModel
    let name = String(cString: modelName).lowercased()

    if name == "llama2" || name == "llama3" {
        model = Unmanaged<LlamaModel>.fromOpaque(llmPtr).takeUnretainedValue()
    }
    else if name == "phi3.5" || name == "phi4" {
        model = Unmanaged<Phi3Model>.fromOpaque(llmPtr).takeUnretainedValue()
    }
    else {
        return nil
    }
    let caches = model.newCache()

    let cachesArray = KVCacheArrayContainer(caches)

    return Unmanaged.passRetained(cachesArray).toOpaque()
}

@_cdecl("llm_predict")
public func llm_predict(
    _ llmPtr: UnsafeMutableRawPointer?,
    _ dataPtr: UnsafePointer<UInt32>?,
    _ length: Int,
    _ cachePtr: UnsafeMutableRawPointer?,
    _ modelName: UnsafePointer<CChar>?,
    _ searchType: UnsafePointer<CChar>,
    _ prepare: Bool
) -> Int32
{
    guard
        let llmPtr = llmPtr,
        let dataPtr = dataPtr,
        let cachePtr = cachePtr,
        let modelName = modelName
    else {
        return -1
    }
    let name = String(cString: modelName).lowercased()
    var logitsMLX: MLXArray
    
    let cachesArray = Unmanaged<KVCacheArrayContainer>.fromOpaque(cachePtr).takeUnretainedValue()
    let caches = cachesArray.caches

    let inputArray = Array(UnsafeBufferPointer(start: dataPtr, count: length))
    var inputMLX = MLXArray(inputArray, [1, length])

    if name == "llama2" || name == "llama3" {
        let model = Unmanaged<LlamaModel>.fromOpaque(llmPtr).takeUnretainedValue()
        if prepare {
            do {
                inputMLX = try model.prepare(inputMLX, cache: caches, windowSize: 512)
            }
            catch {
                return -1
            }
        }
        logitsMLX = model.callAsFunction(inputMLX, cache: caches)
    }
    else if name == "phi3.5" || name == "phi4" {
        let model = Unmanaged<Phi3Model>.fromOpaque(llmPtr).takeUnretainedValue()
        if prepare {
            do {
                inputMLX = try model.prepare(inputMLX, cache: caches, windowSize: 512)
            }
            catch {
                return -1
            }
        }
        logitsMLX = model.callAsFunction(inputMLX, cache: caches)
    }
    else {
        return -1
    }
    
    var nextToken: Int32 = -1
    let swiftStringSearchType = String(cString: searchType)
    if (swiftStringSearchType == "greedy") {
        logitsMLX = logitsMLX[0..., -1, 0...]
        let predictions = logitsMLX.argMax(axis: 1)
        nextToken = predictions.item()
    }

    return nextToken
}

@_cdecl("llm_free_model")
public func llm_free_model(_ llmPtr: UnsafeMutableRawPointer?, _ modelName: UnsafePointer<CChar>?) {
    guard
        let llmPtr = llmPtr,
        let modelName = modelName
    else {
        return
    }
    let name = String(cString: modelName).lowercased()
    if name == "llama2" || name == "llama3" {
        Unmanaged<LlamaModel>.fromOpaque(llmPtr).release()
    }
    else if name == "phi3.5" || name == "phi4" {
        Unmanaged<Phi3Model>.fromOpaque(llmPtr).release()
    }
}

@_cdecl("llm_free_kvcache")
public func llm_free_kvcache(_ cachesPtr: UnsafeMutableRawPointer?) {
    guard 
        let cachesPtr = cachesPtr
    else {
        return
    }

    Unmanaged<KVCacheArrayContainer>.fromOpaque(cachesPtr).release()
}

@_cdecl("llm_free_logits")
public func llm_free_logits(_ ptr: UnsafePointer<Float>?) {
    guard let ptr = ptr else { return }

    ptr.withMemoryRebound(to: Float.self, capacity: 1) {
        UnsafeMutablePointer(mutating: $0).deallocate()
    }
}
