#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace oxide {

struct WindowConfig {
	int         width       = 500;
	int         height      = 500;
	std::string title       = "Oxide";
	bool        resizable   = true;
	bool        transparent = false;
	bool        decorated   = true;
	bool        floating    = false;
};

struct MousePosition {
	double x = 0.0;
	double y = 0.0;
};

struct WindowPosition {
	int x = 0;
	int y = 0;
};

class GLFWContext {
  public:
	explicit GLFWContext();
	~GLFWContext();

	GLFWContext(const GLFWContext &)            = delete;
	GLFWContext &operator=(const GLFWContext &) = delete;
};

class Window {
  public:
	explicit Window(const WindowConfig &config);
	~Window();

	Window(const Window &)            = delete;
	Window &operator=(const Window &) = delete;

	Window(Window &&) noexcept;
	Window &operator=(Window &&) noexcept;

	[[nodiscard]] GLFWwindow   *handle() const noexcept { return window_; }

	// Content size in screen coordinates. This is the same coordinate space as
	// cursorPosition(), so cursor positions can be normalized by these values.
	[[nodiscard]] int width() const noexcept;
	[[nodiscard]] int height() const noexcept;

	// Drawable size in pixels. Differs from width()/height() on displays with a
	// content scale other than 1.0 (for example HiDPI screens).
	[[nodiscard]] int framebufferWidth() const noexcept;
	[[nodiscard]] int framebufferHeight() const noexcept;
	// Wayland compositors do not expose global window coordinates; on Wayland
	// this returns the last requested position instead.
	[[nodiscard]] WindowPosition position() const noexcept;
	void                       setPosition(WindowPosition position) noexcept;
	[[nodiscard]] bool         hasGlobalPosition() const noexcept;

	[[nodiscard]] bool          shouldClose() const noexcept;
	[[nodiscard]] bool          closeRequested() noexcept;
	[[nodiscard]] bool          transparent() const noexcept;
	void                        pollEvents() const noexcept;
	[[nodiscard]] double        time() const noexcept;
	// Edge-triggered: true once per press (or release) since the last call to
	// the matching function, and the event is consumed by that call.
	[[nodiscard]] bool          keyPressed(int key) noexcept;
	[[nodiscard]] bool          keyReleased(int key) noexcept;
	// Level-triggered: true while the key is currently held down.
	[[nodiscard]] bool          keyDown(int key) const noexcept;
	[[nodiscard]] bool          mouseButtonDown(int button) const noexcept;

	// Cursor position relative to the content area, in screen coordinates.
	[[nodiscard]] MousePosition cursorPosition() const noexcept;
	[[nodiscard]] MousePosition consumeScrollDelta() noexcept;

  private:
	static void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods);
	static void scrollCallback(GLFWwindow *window, double x_offset, double y_offset);
	void        handleKeyEvent(int key, int action) noexcept;
	void        handleScrollEvent(double x_offset, double y_offset) noexcept;
	void        release() noexcept;

	GLFWwindow             *window_ = nullptr;
	std::unordered_set<int> pressed_keys_;
	std::unordered_set<int> released_keys_;
	MousePosition           scroll_delta_;
	mutable WindowPosition  position_;

	static std::unordered_map<GLFWwindow *, Window *> instances_;
};

} // namespace oxide
