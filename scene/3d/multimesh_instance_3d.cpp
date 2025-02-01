/**************************************************************************/
/*  multimesh_instance_3d.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "multimesh_instance_3d.h"

#include "scene/resources/3d/navigation_mesh_source_geometry_data_3d.h"
#include "scene/resources/navigation_mesh.h"
#include "servers/navigation_server_3d.h"

Callable MultiMeshInstance3D::_navmesh_source_geometry_parsing_callback;
RID MultiMeshInstance3D::_navmesh_source_geometry_parser;

void MultiMeshInstance3D::_refresh_interpolated() {
	if (is_inside_tree() && multimesh.is_valid()) {
		bool interpolated = is_physics_interpolated_and_enabled();
		multimesh->set_physics_interpolated(interpolated);
	}
}

void MultiMeshInstance3D::_physics_interpolated_changed() {
	VisualInstance3D::_physics_interpolated_changed();
	_refresh_interpolated();
}

void MultiMeshInstance3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_multimesh", "multimesh"), &MultiMeshInstance3D::set_multimesh);
	ClassDB::bind_method(D_METHOD("get_multimesh"), &MultiMeshInstance3D::get_multimesh);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "multimesh", PROPERTY_HINT_RESOURCE_TYPE, "MultiMesh"), "set_multimesh", "get_multimesh");
    
	ClassDB::bind_method(D_METHOD("populate_surface", "target_surface", "source_mesh", 
                                  "mesh_up_axis", "random_rotation", "random_tilt", 
                                  "random_scale", "scale", "amount"), 
                         &MultiMeshInstance3D::populate_surface);
}


void MultiMeshInstance3D::populate_surface(Ref<Mesh> target_surface, Ref<Mesh> source_mesh, 
                                                       UP_AXIS mesh_up_axis, float random_rotation, 
                                                       float random_tilt, float random_scale, 
                                                       float scale, int amount) {

	surface_source->set_text("..");
	populate_axis->select(1);
	populate_rotate_random->set_value(0);
	populate_tilt_random->set_value(0);
	populate_scale_random->set_value(0);
	populate_scale->set_value(1);
	populate_amount->set_value(128);

	// Check if the source_mesh exists, otherwise use multimesh's mesh
	if (source_mesh.is_null()) {
		// Try to get the mesh from multimesh
		if (multimesh.is_null()) {
			ERR_FAIL_MSG("No mesh source specified (and no MultiMesh set in node).");
			return;
		}
		if (multimesh->get_mesh().is_null()) {
			ERR_FAIL_MSG("No mesh source specified (and MultiMesh contains no Mesh).");
			return;
		}
		source_mesh = multimesh->get_mesh();
	}

	if (surface_source->get_text().is_empty()) {
		ERR_FAIL_MSG("No surface source specified.");
		return;
	}

	Node *ss_node = node->get_node(surface_source->get_text());

	if (!ss_node) {
		ERR_FAIL_MSG("Surface source is invalid (invalid path).");
		return;
	}

	MeshInstance3D *ss_instance = Object::cast_to<MeshInstance3D>(ss_node);

	if (!ss_instance || ss_instance->get_mesh().is_null()) {
		ERR_FAIL_MSG("Surface source is invalid (no geometry).");
		return;
	}

	Transform3D geom_xform = node->get_global_transform().affine_inverse() * ss_instance->get_global_transform();

	Vector<Face3> geometry = ss_instance->get_mesh()->get_faces();

	if (geometry.size() == 0) {
		ERR_FAIL_MSG("Surface source is invalid (no faces).");
		return;
	}

	//make all faces local

	int gc = geometry.size();
	Face3 *w = geometry.ptrw();

	for (int i = 0; i < gc; i++) {
		for (int j = 0; j < 3; j++) {
			w[i].vertex[j] = geom_xform.xform(w[i].vertex[j]);
		}
	}

	Vector<Face3> faces = geometry;
	int facecount = faces.size();
	ERR_FAIL_COND_MSG(!facecount, "Parent has no solid faces to populate.");

	const Face3 *r = faces.ptr();

	float area_accum = 0;
	RBMap<float, int> triangle_area_map;
	for (int i = 0; i < facecount; i++) {
		float area = r[i].get_area();
		if (area < CMP_EPSILON) {
			continue;
		}
		triangle_area_map[area_accum] = i;
		area_accum += area;
	}

	ERR_FAIL_COND_MSG(triangle_area_map.is_empty(), "Couldn't map area.");
	ERR_FAIL_COND_MSG(area_accum == 0, "Couldn't map area.");

	Ref<MultiMesh> multimesh = memnew(MultiMesh);
	multimesh->set_mesh(mesh);

	int instance_count = populate_amount->get_value();

	multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
	multimesh->set_use_colors(false);
	multimesh->set_instance_count(instance_count);

	float _tilt_random = populate_tilt_random->get_value();
	float _rotate_random = populate_rotate_random->get_value();
	float _scale_random = populate_scale_random->get_value();
	float _scale = populate_scale->get_value();
	int axis = populate_axis->get_selected();

	Transform3D axis_xform;
	if (axis == Vector3::AXIS_Z) {
		axis_xform.rotate(Vector3(1, 0, 0), -Math_PI * 0.5);
	}
	if (axis == Vector3::AXIS_X) {
		axis_xform.rotate(Vector3(0, 0, 1), -Math_PI * 0.5);
	}

	for (int i = 0; i < instance_count; i++) {
		float areapos = Math::random(0.0f, area_accum);

		RBMap<float, int>::Iterator E = triangle_area_map.find_closest(areapos);
		ERR_FAIL_COND(!E);
		int index = E->value;
		ERR_FAIL_INDEX(index, facecount);

		// ok FINALLY get face
		Face3 face = r[index];
		//now compute some position inside the face...

		Vector3 pos = face.get_random_point_inside();
		Vector3 normal = face.get_plane().normal;
		Vector3 op_axis = (face.vertex[0] - face.vertex[1]).normalized();

		Transform3D xform;

		xform.set_look_at(pos, pos + op_axis, normal);
		xform = xform * axis_xform;

		Basis post_xform;

		post_xform.rotate(xform.basis.get_column(1), -Math::random(-_rotate_random, _rotate_random) * Math_PI);
		post_xform.rotate(xform.basis.get_column(2), -Math::random(-_tilt_random, _tilt_random) * Math_PI);
		post_xform.rotate(xform.basis.get_column(0), -Math::random(-_tilt_random, _tilt_random) * Math_PI);

		xform.basis = post_xform * xform.basis;
		//xform.basis.orthonormalize();

		xform.basis.scale(Vector3(1, 1, 1) * (_scale + Math::random(-_scale_random, _scale_random)));

		multimesh->set_instance_transform(i, xform);
	}

	node->set_multimesh(multimesh);
}



void MultiMeshInstance3D::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		_refresh_interpolated();
	}
}

void MultiMeshInstance3D::set_multimesh(const Ref<MultiMesh> &p_multimesh) {
	multimesh = p_multimesh;
	if (multimesh.is_valid()) {
		set_base(multimesh->get_rid());
		_refresh_interpolated();
	} else {
		set_base(RID());
	}
}

Ref<MultiMesh> MultiMeshInstance3D::get_multimesh() const {
	return multimesh;
}

Array MultiMeshInstance3D::get_meshes() const {
	if (multimesh.is_null() || multimesh->get_mesh().is_null() || multimesh->get_transform_format() != MultiMesh::TransformFormat::TRANSFORM_3D) {
		return Array();
	}

	int count = multimesh->get_visible_instance_count();
	if (count == -1) {
		count = multimesh->get_instance_count();
	}

	Ref<Mesh> mesh = multimesh->get_mesh();

	Array results;
	for (int i = 0; i < count; i++) {
		results.push_back(multimesh->get_instance_transform(i));
		results.push_back(mesh);
	}
	return results;
}

AABB MultiMeshInstance3D::get_aabb() const {
	if (multimesh.is_null()) {
		return AABB();
	} else {
		return multimesh->get_aabb();
	}
}

void MultiMeshInstance3D::navmesh_parse_init() {
	ERR_FAIL_NULL(NavigationServer3D::get_singleton());
	if (!_navmesh_source_geometry_parser.is_valid()) {
		_navmesh_source_geometry_parsing_callback = callable_mp_static(&MultiMeshInstance3D::navmesh_parse_source_geometry);
		_navmesh_source_geometry_parser = NavigationServer3D::get_singleton()->source_geometry_parser_create();
		NavigationServer3D::get_singleton()->source_geometry_parser_set_callback(_navmesh_source_geometry_parser, _navmesh_source_geometry_parsing_callback);
	}
}

void MultiMeshInstance3D::navmesh_parse_source_geometry(const Ref<NavigationMesh> &p_navigation_mesh, Ref<NavigationMeshSourceGeometryData3D> p_source_geometry_data, Node *p_node) {
	MultiMeshInstance3D *multimesh_instance = Object::cast_to<MultiMeshInstance3D>(p_node);

	if (multimesh_instance == nullptr) {
		return;
	}

	NavigationMesh::ParsedGeometryType parsed_geometry_type = p_navigation_mesh->get_parsed_geometry_type();

	if (parsed_geometry_type == NavigationMesh::PARSED_GEOMETRY_MESH_INSTANCES || parsed_geometry_type == NavigationMesh::PARSED_GEOMETRY_BOTH) {
		Ref<MultiMesh> multimesh = multimesh_instance->get_multimesh();
		if (multimesh.is_valid()) {
			Ref<Mesh> mesh = multimesh->get_mesh();
			if (mesh.is_valid()) {
				int n = multimesh->get_visible_instance_count();
				if (n == -1) {
					n = multimesh->get_instance_count();
				}
				for (int i = 0; i < n; i++) {
					p_source_geometry_data->add_mesh(mesh, multimesh_instance->get_global_transform() * multimesh->get_instance_transform(i));
				}
			}
		}
	}
}

MultiMeshInstance3D::MultiMeshInstance3D() {
}

MultiMeshInstance3D::~MultiMeshInstance3D() {
}
