#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <algorithm>

namespace poly_paint
{
    // Coordinates are normalized to the target canvas: (0, 0) is its top-left
    // corner and (1, 1) is its bottom-right corner. Rasterization clips any
    // vertices outside that range, which lets mutation freely change a shape's
    // size and position.
    struct PolygonPoint
    {
        float x {};
        float y {};
    };

    struct RgbaColor
    {
        std::uint8_t r {};
        std::uint8_t g {};
        std::uint8_t b {};
        std::uint8_t a {std::numeric_limits<std::uint8_t>::max()};
    };

    // A polygon has any number of vertices (at least three) and exactly one
    // flat RGBA color. The vertices need not form a convex shape.
    class Polygon
    {
    public:
        // Evolution currently creates 3-7 vertices; one inline spare keeps the
        // genome allocation-free without inflating every one of its 50 polygons.
        static constexpr std::size_t max_vertices = 8;
        using VertexStorage = std::array<PolygonPoint, max_vertices>;

        Polygon() = default;

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

        [[nodiscard]] std::span<const PolygonPoint> vertices() const noexcept
        {
            return {m_vertices.data(), m_vertex_count};
        }

        [[nodiscard]] std::size_t vertex_count() const noexcept
        {
            return m_vertex_count;
        }

        [[nodiscard]] const PolygonPoint& vertex_at(std::size_t index) const
        {
            if (index >= m_vertex_count)
            {
                throw std::out_of_range("Polygon vertex index is outside the polygon.");
            }
            return m_vertices[index];
        }

        [[nodiscard]] const RgbaColor& color() const noexcept
        {
            return m_color;
        }

        void set_vertex(std::size_t index, PolygonPoint vertex)
        {
            if (index >= m_vertex_count)
            {
                throw std::out_of_range("Polygon vertex index is outside the polygon.");
            }
            m_vertices[index] = vertex;
        }

        void set_color(RgbaColor color) noexcept
        {
            m_color = color;
        }

    private:
        VertexStorage m_vertices {};
        std::size_t m_vertex_count {3};
        RgbaColor m_color;
    };

    // This is the evolution-strategy individual for image approximation. A
    // candidate may use fewer than 50 polygons while it is being constructed,
    // but it can never exceed the rendering budget.
    class PolygonCollection
    {
    public:
        static constexpr std::size_t default_polygon_count = 50;
        static constexpr std::size_t max_polygon_capacity = 1'000;

        explicit PolygonCollection(std::size_t polygon_limit = default_polygon_count)
            : m_polygon_limit(polygon_limit)
        {
            if (polygon_limit == 0 || polygon_limit > max_polygon_capacity)
            {
                throw std::invalid_argument("Polygon collection size must be between one and 1,000.");
            }
        }

        PolygonCollection(const PolygonCollection& other)
            : m_polygon_limit(other.m_polygon_limit)
            , m_size(other.m_size)
        {
            std::copy_n(other.m_polygons.begin(), m_size, m_polygons.begin());
        }

        PolygonCollection& operator=(const PolygonCollection& other)
        {
            if (this != &other)
            {
                m_polygon_limit = other.m_polygon_limit;
                m_size = other.m_size;
                std::copy_n(other.m_polygons.begin(), m_size, m_polygons.begin());
            }
            return *this;
        }

        [[nodiscard]] std::size_t size() const noexcept
        {
            return m_size;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return m_size == 0;
        }

        [[nodiscard]] bool full() const noexcept
        {
            return size() == m_polygon_limit;
        }

        [[nodiscard]] std::size_t polygon_limit() const noexcept { return m_polygon_limit; }

        [[nodiscard]] std::span<const Polygon> polygons() const noexcept
        {
            return {m_polygons.data(), m_size};
        }

        [[nodiscard]] const Polygon* begin() const noexcept { return m_polygons.data(); }
        [[nodiscard]] const Polygon* end() const noexcept { return m_polygons.data() + m_size; }

        [[nodiscard]] Polygon& polygon_at(std::size_t index)
        {
            if (index >= m_size)
            {
                throw std::out_of_range("Polygon index is outside the collection.");
            }
            return m_polygons[index];
        }

        [[nodiscard]] const Polygon& polygon_at(std::size_t index) const
        {
            if (index >= m_size)
            {
                throw std::out_of_range("Polygon index is outside the collection.");
            }
            return m_polygons[index];
        }

        bool add(Polygon polygon)
        {
            if (full())
            {
                return false;
            }

            m_polygons[m_size++] = polygon;
            return true;
        }

        void replace(std::size_t index, Polygon polygon)
        {
            polygon_at(index) = polygon;
        }

        void remove(std::size_t index)
        {
            if (index >= size())
            {
                throw std::out_of_range("Polygon index is outside the collection.");
            }
            std::move(
                m_polygons.data() + index + 1,
                m_polygons.data() + m_size,
                m_polygons.data() + index);
            --m_size;
        }

        void clear() noexcept
        {
            m_size = 0;
        }

    private:
        std::array<Polygon, max_polygon_capacity> m_polygons {};
        std::size_t m_polygon_limit {default_polygon_count};
        std::size_t m_size {};
    };
}
