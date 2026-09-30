/*
 * Unit tests for the parts of the plugin that do not depend on OBS.
 *
 * Build and run:
 *   cmake --build --preset windows-x64 --config RelWithDebInfo --target manycast-tests
 *   build_x64\tests\RelWithDebInfo\manycast-tests.exe
 *
 * Uses doctest (https://github.com/doctest/doctest), vendored in tests/doctest.
 *
 * What is intentionally NOT tested here: everything that talks to the OBS C API
 * (docks, outputs, encoders, canvases) - libobs cannot run headless and mocking
 * it would test the mock. For that part there is tests/smoke-plugin.ps1 (the
 * built dll loads and exports the module API) plus the [manycast] log lines.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "common/format.hpp"
#include "common/snap.hpp"
#include "common/stats.hpp"
#include "core/destination.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <unordered_set>

using namespace manycast;

TEST_CASE("maskStreamKey keeps only the last four characters")
{
	CHECK(maskStreamKey("") == "");
	CHECK(maskStreamKey("a") == "*");
	CHECK(maskStreamKey("abc") == "***");
	CHECK(maskStreamKey("abcd") == "****");
	CHECK(maskStreamKey("abcde") == "****bcde");
	CHECK(maskStreamKey("1234-5678-90ab-cdef") == "****cdef");
}

TEST_CASE("formatDuration prints HH:MM:SS without wrapping hours")
{
	CHECK(formatDuration(0.0) == "00:00:00");
	CHECK(formatDuration(0.9) == "00:00:00");
	CHECK(formatDuration(59.9) == "00:00:59");
	CHECK(formatDuration(60.0) == "00:01:00");
	CHECK(formatDuration(3599.0) == "00:59:59");
	CHECK(formatDuration(3600.0) == "01:00:00");
	CHECK(formatDuration(3661.5) == "01:01:01");
	CHECK(formatDuration(360000.0) == "100:00:00");
	CHECK(formatDuration(-5.0) == "00:00:00");
}

TEST_CASE("display numbers are clamped and rounded")
{
	/* the UI must never show a negative or nonsensical value */
	CHECK(bitrateKbps(-100.0) == 0);
	CHECK(framesPerSecond(-1.0) == 0);
	CHECK(bitrateMbps(-1.0) == doctest::Approx(0.0));

	CHECK(bitrateKbps(5980.4) == 5980);
	CHECK(framesPerSecond(59.6) == 60);
	CHECK(bitrateMbps(5980.0) == doctest::Approx(5.98));
}

TEST_CASE("computeRates derives bitrate and fps from two counter samples")
{
	SUBCASE("a non positive interval produces zeroes instead of infinities")
	{
		const Rates rates = computeRates({0, 0}, {1000, 60}, 0.0);
		CHECK(rates.bitrateKbps == doctest::Approx(0.0));
		CHECK(rates.fps == doctest::Approx(0.0));
	}

	SUBCASE("1000 bytes and 60 frames over one second")
	{
		const Rates rates = computeRates({0, 0}, {1000, 60}, 1.0);
		CHECK(rates.bitrateKbps == doctest::Approx(8.0));
		CHECK(rates.fps == doctest::Approx(60.0));
	}

	SUBCASE("the same amount over half a second is twice the rate")
	{
		const Rates rates = computeRates({0, 0}, {1000, 60}, 0.5);
		CHECK(rates.bitrateKbps == doctest::Approx(16.0));
		CHECK(rates.fps == doctest::Approx(120.0));
	}

	SUBCASE("only the delta between the samples counts")
	{
		const Rates rates = computeRates({5000, 300}, {6000, 360}, 1.0);
		CHECK(rates.bitrateKbps == doctest::Approx(8.0));
		CHECK(rates.fps == doctest::Approx(60.0));
	}

	SUBCASE("counters that went backwards (output restarted) must not go negative")
	{
		const Rates rates = computeRates({10000, 600}, {100, 10}, 1.0);
		CHECK(rates.bitrateKbps == doctest::Approx(0.0));
		CHECK(rates.fps == doctest::Approx(0.0));
	}

	SUBCASE("an idle output reports zero")
	{
		const Rates rates = computeRates({5000, 300}, {5000, 300}, 1.0);
		CHECK(rates.bitrateKbps == doctest::Approx(0.0));
		CHECK(rates.fps == doctest::Approx(0.0));
	}
}

