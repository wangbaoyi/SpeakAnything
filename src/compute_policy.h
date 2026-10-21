#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

// How the app chooses the device that runs ggml inference.
//   Auto: use a GPU when the build and the machine provide one, else CPU.
//   Cpu:  never touch a GPU backend.
//   Gpu:  require a GPU; loading fails with a clear error when there is none.
enum class ComputePolicy {
    Auto,
    Cpu,
    Gpu,
};

// Stored under QSettings("SenseVoice", "LocalDictation") with these keys.
// Recognition and translation select their devices independently: running ASR
// on the CPU must not force a slower CPU translator, and vice versa.
inline constexpr const char* compute_policy_settings_key = "compute/device";
inline constexpr const char* translation_compute_policy_settings_key = "translation/device";

// Whether this build compiled the ggml CUDA backend. Runtime detection still
// decides whether a device is present.
inline constexpr bool compute_build_has_cuda() {
#ifdef SENSEVOICE_WITH_CUDA
    return true;
#else
    return false;
#endif
}

// "auto", "cpu" or "gpu"; empty optional for anything else.
[[nodiscard]] std::optional<ComputePolicy> parse_compute_policy(std::string_view value);
[[nodiscard]] std::string_view compute_policy_code(ComputePolicy policy);

enum class ComputeDevice {
    Cpu,
    Gpu,
};

// What the ggml backend registry reports at runtime. The registry only lists
// devices that the build actually compiled and that initialized.
struct GpuCapability {
    bool available = false;
    // Backend registry name, e.g. "CUDA", "Metal", "Vulkan".
    std::string backend;
    // Short logical device name from ggml, e.g. "CUDA0".
    std::string name;
    // Human readable device description, e.g. "NVIDIA GeForce GTX 1080 Ti".
    std::string description;
    std::size_t memory_free = 0;
    std::size_t memory_total = 0;
    bool integrated = false;
    // Opaque ggml_backend_dev_t; only compute_policy.cpp and the engines use it.
    void* device_handle = nullptr;
};

// Detects the first GPU (or integrated GPU) device. Cached after the first
// call because the registry is static for the life of the process.
[[nodiscard]] GpuCapability detect_gpu_capability();

// "NVIDIA GeForce GTX 1080 Ti" (description when set, otherwise name).
// Inline so translation code can label a device without linking the ggml
// registry (and so tests can label fake devices).
[[nodiscard]] inline std::string gpu_display_name(const GpuCapability& capability) {
    const auto trim = [](const std::string& text) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return std::string();
        const auto last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    };
    std::string name = trim(capability.description);
    if (!name.empty()) return name;
    name = trim(capability.name);
    return name.empty() ? std::string("GPU") : name;
}

// "GPU (CUDA: NVIDIA GeForce GTX 1080 Ti)" or "CPU".
[[nodiscard]] inline std::string compute_device_label(
    ComputeDevice device, const GpuCapability& capability) {
    if (device == ComputeDevice::Cpu) return "CPU";
    const std::string backend = capability.backend.empty() ? "GPU" : capability.backend;
    return "GPU (" + backend + ": " + gpu_display_name(capability) + ")";
}

struct ComputeResolution {
    ComputeDevice device = ComputeDevice::Cpu;
    // Empty on success. Set when Gpu was requested but is unavailable.
    std::string error;

    [[nodiscard]] bool ok() const { return error.empty(); }
};

// Auto: GPU when available, otherwise CPU. Cpu: CPU. Gpu: GPU, or an error.
[[nodiscard]] ComputeResolution resolve_compute(
    ComputePolicy policy, const GpuCapability& capability);
