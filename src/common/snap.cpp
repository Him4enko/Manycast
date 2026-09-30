#include "common/snap.hpp"

#include <cmath>

namespace manycast {

namespace {

float nearestOffset(const float edges[3], const float targets[3], float threshold, bool *snapped, float *guide)
{
	float bestDistance = threshold;
	float bestOffset = 0.0f;
	float bestGuide = 0.0f;
	bool found = false;

	for (int target = 0; target < 3; target++) {
		for (int edge = 0; edge < 3; edge++) {
			const float distance = std::fabs(edges[edge] - targets[target]);
			if (distance < bestDistance) {
				bestDistance = distance;
				bestOffset = targets[target] - edges[edge];
				bestGuide = targets[target];
				found = true;
			}
		}
	}

	*snapped = found;
	*guide = bestGuide;
	return bestOffset;
}

} // namespace

SnapResult computeSnap(const SnapBox &box, float canvasWidth, float canvasHeight, float threshold)
{
	SnapResult result;

	if (!box.valid || canvasWidth <= 0.0f || canvasHeight <= 0.0f || threshold <= 0.0f)
		return result;

	const float verticalTargets[3] = {0.0f, canvasWidth / 2.0f, canvasWidth};
	const float verticalEdges[3] = {box.x, box.x + box.width / 2.0f, box.x + box.width};
	const float horizontalTargets[3] = {0.0f, canvasHeight / 2.0f, canvasHeight};
	const float horizontalEdges[3] = {box.y, box.y + box.height / 2.0f, box.y + box.height};

	result.offsetX = nearestOffset(verticalEdges, verticalTargets, threshold, &result.snappedX, &result.guideX);
	result.offsetY = nearestOffset(horizontalEdges, horizontalTargets, threshold, &result.snappedY, &result.guideY);
	return result;
}

} // namespace manycast
