extends SceneTree
## Offline conversion of the supplied rigged Godot scenes; no XR initialization.

func _initialize() -> void:
	call_deferred("_export_hands")


func _export_hands() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	if args.size() != 2 or not args[0].is_absolute_path() or not args[1].is_absolute_path():
		_fail("Expected absolute source and output directories after --")
		return
	if DirAccess.make_dir_recursive_absolute(args[1]) != OK:
		_fail("Cannot create output directory")
		return
	var report: Dictionary = {"engine": Engine.get_version_info()["string"], "hands": []}
	for side: String in ["left", "right"]:
		var source_path: String = args[0].path_join(side + "_hand.tscn")
		var packed: PackedScene = load(source_path) as PackedScene
		if packed == null:
			_fail("Cannot load " + source_path)
			return
		var model: Node3D = packed.instantiate() as Node3D
		root.add_child(model)
		var meshes: Array[Node] = model.find_children("*", "MeshInstance3D", true, false)
		var skeletons: Array[Node] = model.find_children("*", "Skeleton3D", true, false)
		if meshes.size() != 1 or skeletons.size() != 1:
			_fail("Expected one mesh and skeleton for " + side)
			return
		var mesh_node: MeshInstance3D = meshes[0] as MeshInstance3D
		var skeleton: Skeleton3D = skeletons[0] as Skeleton3D
		# Use an explicit skeleton path, independent of project compatibility defaults.
		mesh_node.skeleton = mesh_node.get_path_to(skeleton)
		var material: StandardMaterial3D = StandardMaterial3D.new()
		material.resource_name = "Brown glove"
		material.albedo_color = Color(0.42, 0.27, 0.19, 1.0)
		material.roughness = 0.9
		mesh_node.material_override = material
		var state: GLTFState = GLTFState.new()
		var document: GLTFDocument = GLTFDocument.new()
		var result: Error = document.append_from_scene(model, state)
		if result != OK:
			_fail("Scene export failed for " + side + ": " + str(result))
			return
		var output_path: String = args[1].path_join(side + "_hand.glb")
		result = document.write_to_filesystem(state, output_path)
		if result != OK:
			_fail("GLB write failed for " + side + ": " + str(result))
			return
		if not _validate_round_trip(output_path, skeleton, mesh_node):
			return
		var arrays: Array = mesh_node.mesh.surface_get_arrays(0)
		var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var indices: PackedInt32Array = PackedInt32Array()
		if arrays[Mesh.ARRAY_INDEX] != null:
			indices = arrays[Mesh.ARRAY_INDEX]
		var triangles: int = vertices.size() / 3 if indices.is_empty() else indices.size() / 3
		report["hands"].append({
			"side": side,
			"vertices": vertices.size(),
			"triangles": triangles,
			"bones": skeleton.get_bone_count(),
			"binds": mesh_node.skin.get_bind_count(),
			"source_sha256": FileAccess.get_sha256(source_path),
			"glb_sha256": FileAccess.get_sha256(output_path),
			"round_trip_validated": true,
		})
		model.free()
	var report_file: FileAccess = FileAccess.open(args[1].path_join("export-report.json"), FileAccess.WRITE)
	if report_file == null:
		_fail("Cannot write export report")
		return
	report_file.store_string(JSON.stringify(report, "\t") + "\n")
	report_file.close()
	print(JSON.stringify(report))
	quit(0)


func _validate_round_trip(path: String, source_skeleton: Skeleton3D, source_mesh: MeshInstance3D) -> bool:
	var state: GLTFState = GLTFState.new()
	var document: GLTFDocument = GLTFDocument.new()
	if document.append_from_file(path, state) != OK:
		_fail("Cannot reimport exported GLB")
		return false
	var model: Node3D = document.generate_scene(state)
	if model == null:
		_fail("Cannot instantiate exported GLB")
		return false
	root.add_child(model)
	var meshes: Array[Node] = model.find_children("*", "MeshInstance3D", true, false)
	var skeletons: Array[Node] = model.find_children("*", "Skeleton3D", true, false)
	if meshes.size() != 1 or skeletons.size() != 1:
		_fail("Round trip changed mesh or skeleton count")
		return false
	var mesh_node: MeshInstance3D = meshes[0] as MeshInstance3D
	var skeleton: Skeleton3D = skeletons[0] as Skeleton3D
	if skeleton.get_bone_count() != source_skeleton.get_bone_count():
		_fail("Round trip changed bone count")
		return false
	for bone: int in source_skeleton.get_bone_count():
		var imported_bone: int = skeleton.find_bone(source_skeleton.get_bone_name(bone))
		if imported_bone < 0 or not skeleton.get_bone_global_rest(imported_bone).is_equal_approx(
			source_skeleton.get_bone_global_rest(bone)
		):
			_fail("Round trip changed joint rest transform")
			return false
	var original: Array = source_mesh.mesh.surface_get_arrays(0)
	var imported: Array = mesh_node.mesh.surface_get_arrays(0)
	var original_vertices: PackedVector3Array = original[Mesh.ARRAY_VERTEX]
	var vertices: PackedVector3Array = imported[Mesh.ARRAY_VERTEX]
	var joints: PackedInt32Array = imported[Mesh.ARRAY_BONES]
	var weights: PackedFloat32Array = imported[Mesh.ARRAY_WEIGHTS]
	if (
		vertices.size() != original_vertices.size()
		or joints.size() != vertices.size() * 4
		or weights.size() != joints.size()
	):
		_fail("Round trip changed vertex or skin layout")
		return false
	for vertex: int in vertices.size():
		if not vertices[vertex].is_equal_approx(original_vertices[vertex]):
			_fail("Round trip changed a vertex position")
			return false
		var total: float = 0.0
		for influence: int in 4:
			var at: int = vertex * 4 + influence
			if (
				joints[at] < 0 or joints[at] >= mesh_node.skin.get_bind_count()
				or not is_finite(weights[at]) or weights[at] < 0.0
			):
				_fail("Invalid exported joint index or weight")
				return false
			total += weights[at]
		if absf(total - 1.0) > 0.0001:
			_fail("Exported skin weights do not sum to one")
			return false
	for bind: int in mesh_node.skin.get_bind_count():
		var bone: int = mesh_node.skin.get_bind_bone(bind)
		if bone < 0:
			bone = skeleton.find_bone(mesh_node.skin.get_bind_name(bind))
		if bone < 0 or not (
			skeleton.get_bone_global_rest(bone) * mesh_node.skin.get_bind_pose(bind)
		).is_equal_approx(Transform3D.IDENTITY):
			_fail("Exported inverse bind does not cancel the rest transform")
			return false
	model.free()
	return true


func _fail(message: String) -> void:
	push_error(message)
	quit(1)
