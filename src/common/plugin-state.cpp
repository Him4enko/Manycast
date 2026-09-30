#include "common/plugin-state.hpp"

#include <atomic>

namespace manycast {

namespace {

std::atomic<bool> g_shuttingDown{false};

} // namespace

bool isShuttingDown()
{
	return g_shuttingDown.load();
}

void setShuttingDown(bool shuttingDown)
{
	g_shuttingDown.store(shuttingDown);
}

} // namespace manycast
