#include <oxide/glfw/glfw_oxide.hpp>
#include <oxide/tools/log.hpp>

#include <cstdio>
#include <stdexcept>
#include <utility>

namespace oxide {

std::unordered_map<GLFWwindow *, Window *> Window::instances_;

namespace {

void glfwErrorCallback(int code, const char *description) {
	OXIDE_LOG_ERROR << "GLFW error " << code << ": "
	                << (description != nullptr ? description : "(no description)");
}

} // namespace

// -----------------------------------------------------------------------------
// GLFW lifetime and window management
//
// GLFWContext controls the process-wide GLFW lifetime. Window owns one GLFW
// window and exposes only the operations needed by the renderer and main loop.
// -----------------------------------------------------------------------------
GLFWContext::GLFWContext() {
	// Step 1: initialize GLFW before any window or Vulkan surface is created.
	if (!glfwInit()) {
		const char *description = nullptr;
		glfwGetError(&description);
		throw runtimeError(std::string("failed to initialize GLFW") +
		                   (description != nullptr ? ": " + std::string(description) : ""));
	}
	glfwSetErrorCallback(glfwErrorCallback);
}

GLFWContext::~GLFWContext() {
	glfwTerminate();
}

Window::Window(const WindowConfig &config) {
	// Step 2: reject invalid settings before passing them to GLFW.
	if (config.width <= 0 || config.height <= 0 || config.title.empty()) {
		throw invalidArgument("window dimensions and title must be valid");
	}

	// Step 3: create a window with Vulkan presentation handled externally.
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, config.transparent ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_DECORATED, config.decorated ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_FLOATING, config.floating ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHintString(GLFW_WAYLAND_APP_ID, "make-me-float"); // debug helper for hyprland
	window_ = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
	if (window_ == nullptr) {
		const char *description = nullptr;
		glfwGetError(&description);
		throw runtimeError(std::string("failed to create GLFW window") +
		                   (description != nullptr ? ": " + std::string(description) : ""));
	}
	if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) {
		glfwGetWindowPos(window_, &position_.x, &position_.y);
	}
	instances_[window_] = this;
	glfwSetKeyCallback(window_, keyCallback);
	glfwSetScrollCallback(window_, scrollCallback);
}

Window::~Window() {
	// Destroying the window releases the native handle owned by this wrapper.
	release();
}

Window::Window(Window &&other) noexcept
    : window_(other.window_),
      pressed_keys_(std::move(other.pressed_keys_)),
      released_keys_(std::move(other.released_keys_)),
      scroll_delta_(other.scroll_delta_),
      position_(other.position_) {
	if (window_ != nullptr) {
		instances_[window_] = this;
	}
	other.window_       = nullptr;
	other.scroll_delta_ = {};
}

Window &Window::operator=(Window &&other) noexcept {
	// Move assignment replaces this wrapper's owned native handle. Release the
	// previous handle first so its callback mapping cannot outlive it.
	if (this != &other) {
		release();
		window_        = other.window_;
		pressed_keys_  = std::move(other.pressed_keys_);
		released_keys_ = std::move(other.released_keys_);
		scroll_delta_  = other.scroll_delta_;
		position_      = other.position_;
		if (window_ != nullptr) {
			instances_[window_] = this;
		}
		other.window_       = nullptr;
		other.scroll_delta_ = {};
	}
	return *this;
}

void Window::release() noexcept {
	// Drop the mapping and callbacks before destroying the handle so any event
	// delivered during teardown cannot reach this wrapper.
	if (window_ == nullptr) {
		return;
	}
	instances_.erase(window_);
	glfwSetKeyCallback(window_, nullptr);
	glfwSetScrollCallback(window_, nullptr);
	glfwDestroyWindow(window_);
	window_ = nullptr;
}

int Window::width() const noexcept {
	int width  = 0;
	int height = 0;
	if (window_ != nullptr) {
		glfwGetWindowSize(window_, &width, &height);
	}
	return width;
}

