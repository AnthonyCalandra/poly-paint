#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace poly_paint
{
    /** @brief A vertex with coordinates normalized to the target canvas.
     *  @details (0, 0) is the top-left and (1, 1) the bottom-right. Rasterization
     *  clips out-of-range vertices so mutation can freely adjust shape bounds.
     */
    struct PolygonPoint
    {
        /** @brief Normalized horizontal coordinate. */
        float x {};
        /** @brief Normalized vertical coordinate. */
        float y {};
    };

    /** @brief An eight-bit red, green, blue, and alpha color. */
    struct RgbaColor
    {
        /** @brief Red channel. */
        std::uint8_t r {};
        /** @brief Green channel. */
        std::uint8_t g {};
        /** @brief Blue channel. */
        std::uint8_t b {};
        /** @brief Opacity channel; defaults to fully opaque. */
        std::uint8_t a {std::numeric_limits<std::uint8_t>::max()};
    };

    /** @brief A flat-colored polygon with three to eight vertices.
     *  @details Vertices need not define a convex shape.
     */
    class Polygon
    {
    public:
        /** @brief Maximum number of vertices stored without a separate allocation. */
        static constexpr std::size_t max_vertices = 8;
        /** @brief Fixed-capacity storage for polygon vertices. */
        using VertexStorage = std::array<PolygonPoint, max_vertices>;

        /** @brief Creates the default three-vertex black polygon. */
        Polygon() = default;

        /** @brief Creates a polygon from three to eight @p vertices and @p color.
         *  @throws std::invalid_argument if the vertex count is outside that range.
         */
        Polygon(std::span<const PolygonPoint> vertices, RgbaColor color)
            : m_vertex_count(vertices.size())
            , m_color(color)
        {
            if (vertices.size() < 3 || vertices.size() > max_vertices)
            {
                throw std::invalid_argument("A polygon requires between three and eight vertices.");
            }
            std::copy(vertices.begin(), vertices.end(), m_vertices.begin());
        }

        /** @brief Returns the active vertices in order. */
        [[nodiscard]] std::span<const PolygonPoint> vertices() const noexcept
        {
            return {m_vertices.data(), m_vertex_count};
        }

        /** @brief Returns the active vertex count. */
        [[nodiscard]] std::size_t vertex_count() const noexcept
        {
            return m_vertex_count;
        }

        /** @brief Returns the vertex at @p index.
         *  @throws std::out_of_range if @p index is not an active vertex.
         */
        [[nodiscard]] const PolygonPoint& vertex_at(std::size_t index) const
        {
            if (index >= m_vertex_count)
            {
                throw std::out_of_range("Polygon vertex index is outside the polygon.");
            }
            return m_vertices[index];
        }

        /** @brief Returns the polygon's fill color. */
        [[nodiscard]] const RgbaColor& color() const noexcept
        {
            return m_color;
        }

        /** @brief Replaces one active vertex.
         *  @throws std::out_of_range if @p index is not an active vertex.
         */
        void set_vertex(std::size_t index, PolygonPoint vertex)
        {
            if (index >= m_vertex_count)
            {
                throw std::out_of_range("Polygon vertex index is outside the polygon.");
            }
            m_vertices[index] = vertex;
        }

        /** @brief Replaces the polygon's fill color. */
        void set_color(RgbaColor color) noexcept
        {
            m_color = color;
        }

    private:
        VertexStorage m_vertices {};
        std::size_t m_vertex_count {3};
        RgbaColor m_color;
    };

    /** @brief A bounded collection of polygons used as an evolution individual. */
    class PolygonCollection
    {
    public:
        /** @brief Default polygon budget used by the application. */
        static constexpr std::size_t default_polygon_count = 50;
        /** @brief Largest polygon budget supported by the renderer. */
        static constexpr std::size_t max_polygon_capacity = 1'000;
        /** @brief Iterator type for read-only polygon traversal. */
        using const_iterator = std::vector<Polygon>::const_iterator;

        /** @brief Creates an empty collection with a fixed @p polygon_limit.
         *  @throws std::invalid_argument if the limit is zero or too large.
         */
        explicit PolygonCollection(std::size_t polygon_limit = default_polygon_count)
            : m_polygon_limit(polygon_limit)
        {
            if (polygon_limit == 0 || polygon_limit > max_polygon_capacity)
            {
                throw std::invalid_argument("Polygon collection size must be between one and 1,000.");
            }
            m_polygons.reserve(m_polygon_limit);
        }

        /** @brief Returns the number of stored polygons. */
        [[nodiscard]] std::size_t size() const noexcept
        {
            return m_polygons.size();
        }

        /** @brief Returns whether the collection has no polygons. */
        [[nodiscard]] bool empty() const noexcept
        {
            return m_polygons.empty();
        }

        /** @brief Returns whether the collection reached its polygon limit. */
        [[nodiscard]] bool full() const noexcept
        {
            return size() == m_polygon_limit;
        }

        /** @brief Returns the maximum number of polygons this collection accepts. */
        [[nodiscard]] std::size_t polygon_limit() const noexcept { return m_polygon_limit; }

        /** @brief Returns all stored polygons as a read-only contiguous view. */
        [[nodiscard]] std::span<const Polygon> polygons() const noexcept
        {
            return m_polygons;
        }

        /** @brief Returns an iterator to the first polygon. */
        [[nodiscard]] const_iterator begin() const noexcept { return m_polygons.cbegin(); }
        /** @brief Returns an iterator one past the last polygon. */
        [[nodiscard]] const_iterator end() const noexcept { return m_polygons.cend(); }

        /** @brief Returns a mutable polygon at @p index.
         *  @throws std::out_of_range if @p index is outside the collection.
         */
        [[nodiscard]] Polygon& polygon_at(std::size_t index)
        {
            if (index >= size())
            {
                throw std::out_of_range("Polygon index is outside the collection.");
            }
            return m_polygons[index];
        }

        /** @brief Returns a read-only polygon at @p index.
         *  @throws std::out_of_range if @p index is outside the collection.
         */
        [[nodiscard]] const Polygon& polygon_at(std::size_t index) const
        {
            if (index >= size())
            {
                throw std::out_of_range("Polygon index is outside the collection.");
            }
            return m_polygons[index];
        }

        /** @brief Adds @p polygon and returns false if the collection is full. */
        bool add(Polygon polygon)
        {
            if (full())
            {
                return false;
            }

            m_polygons.push_back(std::move(polygon));
            return true;
        }

        /** @brief Replaces the polygon at @p index.
         *  @throws std::out_of_range if @p index is outside the collection.
         */
        void replace(std::size_t index, Polygon polygon)
        {
            polygon_at(index) = std::move(polygon);
        }

        /** @brief Removes the polygon at @p index.
         *  @throws std::out_of_range if @p index is outside the collection.
         */
        void remove(std::size_t index)
        {
            if (index >= size())
            {
                throw std::out_of_range("Polygon index is outside the collection.");
            }
            m_polygons.erase(m_polygons.begin() + static_cast<std::ptrdiff_t>(index));
        }

        /** @brief Removes every stored polygon while retaining capacity. */
        void clear() noexcept
        {
            m_polygons.clear();
        }

    private:
        std::size_t m_polygon_limit {default_polygon_count};
        std::vector<Polygon> m_polygons;
    };
}
