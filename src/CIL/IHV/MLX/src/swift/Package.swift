// swift-tools-version: 5.9
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "LlamaMLX",

    platforms: [
        .macOS("13.3"),
        .iOS(.v16),
        .visionOS(.v1),
    ],

    products: [
        // main targets
        .library(name: "LlamaMLX", targets: ["LlamaMLX"])
    ],
    dependencies: [
        .package(path: "../../../../../../deps/mlx-swift/")
    ],
    targets: [
        .target(
            name: "LlamaMLX",
            dependencies: [
                .product(name: "MLX", package: "mlx-swift"),
                .product(name: "MLXRandom", package: "mlx-swift"),
                .product(name: "MLXNN", package: "mlx-swift"),
                .product(name: "MLXOptimizers", package: "mlx-swift"),
                .product(name: "MLXFFT", package: "mlx-swift"),
            ],
            linkerSettings: [
                .linkedFramework("Foundation"),
                .linkedFramework("Metal"),
                .linkedFramework("Accelerate"),
            ]
        )

    ],
    cxxLanguageStandard: .gnucxx17
)