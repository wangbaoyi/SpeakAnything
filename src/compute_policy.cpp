#include "compute_policy.h"

#include "ggml-backend.h"

#include <mutex>

namespace {

GpuCapability describe_device(ggml_backend_dev_t device) {
    GpuCapability capability;
    ggml_backend_dev_props props{};
    ggml_backend_dev_get_props(device, &props);
    capability.available = true;
    capability.device_handle = device;
    capability.name = props.name != nullptr ? props.name : "";
    capability.description = props.description != nullptr ? props.description : "";
    capability.memory_free = props.memory_free;
    capability.memory_total = props.memory_total;
    capability.integrated = props.type == GGML_BACKEND_DEVICE_TYPE_IGPU;
    const ggml_backend_reg_t registry = ggml_backend_dev_backend_reg(device);
    if (registry != nullptr) {
        const char* name = ggml_backend_reg_name(registry);
        capability.backend = name != nullptr ? name : "";
    }
    return capability;
}

} // namespace

std::optional<ComputePolicy> parse_compute_policy(std::string_view value) {
    if (value == "auto") return ComputePolicy::Auto;
    if (value == "cpu") return ComputePolicy::Cpu;
    if (value == "gpu") return ComputePolicy::Gpu;
    return std::nullopt;
}

std::string_view compute_policy_code(ComputePolicy policy) {
    switch (policy) {
    case ComputePolicy::Auto: return "auto";
    case ComputePolicy::Cpu: return "cpu";
    case ComputePolicy::Gpu: return "gpu";
    }
    return "auto";
}

GpuCapability detect_gpu_capability() {
    static std::once_flag once;
    static GpuCapability cached;
    std::call_once(once, [] {
        // Discrete GPUs first; an integrated GPU is still preferable to the CPU
        // when the build provides no discrete device.
        for (const enum ggml_backend_dev_type type :
             {GGML_BACKEND_DEVICE_TYPE_GPU, GGML_BACKEND_DEVICE_TYPE_IGPU}) {
            ggml_backend_dev_t device = ggml_backend_dev_by_type(type);
            if (device != nullptr) {
                cached = describe_device(device);
                return;
            }
        }
    });
    return cached;
}

ComputeResolution resolve_compute(
    ComputePolicy policy, const GpuCapability& capability) {
    ComputeResolution resolution;
    switch (policy) {
    case ComputePolicy::Cpu:
        resolution.device = ComputeDevice::Cpu;
        break;
    case ComputePolicy::Auto:
        resolution.device = capability.available ? ComputeDevice::Gpu : ComputeDevice::Cpu;
        break;
    case ComputePolicy::Gpu:
        if (capability.available) {
            resolution.device = ComputeDevice::Gpu;
        } else {
            resolution.device = ComputeDevice::Cpu;
            resolution.error =
                "GPU compute was requested, but no GPU backend is available. ";
            resolution.error += compute_build_has_cuda()
                ? "CUDA is compiled in, but no usable device initialized (check the driver)."
                : "This build was compiled without CUDA.";
        }
        break;
    }
    return resolution;
}
