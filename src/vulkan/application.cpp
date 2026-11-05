#include <oxide/vulkan/application.hpp>

namespace oxide {

Application::Application(
    const WindowConfig   &window_config,
    const char           *app_name,
    const SwapchainConfig &swapchain_config)
    : window_(window_config),
      device_(window_.handle(), app_name == nullptr ? "Oxide" : app_name),
      context_(device_, &window_, swapchain_config) {}

Application::Application(
    int                    width,
    int                    height,
    std::string            app_name,
    const SwapchainConfig &swapchain_config)
    : Application(
          WindowConfig{ width, height, app_name },
          app_name.c_str(),
          swapchain_config) {}

} // namespace oxide
