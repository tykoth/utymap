#include "meshing/clipper.hpp"
#include "terrain/LineGridSplitter.hpp"
#include "terrain/TerraBuilder.hpp"

#include <cmath>
#include <cstdint>
#include <iterator>

using namespace utymap::meshing;
using namespace utymap::terrain;
using namespace ClipperLib;

const uint64_t Scale = 1E8;
const uint64_t DoubleScale = Scale * Scale;

typedef std::vector<MeshRegion> MeshRegions;
typedef std::unordered_map<int, MeshRegions> RoadMap;
typedef std::map<int, MeshRegions> SurfaceMap;

struct TerraContext
{
    Path clipRect;
    Paths water;
    Paths carRoads;
    Paths walkRoads;
    Paths surfaces;
    Paths background;

    LineGridSplitter<double> splitter;

    Mesh<double> mesh;

    TerraContext(int gridCellSize, int roundDigitCount) 
        : splitter(gridCellSize, roundDigitCount)
    {
    }
};

Path createPathFromRect(const Rectangle<double>& tileRect) 
{
    Path rect;
    rect.push_back(IntPoint(tileRect.xMin*Scale, tileRect.yMin*Scale));
    rect.push_back(IntPoint(tileRect.xMin*Scale, tileRect.yMin*Scale));
    rect.push_back(IntPoint(tileRect.xMin*Scale, tileRect.yMin*Scale));
    rect.push_back(IntPoint(tileRect.xMin*Scale, tileRect.yMin*Scale));

    return std::move(rect);
}

Paths clipByRect(Clipper& clipper, const Path& clipRect, const Paths& subjects)
{
    Paths solution;
    clipper.AddPaths(subjects, ptSubject, true);
    clipper.AddPath(clipRect, ptClip, true);
    clipper.Execute(ctIntersection, solution);
    clipper.Clear();
    return std::move(solution);
}

Paths buildPaths(const MeshRegions& regions) {
    // TODO holes are not processed
    Paths paths(regions.size());
    for (const MeshRegion& region : regions) {
        Path p(region.points.size());
        for (const Point<double> point : region.points) {
            p.push_back(IntPoint(point.x*Scale, point.y*Scale));
        }
        paths.push_back(p);
    }
    return std::move(paths);
}

Paths buildOffsetSolution(Clipper& clipper, ClipperOffset& offset, const RoadMap& roads)
{
    for (auto r : roads) {
        Paths offsetSolution;
        offset.AddPaths(buildPaths(r.second), jtMiter, etOpenSquare);
        offset.Execute(offsetSolution, r.first);
        clipper.AddPaths(offsetSolution, ptSubject, true);
        offset.Clear();
    }
    Paths polySolution;
    clipper.Execute(ctUnion, polySolution, pftPositive, pftPositive);
    clipper.Clear();
    return std::move(polySolution);
}

Paths clipRoads(Clipper& clipper, TerraContext& context, const Paths& roads)
{
    Paths resultRoads;
    clipper.AddPaths(context.water, ptClip, true);
    clipper.AddPaths(roads, ptSubject, true);
    clipper.Execute(ctDifference, resultRoads, pftPositive, pftPositive);
    clipper.Clear();
    return std::move(clipByRect(clipper, context.clipRect, resultRoads));
}

void populateMesh(Mesh<double>& mesh, const MeshRegion& region, const Paths& paths)
{
    for (Path path : paths) {
        double area = ClipperLib::Area(path);
        // skip small polygons to prevent triangulation issues
        if (std::abs(area / DoubleScale) < 0.001) continue;

        // TODO
    }
}

/*std::vector<Point<double>> restorePoints(Path path)
{
    // TODO
}*/


// build water layer
void buildWater(Clipper& clipper, TerraContext& context, const MeshRegions& regions)
{
    for (const MeshRegion& region : regions) {
        Path p(region.points.size());
        for (const Point<double> point : region.points) {
            p.push_back(IntPoint(point.x*Scale, point.y*Scale));
        }
        clipper.AddPath(p, ptSubject, true);
    }

    Paths solution;
    clipper.Execute(ctUnion, solution);
    clipper.Clear();
    context.water = std::move(clipByRect(clipper, context.clipRect, solution));
}

// build road layer
void buildRoads(Clipper& clipper, TerraContext& context, const RoadMap& carMap, const RoadMap& walkMap)
{
    ClipperOffset offset;
    Paths carRoadPaths = buildOffsetSolution(clipper, offset, carMap);
    Paths walkRoadsPaths = buildOffsetSolution(clipper, offset, walkMap);

    clipper.AddPaths(carRoadPaths, ptClip, true);
    clipper.AddPaths(walkRoadsPaths, ptSubject, true);
    Paths extrudedWalkRoads;
    clipper.Execute(ctDifference, extrudedWalkRoads);
    clipper.Clear();

    context.carRoads = std::move(clipRoads(clipper, context, carRoadPaths));
    context.walkRoads = std::move(clipRoads(clipper, context, extrudedWalkRoads));
}

// build surfaces layer
void buildSurfaces(Clipper& clipper, TerraContext& context, const SurfaceMap& surfaces)
{
    for (auto surfacePair : surfaces) {
        Paths paths = buildPaths(surfacePair.second);
        clipper.AddPaths(paths, ptSubject, true);
        Paths surfacesUnion;
        clipper.Execute(ctUnion, surfacesUnion);
        clipper.Clear();

        clipper.AddPaths(context.carRoads, ptClip, true);
        clipper.AddPaths(context.walkRoads, ptClip, true);
        clipper.AddPaths(context.water, ptClip, true);
        clipper.AddPaths(context.surfaces, ptClip, true);
        clipper.AddPaths(surfacesUnion, ptSubject, true);
        Paths result;
        clipper.Execute(ctDifference, result, pftPositive, pftPositive);
        clipper.Clear();
        result = clipByRect(clipper, context.clipRect, result);

        // TODO process surfaces as context is present only here

        context.surfaces.insert(
            context.surfaces.end(),
            std::make_move_iterator(result.begin()),
            std::make_move_iterator(result.end())
            );
    }
}

// build background
void buildBackground(Clipper& clipper, TerraContext& context)
{
    clipper.AddPath(context.clipRect, ptSubject, true);

    clipper.AddPaths(context.carRoads, ptClip, true);
    clipper.AddPaths(context.walkRoads, ptClip, true);
    clipper.AddPaths(context.water, ptClip, true);
    clipper.AddPaths(context.surfaces, ptClip, true);
    clipper.Execute(ctDifference, context.background, pftPositive, pftPositive);
    clipper.Clear();
}

Mesh<double> TerraBuilder::build(const Rectangle<double>& tileRect)
{
    Clipper clipper;
    TerraContext context(1, 8);
    context.clipRect = createPathFromRect(tileRect);

    // fill context with layer specific data.
    buildWater(clipper, context, waters_);
    buildRoads(clipper, context, carRoads_, walkRoads_);
    buildSurfaces(clipper, context, surfaces_);
    buildBackground(clipper, context);

    return std::move(context.mesh);
}