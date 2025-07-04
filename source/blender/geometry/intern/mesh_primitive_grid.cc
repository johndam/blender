/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BKE_attribute.hh"
#include "BKE_mesh.hh"

#include "GEO_mesh_primitive_grid.hh"

namespace blender::geometry {

static void calculate_uvs(Mesh *mesh,
                          const Span<float3> positions,
                          const Span<int> corner_verts,
                          const float size_x,
                          const float size_y,
                          const StringRef uv_map_id)
{
  bke::MutableAttributeAccessor attributes = mesh->attributes_for_write();
  bke::SpanAttributeWriter uv_attribute = attributes.lookup_or_add_for_write_only_span<float2>(
      uv_map_id, bke::AttrDomain::Corner);

  const float dx = (size_x == 0.0f) ? 0.0f : 1.0f / size_x;
  const float dy = (size_y == 0.0f) ? 0.0f : 1.0f / size_y;
  threading::memory_bandwidth_bound_task(
      uv_attribute.span.size_in_bytes() + positions.size_in_bytes() + corner_verts.size_in_bytes(),
      [&]() {
        threading::parallel_for(corner_verts.index_range(), 1024, [&](IndexRange range) {
          for (const int i : range) {
            const float3 &co = positions[corner_verts[i]];
            uv_attribute.span[i].x = (co.x + size_x * 0.5f) * dx;
            uv_attribute.span[i].y = (co.y + size_y * 0.5f) * dy;
          }
        });
      });

  uv_attribute.finish();
}

Mesh *create_grid_mesh(const int verts_x,
                       const int verts_y,
                       const float size_x,
                       const float size_y,
                       const std::optional<StringRef> uv_map_id)
{
  BLI_assert(verts_x > 0 && verts_y > 0);
  const int edges_x = verts_x - 1;
  const int edges_y = verts_y - 1;
  Mesh *mesh = BKE_mesh_new_nomain(verts_x * verts_y,
                                   edges_x * verts_y + edges_y * verts_x,
                                   edges_x * edges_y,
                                   edges_x * edges_y * 4);
  MutableSpan<float3> positions = mesh->vert_positions_for_write();
  MutableSpan<int2> edges = mesh->edges_for_write();
  MutableSpan<int> corner_verts = mesh->corner_verts_for_write();
  MutableSpan<int> corner_edges = mesh->corner_edges_for_write();
  bke::mesh_smooth_set(*mesh, false);

  offset_indices::fill_constant_group_size(4, 0, mesh->face_offsets_for_write());

  {
    const float dx = edges_x == 0 ? 0.0f : size_x / edges_x;
    const float dy = edges_y == 0 ? 0.0f : size_y / edges_y;
    const float x_shift = edges_x / 2.0f;
    const float y_shift = edges_y / 2.0f;
    threading::memory_bandwidth_bound_task(positions.size_in_bytes(), [&]() {
      threading::parallel_for(IndexRange(verts_x), 512, [&](IndexRange x_range) {
        for (const int x : x_range) {
          const int y_offset = x * verts_y;
          threading::parallel_for(IndexRange(verts_y), 512, [&](IndexRange y_range) {
            for (const int y : y_range) {
              const int vert = y_offset + y;
              positions[vert].x = (x - x_shift) * dx;
              positions[vert].y = (y - y_shift) * dy;
              positions[vert].z = 0.0f;
            }
          });
        }
      });
    });
  }

  const int y_edges_start = 0;
  const int x_edges_start = verts_x * edges_y;

  /* Build the horizontal edges in the X direction. */
  threading::memory_bandwidth_bound_task(edges.size_in_bytes(), [&]() {
    threading::parallel_for(IndexRange(verts_x), 512, [&](IndexRange x_range) {
      for (const int x : x_range) {
        const int y_vert_offset = x * verts_y;
        const int y_edge_offset = y_edges_start + x * edges_y;
        threading::parallel_for(IndexRange(edges_y), 512, [&](IndexRange y_range) {
          for (const int y : y_range) {
            const int vert = y_vert_offset + y;
            edges[y_edge_offset + y] = int2(vert, vert + 1);
          }
        });
      }
    });
  });

  /* Build the vertical edges in the Y direction. */
  threading::memory_bandwidth_bound_task(edges.size_in_bytes(), [&]() {
    threading::parallel_for(IndexRange(verts_y), 512, [&](IndexRange y_range) {
      for (const int y : y_range) {
        const int x_edge_offset = x_edges_start + y * edges_x;
        threading::parallel_for(IndexRange(edges_x), 512, [&](IndexRange x_range) {
          for (const int x : x_range) {
            const int vert = x * verts_y + y;
            edges[x_edge_offset + x] = int2(vert, vert + verts_y);
          }
        });
      }
    });
  });

  threading::memory_bandwidth_bound_task(
      corner_edges.size_in_bytes() + corner_verts.size_in_bytes(), [&]() {
        threading::parallel_for(IndexRange(edges_x), 512, [&](IndexRange x_range) {
          for (const int x : x_range) {
            const int y_offset = x * edges_y;
            threading::parallel_for(IndexRange(edges_y), 512, [&](IndexRange y_range) {
              for (const int y : y_range) {
                const int face = y_offset + y;
                const int corner = face * 4;
                const int vert = x * verts_y + y;

                corner_verts[corner] = vert;
                corner_edges[corner] = x_edges_start + edges_x * y + x;

                corner_verts[corner + 1] = vert + verts_y;
                corner_edges[corner + 1] = y_edges_start + edges_y * (x + 1) + y;

                corner_verts[corner + 2] = vert + verts_y + 1;
                corner_edges[corner + 2] = x_edges_start + edges_x * (y + 1) + x;

                corner_verts[corner + 3] = vert + 1;
                corner_edges[corner + 3] = y_edges_start + edges_y * x + y;
              }
            });
          }
        });
      });

  if (uv_map_id && mesh->faces_num != 0) {
    calculate_uvs(mesh, positions, corner_verts, size_x, size_y, *uv_map_id);
  }

  if (verts_x > 1 || verts_y > 1) {
    mesh->tag_loose_verts_none();
  }
  if (verts_x > 1 && verts_y > 1) {
    mesh->tag_loose_edges_none();
  }
  mesh->tag_overlapping_none();

  const float3 bounds = float3(size_x * 0.5f, size_y * 0.5f, 0.0f);
  mesh->bounds_set_eager({-bounds, bounds});

  return mesh;
}

}  // namespace blender::geometry
