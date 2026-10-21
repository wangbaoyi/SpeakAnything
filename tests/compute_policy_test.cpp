#include "compute_policy.h"

#include <cassert>
#include <string>

namespace {

GpuCapability fake_gpu(bool available) {
    GpuCapability capability;
    capability.available = available;
    if (available) {
        capability.backend = "CUDA";
        capability.name = "CUDA0";
        capability.description = "NVIDIA GeForce GTX 1080 Ti";
        capability.memory_free = 8ULL * 1024 * 1024 * 1024;
        capability.memory_total = 11ULL * 1024 * 1024 * 1024;
        capability.device_handle = reinterpret_cast<void*>(0x1);
    }
    return capability;
}

} // namespace

int main() {
    // Parsing and persistence codes.
    assert(parse_compute_policy("auto") == ComputePolicy::Auto);
    assert(parse_compute_policy("cpu") == ComputePolicy::Cpu);
    assert(parse_compute_policy("gpu") == ComputePolicy::Gpu);
    assert(!parse_compute_policy("CUDA").has_value());
    assert(!parse_compute_policy("").has_value());
    assert(compute_policy_code(ComputePolicy::Auto) == "auto");
    assert(compute_policy_code(ComputePolicy::Cpu) == "cpu");
    assert(compute_policy_code(ComputePolicy::Gpu) == "gpu");
    assert(std::string(compute_policy_settings_key) == "compute/device");

    // CPU never selects a GPU, even when one is present.
    for (const bool available : {false, true}) {
        const ComputeResolution forced_cpu =
            resolve_compute(ComputePolicy::Cpu, fake_gpu(available));
        assert(forced_cpu.ok());
        assert(forced_cpu.device == ComputeDevice::Cpu);
    }

    // Auto prefers the GPU and falls back to the CPU without an error.
    const ComputeResolution auto_gpu = resolve_compute(ComputePolicy::Auto, fake_gpu(true));
    assert(auto_gpu.ok());
    assert(auto_gpu.device == ComputeDevice::Gpu);
    const ComputeResolution auto_cpu = resolve_compute(ComputePolicy::Auto, fake_gpu(false));
    assert(auto_cpu.ok());
    assert(auto_cpu.device == ComputeDevice::Cpu);

    // GPU is mandatory under the Gpu policy and fails clearly without one.
    const ComputeResolution required_gpu =
        resolve_compute(ComputePolicy::Gpu, fake_gpu(true));
    assert(required_gpu.ok());
    assert(required_gpu.device == ComputeDevice::Gpu);
    const ComputeResolution missing_gpu =
        resolve_compute(ComputePolicy::Gpu, fake_gpu(false));
    assert(!missing_gpu.ok());
    assert(missing_gpu.device == ComputeDevice::Cpu);
    assert(missing_gpu.error.find("GPU") != std::string::npos);

    // Device labels name the backend and device, never just "GPU".
    assert(compute_device_label(ComputeDevice::Cpu, fake_gpu(true)) == "CPU");
    assert(compute_device_label(ComputeDevice::Gpu, fake_gpu(true)) ==
           "GPU (CUDA: NVIDIA GeForce GTX 1080 Ti)");
    assert(gpu_display_name(fake_gpu(true)) == "NVIDIA GeForce GTX 1080 Ti");
    GpuCapability unnamed;
    unnamed.available = true;
    unnamed.backend = "Vulkan";
    unnamed.name = "Vulkan0";
    assert(compute_device_label(ComputeDevice::Gpu, unnamed) == "GPU (Vulkan: Vulkan0)");

    // Runtime detection is consistent: an available device has an opaque
    // handle and a backend name from the ggml registry.
    const GpuCapability detected = detect_gpu_capability();
    if (detected.available) {
        assert(detected.device_handle != nullptr);
        assert(!detected.backend.empty());
    }
    return 0;
}
