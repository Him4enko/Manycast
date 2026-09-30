#pragma once

#include <obs-module.h>

#include <exception>
#include <utility>

namespace manycast {

/*
 * Runs a callback invoked from libobs or Qt (module load, output signals, dock
 * timers, display draw callbacks, ...). An exception must never escape into
 * those frameworks: it would unwind through C code or the Qt event loop and
 * crash OBS without any useful message.
 */
template<typename Func> void guard(const char *what, Func &&func) noexcept
{
	try {
		std::forward<Func>(func)();
	} catch (const std::exception &error) {
		blog(LOG_ERROR, "[manycast] %s: %s", what, error.what());
	} catch (...) {
		blog(LOG_ERROR, "[manycast] %s: unknown exception", what);
	}
}

/* Wraps a callback into a guarded one, e.g. for Qt signal connections. */
template<typename Func> auto guarded(const char *what, Func &&func)
{
	return [what, func = std::forward<Func>(func)]() mutable {
		guard(what, func);
	};
}

} // namespace manycast