int Window::height() const noexcept {
	int width  = 0;
	int height = 0;
	if (window_ != nullptr) {
		glfwGetWindowSize(window_, &width, &height);
	}
	return height;
}

int Window::framebufferWidth() const noexcept {
	int width  = 0;
	int height = 0;
	if (window_ != nullptr) {
		glfwGetFramebufferSize(window_, &width, &height);
	}
	return width;
}

int Window::framebufferHeight() const noexcept {
	int width  = 0;
	int height = 0;
	if (window_ != nullptr) {
		glfwGetFramebufferSize(window_, &width, &height);
	}
	return height;
}

WindowPosition Window::position() const noexcept {
	if (window_ != nullptr && glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) {
		glfwGetWindowPos(window_, &position_.x, &position_.y);
	}
	return position_;
}

void Window::setPosition(WindowPosition position) noexcept {
	position_ = position;
	if (window_ != nullptr && glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) {
		glfwSetWindowPos(window_, position.x, position.y);
	}
}

bool Window::hasGlobalPosition() const noexcept {
	return window_ != nullptr && glfwGetPlatform() != GLFW_PLATFORM_WAYLAND;
}

bool Window::shouldClose() const noexcept {
	return window_ == nullptr || glfwWindowShouldClose(window_) == GLFW_TRUE;
}

bool Window::closeRequested() noexcept {
	return shouldClose() || keyPressed(GLFW_KEY_ESCAPE) || keyPressed(GLFW_KEY_Q);
}

bool Window::transparent() const noexcept {
	return window_ != nullptr &&
	       glfwGetWindowAttrib(window_, GLFW_TRANSPARENT_FRAMEBUFFER) == GLFW_TRUE;
}

void Window::pollEvents() const noexcept {
	glfwPollEvents();
}

double Window::time() const noexcept {
	return glfwGetTime();
}

bool Window::mouseButtonDown(int button) const noexcept {
	return window_ != nullptr && glfwGetMouseButton(window_, button) == GLFW_PRESS;
}

MousePosition Window::cursorPosition() const noexcept {
	MousePosition position;
	if (window_ != nullptr) {
		glfwGetCursorPos(window_, &position.x, &position.y);
	}
	return position;
}

MousePosition Window::consumeScrollDelta() noexcept {
	const MousePosition delta = scroll_delta_;
	scroll_delta_             = {};
	return delta;
}

bool Window::keyPressed(int key) noexcept {
	if (window_ == nullptr) {
		return false;
	}

	const auto pressed = pressed_keys_.find(key);
	if (pressed == pressed_keys_.end()) {
		return false;
	}
	pressed_keys_.erase(pressed);
	return true;
}

bool Window::keyReleased(int key) noexcept {
	if (window_ == nullptr) {
		return false;
	}

	const auto released = released_keys_.find(key);
	if (released == released_keys_.end()) {
		return false;
	}
	released_keys_.erase(released);
	return true;
}

bool Window::keyDown(int key) const noexcept {
	return window_ != nullptr && glfwGetKey(window_, key) == GLFW_PRESS;
}

void Window::keyCallback(GLFWwindow *window, int key, int, int action, int) {
	const auto instance = instances_.find(window);
	if (instance != instances_.end()) {
		instance->second->handleKeyEvent(key, action);
	}
}

void Window::scrollCallback(GLFWwindow *window, double x_offset, double y_offset) {
	const auto instance = instances_.find(window);
	if (instance != instances_.end()) {
		instance->second->handleScrollEvent(x_offset, y_offset);
	}
}

void Window::handleKeyEvent(int key, int action) noexcept {
	if (action == GLFW_PRESS) {
		pressed_keys_.insert(key);
	} else if (action == GLFW_RELEASE) {
		released_keys_.insert(key);
	}
}

void Window::handleScrollEvent(double x_offset, double y_offset) noexcept {
	scroll_delta_.x += x_offset;
	scroll_delta_.y += y_offset;
}

} // namespace oxide
