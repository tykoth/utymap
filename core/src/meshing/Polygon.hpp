#ifndef MESHING_POLYGON_HPP_DEFINED
#define MESHING_POLYGON_HPP_DEFINED

#include "meshing/MeshTypes.hpp"

#include <stdexcept>

namespace utymap { namespace meshing {

// Represents polygon in 2D space.
// NOTE this class is designed to work with Triangle library
class Polygon
{
public:
    typedef std::pair<std::size_t, std::size_t> Range;

    std::vector<double> points;
    std::vector<double> holes;
    std::vector<int> segments;

    // defines outer shape
    std::vector<Range> outers;
    // defines inner shape (holes)
    std::vector<Range> inners;

    Rectangle rectangle;

    Polygon(size_t numberOfPoints, size_t numberOfHoles = 1)
    {
        points.reserve(numberOfPoints * 2);
        holes.reserve(numberOfHoles * 2);
        segments.reserve(numberOfPoints * 2);
    }

    void addContour(const std::vector<Vector2>& contour)
    { 
        addContour(contour, false); 
    }

    void addHole(const std::vector<Vector2>& hole)
    {
        addContour(hole, true);
    }

private:

    void addContour(const std::vector<Vector2>& contour, bool isHole)
    {
        auto count = contour.size();
        if (contour[0] == contour[count - 1])
            count--;

        auto startIndex = points.size();
        auto endIndex = startIndex + count * 2;

        if (isHole) {
            Vector2 pointInside;
            if (!findPointInPolygon(contour, pointInside)) {
                // TODO log error
                return;
            }
            holes.push_back(pointInside.x);
            holes.push_back(pointInside.y);
            inners.push_back(std::make_pair(startIndex, endIndex));
        } else {
            outers.push_back(std::make_pair(startIndex, endIndex));
        }      

        auto offset = segments.size() / 2;
        for (std::size_t i = 0; i < count; ++i) {
            Vector2 point = contour[i];
            rectangle.expand(point);
            points.push_back(point.x);
            points.push_back(point.y);
            segments.push_back(static_cast<int>(offset + i));
            segments.push_back(static_cast<int>(offset + ((i + 1) % count)));
        }
    }

    // tries to find point in polygon.
    bool findPointInPolygon(const std::vector<Vector2>& contour, Vector2& point) const
    {
        Rectangle bounds;
        bounds.expand(contour);

        auto length = contour.size();
        int limit = 8;

        Vector2 a, b; // Current edge.
        double cx, cy; // Center of current edge.
        double dx, dy; // Direction perpendicular to edge.

        if (contour.size() == 3) {
            point = Vector2((contour[0].x + contour[1].x + contour[2].x) / 3,
                (contour[0].y + contour[1].y + contour[2].y) / 3);
            return true;
        }

        for (size_t i = 0; i < length; i++) {
            a = contour[i];
            b = contour[(i + 1) % length];

            cx = (a.x + b.x) / 2;
            cy = (a.y + b.y) / 2;

            dx = (b.y - a.y) / 1.374;
            dy = (a.x - b.x) / 1.374;

            for (int j = 1; j <= limit; j++) {
                // Search to the right of the segment.
                point.x = cx + dx / j;
                point.y = cy + dy / j;

                if (bounds.contains(point) && isPointInPolygon(point, contour))
                    return true;

                // Search on the other side of the segment.
                point.x = cx - dx / j;
                point.y = cy - dy / j;

                if (bounds.contains(point) && isPointInPolygon(point, contour))
                    return true;
            }
        }

        return false;
    }

    // checks whether point in polygon using ray casting algorithm
    bool isPointInPolygon(const Vector2& point, const std::vector<Vector2>& poly) const
    {
        bool inside = false;
        double x = point.x;
        double y = point.y;
        auto count = poly.size();

        for (std::size_t i = 0, j = count - 1; i < count; i++) {
            if (((poly[i].y < y && poly[j].y >= y) || (poly[j].y < y && poly[i].y >= y))
                && (poly[i].x <= x || poly[j].x <= x)) {
                inside ^= (poly[i].x + (y - poly[i].y) / (poly[j].y - poly[i].y) * (poly[j].x - poly[i].x) < x);
            }
            j = i;
        }

        return inside;
    }
};


}}

#endif // MESHING_POLYGON_HPP_DEFINED
