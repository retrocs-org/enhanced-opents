// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 OpenTS contributors

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <SDL3/SDL.h>


void * Mac_Start_Precise_Scroll(SDL_Window * window, Uint32 event_type)
{
	NSWindow * native = static_cast<NSWindow *>(SDL_GetPointerProperty(
		SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr));
	if (native == nil || event_type == 0) return(nullptr);

	return([NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel handler:^NSEvent *(NSEvent * event) {
		if (event.window != native || !event.hasPreciseScrollingDeltas || !native.isKeyWindow) return(event);

		NSView * view = native.contentView;
		NSPoint const position = [view convertPoint:event.locationInWindow fromView:nil];
		if (!NSPointInRect(position, view.bounds)) return(event);

		// SDL normalizes Cocoa's scroll units but drops the precise-device flag.
		SDL_Event scroll{};
		scroll.wheel.type = static_cast<SDL_EventType>(event_type);
		scroll.wheel.windowID = SDL_GetWindowID(window);
		scroll.wheel.x = static_cast<float>(-event.scrollingDeltaX * 0.1);
		scroll.wheel.y = static_cast<float>(event.scrollingDeltaY * 0.1);
		scroll.wheel.direction = event.isDirectionInvertedFromDevice ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
		scroll.wheel.mouse_x = static_cast<float>(position.x - NSMinX(view.bounds));
		scroll.wheel.mouse_y = static_cast<float>(view.isFlipped ? position.y - NSMinY(view.bounds) : NSMaxY(view.bounds) - position.y);
		return(SDL_PushEvent(&scroll) ? nil : event);
	}]);
}


int Mac_Window_Refresh_Rate(SDL_Window * window, int fallback)
{
	NSWindow * native = static_cast<NSWindow *>(SDL_GetPointerProperty(
		SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr));
	NSScreen * screen = native.screen;
	NSNumber * number = [screen.deviceDescription objectForKey:@"NSScreenNumber"];
	if (number == nil) return(fallback);

	CGDisplayModeRef mode = CGDisplayCopyDisplayMode(number.unsignedIntValue);
	if (mode == nullptr) return(fallback);
	double const rate = CGDisplayModeGetRefreshRate(mode);
	CGDisplayModeRelease(mode);
	if (rate > 1.0) return(static_cast<int>(rate + 0.5));
	if (@available(macOS 12.0, *)) {
		int const adaptive = static_cast<int>(screen.maximumFramesPerSecond);
		if (adaptive > 1) return(adaptive);
	}
	return(fallback);
}


void Mac_Stop_Precise_Scroll(void * monitor)
{
	if (monitor != nullptr) [NSEvent removeMonitor:static_cast<id>(monitor)];
}
