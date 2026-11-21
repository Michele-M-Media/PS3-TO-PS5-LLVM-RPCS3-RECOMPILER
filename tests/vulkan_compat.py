"""Exercise the renderer's texture writer and device feature chain on a Vulkan host."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

TESTS = Path(__file__).resolve().parent
VK = TESTS.parent / "overlay/rpcs3/Emu/RSX/VK"


def check(headers):
    with tempfile.TemporaryDirectory() as temporary:
        build = Path(temporary)

        def compile_and_run(source, *options):
            binary = build / "check"
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-I", str(headers),
                            "-I", str(build), "-I", str(VK / "vkutils"), str(source),
                            *options, "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

        draw = (VK / "VKDraw.cpp").read_text()
        start = draw.index("bool VKGSRender::bind_texture_env()")
        end = draw.index("bool VKGSRender::bind_interpreter_texture_env()", start)
        (build / "bind_texture_env.inc").write_text(draw[start:end])
        compile_and_run(TESTS / "declared_texture_bindings.cpp", "-Wall", "-Wextra", "-Werror")
        print("Texture binding layout passed", flush=True)

        device = (VK / "vkutils/device.cpp").read_text()
        start = device.index("VkPhysicalDeviceFaultFeaturesEXT device_fault_info", device.index("VkDeviceCreateInfo device ="))
        end = device.index("VkPhysicalDeviceConditionalRenderingFeaturesEXT", start)
        (build / "device_fault_chain.inc").write_text(device[start:end])
        source = build / "chain.cpp"
        source.write_text("""
#include <vulkan/vulkan_core.h>
#include <cassert>
#include <initializer_list>
int main() {
    for (bool supported : {false, true}) {
        struct { struct { bool extended_device_fault; } optional_features_support; } gpu{{supported}};
        auto* pgpu = &gpu;
        VkPhysicalDeviceVulkan12Features required{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device.pNext = &required;
#include "device_fault_chain.inc"
        if (supported) {
            assert(device.pNext == &device_fault_info && device_fault_info.pNext == &required);
            assert(device_fault_info.deviceFault && !device_fault_info.deviceFaultVendorBinary);
        } else {
            assert(device.pNext == &required);
        }
    }
}
""")
        compile_and_run(source)
        print("Device feature chain passed with diagnostics enabled and disabled", flush=True)
        compile_and_run(TESTS / "descriptor_sampler.cpp", "-Wl,-l:libvulkan.so.1")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vulkan-include", type=Path, default=Path("/usr/include"))
    check(parser.parse_args().vulkan_include)