TEST_CASE("computeSnap picks the nearest canvas line per axis")
{
	const SnapBox invalid{};
	CHECK_FALSE(computeSnap(invalid, 1080.0f, 1920.0f, 10.0f).snappedX);

	SUBCASE("a box already on the corner is not moved")
	{
		const SnapBox corner{true, 0.0f, 0.0f, 100.0f, 100.0f};
		const SnapResult snap = computeSnap(corner, 1080.0f, 1920.0f, 10.0f);
		CHECK(snap.snappedX);
		CHECK(snap.snappedY);
		CHECK(snap.offsetX == doctest::Approx(0.0));
		CHECK(snap.offsetY == doctest::Approx(0.0));
		CHECK(snap.guideX == doctest::Approx(0.0));
	}

	SUBCASE("a few pixels off the corner is pulled onto it")
	{
		const SnapBox box{true, 5.0f, 7.0f, 100.0f, 100.0f};
		const SnapResult snap = computeSnap(box, 1080.0f, 1920.0f, 10.0f);
		CHECK(snap.snappedX);
		CHECK(snap.snappedY);
		CHECK(snap.offsetX == doctest::Approx(-5.0));
		CHECK(snap.offsetY == doctest::Approx(-7.0));
	}

	SUBCASE("the box centre lands on the canvas centre and the guide is reported")
	{
		const SnapBox box{true, 494.0f, 914.0f, 100.0f, 100.0f};
		const SnapResult snap = computeSnap(box, 1080.0f, 1920.0f, 10.0f);
		CHECK(snap.snappedX);
		CHECK(snap.snappedY);
		CHECK(snap.offsetX == doctest::Approx(-4.0));
		CHECK(snap.offsetY == doctest::Approx(-4.0));
		CHECK(snap.guideX == doctest::Approx(540.0));
		CHECK(snap.guideY == doctest::Approx(960.0));
	}

	SUBCASE("the right and bottom edges are pulled onto the canvas border")
	{
		const SnapBox box{true, 977.0f, 1817.0f, 100.0f, 100.0f};
		const SnapResult snap = computeSnap(box, 1080.0f, 1920.0f, 10.0f);
		CHECK(snap.snappedX);
		CHECK(snap.snappedY);
		CHECK(snap.offsetX == doctest::Approx(3.0));
		CHECK(snap.offsetY == doctest::Approx(3.0));
		CHECK(snap.guideX == doctest::Approx(1080.0));
	}

	SUBCASE("far away from any line nothing is snapped")
	{
		const SnapBox box{true, 300.0f, 500.0f, 100.0f, 100.0f};
		const SnapResult snap = computeSnap(box, 1080.0f, 1920.0f, 10.0f);
		CHECK_FALSE(snap.snappedX);
		CHECK_FALSE(snap.snappedY);
	}

	SUBCASE("an item wider than the canvas can still be aligned to its border")
	{
		const SnapBox box{true, -520.0f, 0.0f, 1600.0f, 100.0f};
		const SnapResult snap = computeSnap(box, 1080.0f, 1920.0f, 10.0f);
		CHECK(snap.snappedX);
		CHECK(snap.offsetX == doctest::Approx(0.0));
		CHECK(snap.guideX == doctest::Approx(1080.0));
	}

	SUBCASE("only one line per axis is applied, otherwise the item jumps")
	{
		const SnapBox box{true, 4.0f, 0.0f, 20.0f, 10.0f};
		const SnapResult snap = computeSnap(box, 100.0f, 100.0f, 15.0f);
		CHECK(snap.snappedX);
		CHECK(snap.offsetX == doctest::Approx(-4.0));
	}

	SUBCASE("a zero threshold or a canvas without a size disables snapping")
	{
		const SnapBox box{true, 5.0f, 7.0f, 100.0f, 100.0f};
		CHECK_FALSE(computeSnap(box, 1080.0f, 1920.0f, 0.0f).snappedX);
		CHECK_FALSE(computeSnap(box, 0.0f, 0.0f, 10.0f).snappedX);
	}
}

TEST_CASE("a destination maps its mode to the OBS service type and to a label")
{
	Destination destination;
	destination.platform = "YouTube - RTMPS";

	CHECK(destination.serviceType() == "rtmp_common");
	CHECK(destination.targetLabel() == "YouTube - RTMPS");

	SUBCASE("an empty platform still gets a readable label")
	{
		destination.platform.clear();
		CHECK(destination.targetLabel() == "RTMP");
	}

	SUBCASE("a custom destination uses rtmp_custom and the url")
	{
		destination.serviceMode = ServiceMode::Custom;
		destination.serverUrl = "rtmp://example.com/live";
		CHECK(destination.serviceType() == "rtmp_custom");
		CHECK(destination.targetLabel() == "rtmp://example.com/live");
	}
}

TEST_CASE("makeId produces unique, name safe identifiers")
{
	/*
	 * This is a smoke check, not a proof: makeId mixes the clock with a counter,
	 * so collisions are practically impossible rather than impossible. The counter
	 * is a std::atomic, which makes the function safe to call from any thread, but
	 * the plugin only calls it from the UI thread - a real race detector (TSan)
	 * would be needed to test thread safety.
	 */
	const std::size_t count = 5000;
	std::unordered_set<std::string> ids;
	ids.reserve(count);
	for (std::size_t i = 0; i < count; i++)
		ids.insert(makeId());

	CHECK(ids.size() == count);

	/* identifiers appear in encoder/output names and in log lines, so they have
	 * to be printable ASCII without spaces - the length is not a contract */
	const bool allHex = std::all_of(ids.begin(), ids.end(), [](const std::string &id) {
		return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char character) {
			return std::isxdigit(character) != 0;
		});
	});
	CHECK(allHex);
}
