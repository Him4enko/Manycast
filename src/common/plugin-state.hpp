#pragma once

namespace manycast {

/*
 * Set once OBS starts unloading modules. Everything that touches libobs after
 * that point (late widget destructors, pending timers) must keep its hands off
 * it, otherwise OBS crashes while shutting down.
 */
bool isShuttingDown();
void setShuttingDown(bool shuttingDown);

} // namespace manycast
