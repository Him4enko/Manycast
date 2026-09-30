#pragma once

namespace manycast {

/* canvas pixels */
struct SnapBox {
	bool valid = false;
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

struct SnapResult {
	float offsetX = 0.0f;
	float offsetY = 0.0f;
	bool snappedX = false;
	bool snappedY = false;
	/* only valid when snapped */
	float guideX = 0.0f;
	float guideY = 0.0f;
};

/* the closest of the item's edges within `threshold` wins; at most one line per
 * axis is applied, otherwise the item jumps by several offsets at once */
SnapResult computeSnap(const SnapBox &box, float canvasWidth, float canvasHeight, float threshold);

} // namespace manycast
