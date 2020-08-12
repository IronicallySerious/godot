/*************************************************************************/
/*  version_control_editor_plugin.cpp                                    */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "version_control_editor_plugin.h"

#include "core/script_language.h"
#include "editor/editor_file_system.h"
#include "editor/editor_node.h"
#include "editor/editor_scale.h"
#include "editor/filesystem_dock.h"

VersionControlEditorPlugin *VersionControlEditorPlugin::singleton = nullptr;

void VersionControlEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_selected_a_vcs"), &VersionControlEditorPlugin::_selected_a_vcs);
	ClassDB::bind_method(D_METHOD("_initialize_vcs"), &VersionControlEditorPlugin::_initialize_vcs);
	ClassDB::bind_method(D_METHOD("_commit"), &VersionControlEditorPlugin::_commit);
	ClassDB::bind_method(D_METHOD("_refresh_stage_area"), &VersionControlEditorPlugin::_refresh_stage_area);
	ClassDB::bind_method(D_METHOD("_move_all"), &VersionControlEditorPlugin::_move_all);
	ClassDB::bind_method(D_METHOD("_load_diff"), &VersionControlEditorPlugin::_load_diff);
	ClassDB::bind_method(D_METHOD("_display_diff"), &VersionControlEditorPlugin::_display_diff);
	ClassDB::bind_method(D_METHOD("_item_activated"), &VersionControlEditorPlugin::_item_activated);
	ClassDB::bind_method(D_METHOD("popup_vcs_set_up_dialog"), &VersionControlEditorPlugin::popup_vcs_set_up_dialog);
	ClassDB::bind_method(D_METHOD("_cell_button_pressed"), &VersionControlEditorPlugin::_cell_button_pressed);
	ClassDB::bind_method(D_METHOD("_discard_all"), &VersionControlEditorPlugin::_discard_all);
	ClassDB::bind_method(D_METHOD("_branch_item_selected"), &VersionControlEditorPlugin::_branch_item_selected);
	ClassDB::bind_method(D_METHOD("_fetch"), &VersionControlEditorPlugin::_fetch);
	ClassDB::bind_method(D_METHOD("_pull"), &VersionControlEditorPlugin::_pull);
	ClassDB::bind_method(D_METHOD("_push"), &VersionControlEditorPlugin::_push);
}

void VersionControlEditorPlugin::_selected_a_vcs(int p_id) {
	List<StringName> available_addons = get_available_vcs_names();
	const StringName selected_vcs = set_up_choice->get_item_text(p_id);
}

void VersionControlEditorPlugin::_populate_available_vcs_names() {
	static bool called = false;

	if (!called) {
		List<StringName> available_addons = get_available_vcs_names();
		for (int i = 0; i < available_addons.size(); i++) {
			set_up_choice->add_item(available_addons[i]);
		}

		called = true;
	}
}

VersionControlEditorPlugin *VersionControlEditorPlugin::get_singleton() {
	return singleton ? singleton : memnew(VersionControlEditorPlugin);
}

void VersionControlEditorPlugin::popup_vcs_set_up_dialog(const Control *p_gui_base) {
	fetch_available_vcs_addon_names();
	List<StringName> available_addons = get_available_vcs_names();
	if (available_addons.size() >= 1) {
		Size2 popup_size = Size2(400, 100);
		Size2 window_size = p_gui_base->get_viewport_rect().size;
		popup_size.x = MIN(window_size.x * 0.5, popup_size.x);
		popup_size.y = MIN(window_size.y * 0.5, popup_size.y);

		_populate_available_vcs_names();

		set_up_dialog->popup_centered_clamped(popup_size * EDSCALE);
	} else {
		EditorNode::get_singleton()->show_warning(TTR("No VCS addons are available."), TTR("Error"));
	}
}

void VersionControlEditorPlugin::_initialize_vcs() {
	register_editor();

	ERR_FAIL_COND_MSG(EditorVCSInterface::get_singleton(), EditorVCSInterface::get_singleton()->get_vcs_name() + " is already active");

	const int id = set_up_choice->get_selected_id();
	String selected_addon = set_up_choice->get_item_text(id);

	String path = ScriptServer::get_global_class_path(selected_addon);
	Ref<Script> script = ResourceLoader::load(path);

	ERR_FAIL_COND_MSG(!script.is_valid(), "VCS Addon path is invalid");

	EditorVCSInterface *vcs_interface = memnew(EditorVCSInterface);
	ScriptInstance *addon_script_instance = script->instance_create(vcs_interface);

	ERR_FAIL_COND_MSG(!addon_script_instance, "Failed to create addon script instance.");

	// The addon is attached as a script to the VCS interface as a proxy end-point
	vcs_interface->set_script_and_instance(script.get_ref_ptr(), addon_script_instance);

	EditorVCSInterface::set_singleton(vcs_interface);
	EditorFileSystem::get_singleton()->connect("filesystem_changed", this, "_refresh_stage_area");

	String res_dir = OS::get_singleton()->get_resource_dir();

	ERR_FAIL_COND_MSG(!EditorVCSInterface::get_singleton()->initialize(res_dir), "VCS was not initialized");

	if (set_up_username->get_text() == "" || set_up_password->get_text() == "") {
		WARN_PRINT("Some features will not work without remote credentials.")
	}

	EditorVCSInterface::get_singleton()->set_up_credentials(set_up_username->get_text(), set_up_password->get_text());
	_refresh_stage_area();
	_refresh_commit_list();
	_refresh_branch_list();
}

void VersionControlEditorPlugin::_refresh_branch_list() {
	ERR_FAIL_COND_MSG(!EditorVCSInterface::get_singleton(), "No VCS plugin is initialized. Select a Version Control Addon from Project menu");

	List<String> branch_list = EditorVCSInterface::get_singleton()->get_branch_list();
	branch_select->clear();
	for (int i = 0; i < branch_list.size(); i++) {
		branch_select->add_item(branch_list[i], i);
	}

	// TODO: Add new brach feature
	// branch_select->add_separator();
	// branch_select->add_item("New Branch");
	branch_select->select(0); // First branch in the list is current branch
}

void VersionControlEditorPlugin::_refresh_commit_list() {
	ERR_FAIL_COND_MSG(!EditorVCSInterface::get_singleton(), "No VCS plugin is initialized. Select a Version Control Addon from Project menu");

	commit_list->get_root()->clear_children();

	List<EditorVCSInterface::Commit> commit_info_list = EditorVCSInterface::get_singleton()->get_previous_commits();

	for (List<EditorVCSInterface::Commit>::Element *E = commit_info_list.front(); E; E = E->next()) {
		EditorVCSInterface::Commit commit = E->get();
		TreeItem *item = commit_list->create_item(commit_list->get_root());
		item->set_text(0, commit.msg.strip_edges());
		item->set_text(1, commit.author.strip_edges());
		item->set_metadata(0, commit.hex_id);
	}
}

void VersionControlEditorPlugin::_commit() {
	ERR_FAIL_COND_MSG(!EditorVCSInterface::get_singleton(), "No VCS plugin is initialized. Select a Version Control Addon from Project menu");

	ERR_FAIL_COND_MSG(_get_item_count(staged_files) == 0, TTR("No files added to stage"));

	String msg = commit_message->get_text();

	ERR_FAIL_COND_MSG(msg == "", TTR("No commit message was provided"));

	EditorVCSInterface::get_singleton()->commit(msg);

	commit_message->set_text("Add a commit message");
	version_control_dock_button->set_pressed(false);

	_refresh_stage_area();
	_refresh_commit_list();
	_clear_diff();
}

void VersionControlEditorPlugin::_branch_item_selected(int index) {
	ERR_FAIL_COND_MSG(!EditorVCSInterface::get_singleton(), "No VCS plugin is initialized. Select a Version Control Addon from Project menu");

	String branch_name = branch_select->get_item_text(index);
	EditorVCSInterface::get_singleton()->checkout_branch(branch_name);
	EditorFileSystem::get_singleton()->scan();
	_refresh_branch_list();
	_refresh_commit_list();
	_refresh_stage_area();
	_clear_diff();

	// FIXIT: Editor is not adopting new changes
	EditorFileSystem::get_singleton()->scan_changes();
}

int VersionControlEditorPlugin::_get_item_count(Tree *p_tree) {
	if (!p_tree->get_root()) {
		return 0;
	}

	int count = 0;
	TreeItem *file_entry = p_tree->get_root()->get_children();
	while (file_entry) {
		file_entry = file_entry->get_next();
		count++;
	}

	return count;
}

void VersionControlEditorPlugin::_refresh_stage_area() {
	ERR_FAIL_COND_MSG(!EditorVCSInterface::get_singleton(), "No VCS plugin is initialized. Select a Version Control Addon from Project menu");

	staged_files->get_root()->clear_children();
	unstaged_files->get_root()->clear_children();

	List<EditorVCSInterface::StatusFile> status_files = EditorVCSInterface::get_singleton()->get_modified_files_data();
	for (List<EditorVCSInterface::StatusFile>::Element *E = status_files.front(); E; E = E->next()) {
		EditorVCSInterface::StatusFile sf = E->get();
		if (sf.area == EditorVCSInterface::TREE_AREA_STAGED) {
			_add_new_item(staged_files, sf.file_path, sf.change_type);
		} else if (sf.area == EditorVCSInterface::TREE_AREA_UNSTAGED) {
			_add_new_item(unstaged_files, sf.file_path, sf.change_type);
		}
	}

	staged_files->update();
	unstaged_files->update();

	int total_changes = status_files.size();
	String commit_tab_title = TTR("Commit") + (total_changes > 0 ? " (" + itos(total_changes) + ")" : "");
	dock_vbc->set_tab_title(version_commit_dock->get_index(), commit_tab_title);
}

void VersionControlEditorPlugin::_discard_file(String p_file_path, EditorVCSInterface::ChangeType change) {
	if (change == EditorVCSInterface::CHANGE_TYPE_NEW) {
		DirAccess *dir = DirAccess::create(DirAccess::ACCESS_RESOURCES);
		dir->remove(p_file_path);
		memdelete(dir);
	} else {
		ERR_FAIL_COND_MSG(!EditorVCSInterface::get_singleton(), "No VCS plugin is initialized. Select a Version Control Addon from Project menu");
		EditorVCSInterface::get_singleton()->discard_file(p_file_path);
	}
	//FIXIT: The project.godot file shows wierd behaviour
	EditorFileSystem::get_singleton()->update_file(p_file_path);
}

void VersionControlEditorPlugin::_discard_all() {
	TreeItem *root = unstaged_files->get_root();
	if (root) {
		TreeItem *file_entry = root->get_children();
		while (file_entry) {
			String file_path = file_entry->get_meta("file_path");
			EditorVCSInterface::ChangeType change = (EditorVCSInterface::ChangeType)(int)file_entry->get_meta("change_type");
			_discard_file(file_path, change);
			file_entry = file_entry->get_next();
		}
	}
	_refresh_stage_area();
}

void VersionControlEditorPlugin::_add_new_item(Tree *p_tree, String p_file_path, EditorVCSInterface::ChangeType change) {
	String change_text = p_file_path + " (" + change_type_to_strings[change] + ")";

	TreeItem *new_item = p_tree->create_item(p_tree->get_root());
	new_item->set_text(0, change_text);
	new_item->set_icon(0, change_type_to_icon[change]);
	new_item->set_meta("file_path", p_file_path);
	new_item->set_meta("change_type", change);
	new_item->set_custom_color(0, change_type_to_color[change]);

	new_item->add_button(0, EditorNode::get_singleton()->get_gui_base()->get_icon("Open", "EditorIcons"), BUTTON_TYPE_OPEN, false, "Open");
	if (p_tree == unstaged_files) {
		new_item->add_button(0, EditorNode::get_singleton()->get_gui_base()->get_icon("Undo", "EditorIcons"), BUTTON_TYPE_DISCARD, false, "Discard Changes");
	}
}

void VersionControlEditorPlugin::_fetch() {
	ERR_FAIL_COND_MSG(!EditorVCSInterface::get_singleton(), "No VCS plugin is initialized. Select a Version Control Addon from Project menu");

	EditorVCSInterface::get_singleton()->fetch();
	_refresh_branch_list();
}

void VersionControlEditorPlugin::_pull() {
	ERR_FAIL_COND_MSG(!EditorVCSInterface::get_singleton(), "No VCS plugin is initialized. Select a Version Control Addon from Project menu");

	EditorVCSInterface::get_singleton()->pull();
	_refresh_stage_area();
	_refresh_branch_list();
	_refresh_commit_list();
	_clear_diff();
}

void VersionControlEditorPlugin::_push() {
	ERR_FAIL_COND_MSG(!EditorVCSInterface::get_singleton(), "No VCS plugin is initialized. Select a Version Control Addon from Project menu");

	EditorVCSInterface::get_singleton()->push();
}

void VersionControlEditorPlugin::_move_all(Object *p_tree) {
	Tree *tree = Object::cast_to<Tree>(p_tree);

	TreeItem *root = tree->get_root();
	if (root) {
		TreeItem *file_entry = root->get_children();
		while (file_entry) {
			_move_item(tree, file_entry);
			file_entry = file_entry->get_next();
		}
	}
	_refresh_stage_area();
}

void VersionControlEditorPlugin::_load_diff(Object *p_tree) {
	ERR_FAIL_COND_MSG(!EditorVCSInterface::get_singleton(), "No VCS plugin is initialized. Select a Version Control Addon from Project menu");

	version_control_dock_button->set_pressed(true);
	Tree *tree = Object::cast_to<Tree>(p_tree);
	if (tree == staged_files) {
		String file_path = tree->get_selected()->get_meta("file_path");
		diff_title->set_text("Staged Changes");
		diff_content = EditorVCSInterface::get_singleton()->get_file_diff(file_path, EditorVCSInterface::TREE_AREA_STAGED);
	} else if (tree == unstaged_files) {
		String file_path = tree->get_selected()->get_meta("file_path");
		diff_title->set_text("Unstaged Changes");
		diff_content = EditorVCSInterface::get_singleton()->get_file_diff(file_path, EditorVCSInterface::TREE_AREA_UNSTAGED);
	} else if (tree == commit_list) {
		String commit_id = tree->get_selected()->get_metadata(0);
		diff_title->set_text(tree->get_selected()->get_text(0));
		diff_content = EditorVCSInterface::get_singleton()->get_file_diff(commit_id, EditorVCSInterface::TREE_AREA_COMMIT);
	}
	_display_diff(0);
}

void VersionControlEditorPlugin::_clear_diff() {
	diff->clear();
	diff_content = List<EditorVCSInterface::DiffFile>();
	diff_title->set_text("");
}

void VersionControlEditorPlugin::_item_activated(Object *p_tree) {
	Tree *tree = Object::cast_to<Tree>(p_tree);

	_move_item(tree, tree->get_selected());
	_refresh_stage_area();
}

void VersionControlEditorPlugin::_move_item(Tree *p_tree, TreeItem *p_item) {
	ERR_FAIL_COND_MSG(!EditorVCSInterface::get_singleton(), "No VCS plugin is initialized. Select a Version Control Addon from Project menu");

	if (p_tree == staged_files) {
		EditorVCSInterface::get_singleton()->unstage_file(p_item->get_meta("file_path"));
	} else {
		EditorVCSInterface::get_singleton()->stage_file(p_item->get_meta("file_path"));
	}
}

void VersionControlEditorPlugin::_cell_button_pressed(Object *p_item, int column, int id) {
	TreeItem *item = Object::cast_to<TreeItem>(p_item);
	String file_path = item->get_meta("file_path");
	EditorVCSInterface::ChangeType change = (EditorVCSInterface::ChangeType)(int)item->get_meta("change_type");

	if (id == BUTTON_TYPE_OPEN && change != EditorVCSInterface::CHANGE_TYPE_DELETED) {
		DirAccess *dir = DirAccess::create(DirAccess::ACCESS_RESOURCES);
		if (!dir->file_exists(file_path)) {
			return;
		}
		memdelete(dir);

		file_path = "res://" + file_path;
		if (ResourceLoader::get_resource_type(file_path) == "PackedScene") {
			EditorNode::get_singleton()->open_request(file_path);
		} else if (file_path.ends_with(".gd")) {
			EditorNode::get_singleton()->load_resource(file_path);
		} else {
			EditorNode::get_singleton()->get_filesystem_dock()->navigate_to_path(file_path);
		}

	} else if (id == BUTTON_TYPE_DISCARD) {
		_discard_file(file_path, change);
		_refresh_stage_area();
	}
}

void VersionControlEditorPlugin::_display_diff(int idx) {
	ERR_FAIL_COND_MSG(diff_content.size() == 0, "Diff is empty");

	DiffViewType diff_view = (DiffViewType)diff_view_type_select->get_selected();

	diff->clear();
	for (int i = 0; i < diff_content.size(); i++) {

		EditorVCSInterface::DiffFile diff_file = diff_content[i];

		diff->push_font(EditorNode::get_singleton()->get_gui_base()->get_font("doc_bold", "EditorFonts"));
		diff->push_color(EditorNode::get_singleton()->get_gui_base()->get_color("accent_color", "Editor"));
		diff->add_text("File: " + diff_file.new_file);
		diff->pop();
		diff->pop();

		for (int j = 0; j < diff_file.diff_hunks.size(); j++) {

			EditorVCSInterface::DiffHunk hunk = diff_file.diff_hunks[j];

			String old_start = String::num_int64(hunk.old_start);
			String new_start = String::num_int64(hunk.new_start);
			String old_lines = String::num_int64(hunk.old_lines);
			String new_lines = String::num_int64(hunk.new_lines);

			diff->push_font(EditorNode::get_singleton()->get_gui_base()->get_font("source", "EditorFonts"));

			diff->push_align(RichTextLabel::ALIGN_CENTER);
			diff->add_text("@@ " + old_start + "," + old_lines + " " + new_start + "," + new_lines + " @@");
			diff->pop();

			switch (diff_view) {
				case DIFF_VIEW_TYPE_SPLIT:
					_display_diff_split_view(hunk.diff_lines);
					break;
				case DIFF_VIEW_TYPE_UNIFIED:
					_display_diff_unified_view(hunk.diff_lines);
					break;
			}
			diff->add_newline();
			diff->add_newline();
			diff->pop();
		}
	}
}

void VersionControlEditorPlugin::_display_diff_split_view(List<EditorVCSInterface::DiffLine> &p_diff_content) {

	List<EditorVCSInterface::DiffLine> parsed_diff = List<EditorVCSInterface::DiffLine>();

	for (int i = 0; i < p_diff_content.size(); i++) {
		EditorVCSInterface::DiffLine diff_line = p_diff_content[i];
		String line = diff_line.content.strip_edges(false, true);

		if (diff_line.new_line_no >= 0 && diff_line.old_line_no >= 0) {
			diff_line.new_text = line;
			diff_line.old_text = line;
			parsed_diff.push_back(diff_line);
		} else if (diff_line.new_line_no == -1) {
			diff_line.new_text = "";
			diff_line.old_text = line;
			parsed_diff.push_back(diff_line);
		} else if (diff_line.old_line_no == -1) {
			int j = parsed_diff.size() - 1;
			while (j >= 0 && parsed_diff[j].new_line_no == -1) {
				j--;
			}

			if (j == parsed_diff.size() - 1) {
				// no lines are modified
				diff_line.new_text = line;
				diff_line.old_text = "";
				parsed_diff.push_back(diff_line);
			} else {
				// lines are modified
				EditorVCSInterface::DiffLine modified_line = parsed_diff[j + 1];
				modified_line.new_text = line;
				modified_line.new_line_no = diff_line.new_line_no;
				parsed_diff[j + 1] = modified_line;
			}
		}
	}

	diff->push_table(6);
	/*
		[cell]Old Line No[/cell]
		[cell]prefix[/cell]
		[cell]Old Code[/cell]

		[cell]New Line No[/cell]
		[cell]prefix[/cell]
		[cell]New Line[/cell]
	*/

	diff->set_table_column_expand(2, true);
	diff->set_table_column_expand(5, true);

	for (int i = 0; i < parsed_diff.size(); i++) {

		EditorVCSInterface::DiffLine diff_line = parsed_diff[i];

		bool has_change = diff_line.status != " ";
		static const Color red = EditorNode::get_singleton()->get_gui_base()->get_color("error_color", "Editor");
		static const Color green = EditorNode::get_singleton()->get_gui_base()->get_color("success_color", "Editor");
		static const Color white = EditorNode::get_singleton()->get_gui_base()->get_color("font_color", "Label") * Color(1, 1, 1, 0.6);

		if (diff_line.old_line_no >= 0) {

			diff->push_cell();
			diff->push_indent(1);
			diff->push_color(has_change ? red : white);
			diff->add_text(String::num_int64(diff_line.old_line_no));
			diff->pop();
			diff->pop();
			diff->pop();

			diff->push_cell();
			diff->push_color(has_change ? red : white);
			diff->add_text(has_change ? "-|" : " |");
			diff->pop();
			diff->pop();

			diff->push_cell();
			diff->push_color(has_change ? red : white);
			diff->add_text(diff_line.old_text);
			diff->pop();
			diff->pop();

		} else {
			diff->push_cell();
			diff->pop();

			diff->push_cell();
			diff->pop();

			diff->push_cell();
			diff->pop();
		}

		if (diff_line.new_line_no >= 0) {
			diff->push_cell();
			diff->push_indent(1);
			diff->push_color(has_change ? green : white);
			diff->add_text(String::num_int64(diff_line.new_line_no));
			diff->pop();
			diff->pop();
			diff->pop();

			diff->push_cell();
			diff->push_color(has_change ? green : white);
			diff->add_text(has_change ? "+|" : " |");
			diff->pop();
			diff->pop();

			diff->push_cell();
			diff->push_color(has_change ? green : white);
			diff->add_text(diff_line.new_text);
			diff->pop();
			diff->pop();
		} else {
			diff->push_cell();
			diff->pop();

			diff->push_cell();
			diff->pop();

			diff->push_cell();
			diff->pop();
		}
	}
	diff->pop();
}

void VersionControlEditorPlugin::_display_diff_unified_view(List<EditorVCSInterface::DiffLine> &p_diff_content) {

	diff->push_table(4);
	diff->set_table_column_expand(3, true);

	/*
		[cell]Old Line No[/cell]
		[cell]New Line No[/cell]
		[cell]status[/cell]
		[cell]code[/cell]
	*/
	for (int i = 0; i < p_diff_content.size(); i++) {
		EditorVCSInterface::DiffLine diff_line = p_diff_content[i];
		String line = diff_line.content.strip_edges(false, true);

		Color color;
		if (diff_line.status == "+") {
			color = EditorNode::get_singleton()->get_gui_base()->get_color("success_color", "Editor");
		} else if (diff_line.status == "-") {
			color = EditorNode::get_singleton()->get_gui_base()->get_color("error_color", "Editor");
		} else {
			color = EditorNode::get_singleton()->get_gui_base()->get_color("font_color", "Label");
			color *= Color(1, 1, 1, 0.6);
		}

		diff->push_cell();
		diff->push_color(color);
		diff->push_indent(1);
		diff->add_text(diff_line.old_line_no >= 0 ? String::num_int64(diff_line.old_line_no) : "");
		diff->pop();
		diff->pop();
		diff->pop();

		diff->push_cell();
		diff->push_color(color);
		diff->push_indent(1);
		diff->add_text(diff_line.new_line_no >= 0 ? String::num_int64(diff_line.new_line_no) : "");
		diff->pop();
		diff->pop();
		diff->pop();

		diff->push_cell();
		diff->push_color(color);
		diff->add_text(diff_line.status != "" ? diff_line.status + "|" : " |");
		diff->pop();
		diff->pop();

		diff->push_cell();
		diff->push_color(color);
		diff->add_text(line);
		diff->pop();
		diff->pop();
	}

	diff->pop(); //table
}

void VersionControlEditorPlugin::register_editor() {
	if (!EditorVCSInterface::get_singleton()) {
		EditorNode::get_singleton()->add_control_to_dock(EditorNode::DOCK_SLOT_RIGHT_UL, version_commit_dock);
		dock_vbc = (TabContainer *)version_commit_dock->get_parent_control();
		dock_vbc->set_tab_title(version_commit_dock->get_index(), TTR("Commit"));

		ToolButton *vc = EditorNode::get_singleton()->add_bottom_panel_item(TTR("Version Control"), version_control_dock);
		set_version_control_tool_button(vc);
	}
}

void VersionControlEditorPlugin::fetch_available_vcs_addon_names() {
	List<StringName> global_classes;
	ScriptServer::get_global_class_list(&global_classes);

	for (int i = 0; i != global_classes.size(); i++) {
		String path = ScriptServer::get_global_class_path(global_classes[i]);
		Ref<Script> script = ResourceLoader::load(path);
		ERR_FAIL_COND(script.is_null());

		if (script->get_instance_base_type() == "EditorVCSInterface") {
			available_addons.push_back(global_classes[i]);
		}
	}
}

void VersionControlEditorPlugin::shut_down() {
	if (EditorVCSInterface::get_singleton()) {
		if (EditorFileSystem::get_singleton()->is_connected("filesystem_changed", this, "_refresh_stage_area")) {
			EditorFileSystem::get_singleton()->disconnect("filesystem_changed", this, "_refresh_stage_area");
		}
		EditorVCSInterface::get_singleton()->shut_down();
		memdelete(EditorVCSInterface::get_singleton());
		EditorVCSInterface::set_singleton(nullptr);

		EditorNode::get_singleton()->remove_control_from_dock(version_commit_dock);
		EditorNode::get_singleton()->remove_bottom_panel_item(version_control_dock);
	}
}

bool VersionControlEditorPlugin::is_vcs_initialized() const {
	return EditorVCSInterface::get_singleton() ? EditorVCSInterface::get_singleton()->is_vcs_initialized() : false;
}

const String VersionControlEditorPlugin::get_vcs_name() const {
	return EditorVCSInterface::get_singleton() ? EditorVCSInterface::get_singleton()->get_vcs_name() : "";
}

VersionControlEditorPlugin::VersionControlEditorPlugin() {
	singleton = this;

	version_control_actions = memnew(PopupMenu);
	version_control_actions->set_v_size_flags(BoxContainer::SIZE_SHRINK_CENTER);

	set_up_dialog = memnew(AcceptDialog);
	set_up_dialog->set_title(TTR("Set Up Version Control"));
	set_up_dialog->set_custom_minimum_size(Size2(400, 100));
	set_up_dialog->set_hide_on_ok(true);
	version_control_actions->add_child(set_up_dialog);

	VBoxContainer *set_up_vbc = memnew(VBoxContainer);
	set_up_vbc->set_alignment(VBoxContainer::ALIGN_CENTER);
	set_up_dialog->add_child(set_up_vbc);

	HBoxContainer *set_up_hbc = memnew(HBoxContainer);
	set_up_hbc->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_up_vbc->add_child(set_up_hbc);

	RichTextLabel *set_up_vcs_status = memnew(RichTextLabel);
	set_up_vcs_status->set_text(TTR("VCS Addon is not initialized"));
	set_up_vbc->add_child(set_up_vcs_status);

	Label *set_up_vcs_label = memnew(Label);
	set_up_vcs_label->set_text(TTR("Version Control System"));
	set_up_hbc->add_child(set_up_vcs_label);

	set_up_choice = memnew(OptionButton);
	set_up_choice->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_up_choice->connect("item_selected", this, "_selected_a_vcs");
	set_up_hbc->add_child(set_up_choice);

	HSeparator *hs1 = memnew(HSeparator);
	set_up_vbc->add_child(hs1);

	Label *remote_login = memnew(Label);
	remote_login->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	remote_login->set_align(Label::ALIGN_CENTER);
	remote_login->set_text(TTR("Remote login credentials"));
	set_up_vbc->add_child(remote_login);

	HBoxContainer *set_up_username_input = memnew(HBoxContainer);
	set_up_username_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	Label *set_up_username_label = memnew(Label);
	set_up_username_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_up_username_label->set_text(TTR("Username: "));
	set_up_username_input->add_child(set_up_username_label);
	set_up_username = memnew(LineEdit);
	set_up_username->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_up_username_input->add_child(set_up_username);
	set_up_vbc->add_child(set_up_username_input);

	HBoxContainer *set_up_password_input = memnew(HBoxContainer);
	set_up_password_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	Label *set_up_password_label = memnew(Label);
	set_up_password_label->set_text(TTR("Password: "));
	set_up_password_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_up_password_input->add_child(set_up_password_label);
	set_up_password = memnew(LineEdit);
	set_up_password->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_up_password->set_secret(true);
	set_up_password_input->add_child(set_up_password);
	set_up_vbc->add_child(set_up_password_input);

	set_up_init_settings = NULL;

	HSeparator *hs2 = memnew(HSeparator);
	set_up_vbc->add_child(hs2);

	set_up_init_button = set_up_dialog->get_ok();
	set_up_init_button->set_text(TTR("Initialize"));
	set_up_init_button->connect("pressed", this, "_initialize_vcs");

	version_control_actions->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	version_control_actions->set_h_size_flags(Control::SIZE_EXPAND_FILL);

	// Commit Dock
	version_commit_dock = memnew(VBoxContainer);
	version_commit_dock->set_visible(false);

	// Unstage Area
	VBoxContainer *unstage_area = memnew(VBoxContainer);
	unstage_area->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	unstage_area->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	version_commit_dock->add_child(unstage_area);

	// Title of unstage area
	HBoxContainer *unstage_title = memnew(HBoxContainer);
	Label *unstage_label = memnew(Label);
	unstage_label->set_text(TTR("Unstaged Changes"));
	unstage_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	unstage_title->add_child(unstage_label);

	refresh_button = memnew(ToolButton);
	refresh_button->set_tooltip(TTR("Detect new changes"));
	refresh_button->set_flat(true);
	refresh_button->set_icon(EditorNode::get_singleton()->get_gui_base()->get_icon("Refresh", "EditorIcons"));
	refresh_button->connect("pressed", this, "_refresh_stage_area");
	unstage_title->add_child(refresh_button);

	discard_all_button = memnew(ToolButton);
	discard_all_button->set_tooltip(TTR("Discard All changes"));
	discard_all_button->set_icon(EditorNode::get_singleton()->get_gui_base()->get_icon("Close", "EditorIcons"));
	discard_all_button->connect("pressed", this, "_discard_all");
	discard_all_button->set_flat(true);
	unstage_title->add_child(discard_all_button);

	stage_all_button = memnew(ToolButton);
	stage_all_button->set_flat(true);
	stage_all_button->set_icon(EditorNode::get_singleton()->get_gui_base()->get_icon("MoveDown", "EditorIcons"));
	stage_all_button->set_tooltip(TTR("Stage all changes"));
	unstage_title->add_child(stage_all_button);
	unstage_area->add_child(unstage_title);

	// Body of unstage area
	unstaged_files = memnew(Tree);
	unstaged_files->set_h_size_flags(Tree::SIZE_EXPAND_FILL);
	unstaged_files->set_v_size_flags(Tree::SIZE_EXPAND_FILL);
	unstaged_files->set_select_mode(Tree::SELECT_ROW);
	unstaged_files->connect("item_selected", this, "_load_diff", varray(unstaged_files));
	unstaged_files->connect("item_activated", this, "_item_activated", varray(unstaged_files));
	unstaged_files->connect("button_pressed", this, "_cell_button_pressed");
	unstaged_files->create_item();
	unstaged_files->set_hide_root(true);
	unstage_area->add_child(unstaged_files);

	//stage area
	VBoxContainer *stage_area = memnew(VBoxContainer);
	stage_area->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	stage_area->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	version_commit_dock->add_child(stage_area);

	// Title of stage area
	HBoxContainer *stage_title = memnew(HBoxContainer);
	Label *stage_label = memnew(Label);
	stage_label->set_text(TTR("Staged Changes"));
	stage_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	stage_title->add_child(stage_label);
	unstage_all_button = memnew(ToolButton);
	unstage_all_button->set_flat(true);
	unstage_all_button->set_icon(EditorNode::get_singleton()->get_gui_base()->get_icon("MoveUp", "EditorIcons"));
	unstage_all_button->set_tooltip(TTR("Unstage all changes"));
	stage_title->add_child(unstage_all_button);
	stage_area->add_child(stage_title);

	// Body of stage area
	staged_files = memnew(Tree);
	staged_files->set_h_size_flags(Tree::SIZE_EXPAND_FILL);
	staged_files->set_v_size_flags(Tree::SIZE_EXPAND_FILL);
	staged_files->set_select_mode(Tree::SELECT_ROW);
	staged_files->connect("item_selected", this, "_load_diff", varray(staged_files));
	staged_files->connect("button_pressed", this, "_cell_button_pressed");
	staged_files->connect("item_activated", this, "_item_activated", varray(staged_files));
	staged_files->create_item();
	staged_files->set_hide_root(true);
	stage_area->add_child(staged_files);

	// Editor crashes if bind is null
	unstage_all_button->connect("pressed", this, "_move_all", varray(staged_files));
	stage_all_button->connect("pressed", this, "_move_all", varray(unstaged_files));

	HSeparator *hs = memnew(HSeparator);
	version_commit_dock->add_child(hs);

	VBoxContainer *commit_area = memnew(VBoxContainer);
	version_commit_dock->add_child(commit_area);

	commit_message = memnew(TextEdit);
	commit_message->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	commit_message->set_h_grow_direction(Control::GrowDirection::GROW_DIRECTION_BEGIN);
	commit_message->set_v_grow_direction(Control::GrowDirection::GROW_DIRECTION_END);
	commit_message->set_custom_minimum_size(Size2(200, 100));
	commit_message->set_wrap_enabled(true);
	commit_message->set_text(TTR("Add a commit message"));
	commit_area->add_child(commit_message);

	commit_button = memnew(Button);
	commit_button->set_text(TTR("Commit Changes"));
	commit_button->connect("pressed", this, "_commit");
	commit_area->add_child(commit_button);

	HSeparator *hs_1 = memnew(HSeparator);
	version_commit_dock->add_child(hs_1);

	commit_list = memnew(Tree);
	commit_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	commit_list->set_v_grow_direction(Control::GrowDirection::GROW_DIRECTION_END);
	commit_list->set_custom_minimum_size(Size2(200, 80));
	commit_list->create_item();
	commit_list->set_hide_root(true);
	commit_list->set_select_mode(Tree::SELECT_ROW);
	commit_list->set_columns(2); // Commit msg, author
	commit_list->set_column_min_width(0, 75);
	commit_list->set_column_min_width(1, 25);
	commit_list->connect("item_selected", this, "_load_diff", varray(commit_list));
	version_commit_dock->add_child(commit_list);

	HSeparator *hs_2 = memnew(HSeparator);
	version_commit_dock->add_child(hs_2);

	HBoxContainer *menu_bar = memnew(HBoxContainer);
	menu_bar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	menu_bar->set_v_size_flags(Control::SIZE_FILL);

	branch_select = memnew(OptionButton);
	branch_select->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	branch_select->connect("item_selected", this, "_branch_item_selected");
	menu_bar->add_child(branch_select);

	fetch_button = memnew(ToolButton);
	fetch_button->set_tooltip(TTR("Fetch"));
	fetch_button->set_icon(EditorNode::get_singleton()->get_gui_base()->get_icon("Refresh", "EditorIcons"));
	fetch_button->connect("pressed", this, "_fetch");
	menu_bar->add_child(fetch_button);

	pull_button = memnew(ToolButton);
	pull_button->set_tooltip(TTR("Pull"));
	pull_button->set_icon(EditorNode::get_singleton()->get_gui_base()->get_icon("MoveDown", "EditorIcons"));
	pull_button->connect("pressed", this, "_pull");
	menu_bar->add_child(pull_button);

	push_button = memnew(ToolButton);
	push_button->set_tooltip(TTR("Push"));
	push_button->set_icon(EditorNode::get_singleton()->get_gui_base()->get_icon("MoveUp", "EditorIcons"));
	push_button->connect("pressed", this, "_push");
	menu_bar->add_child(push_button);
	version_commit_dock->add_child(menu_bar);

	change_type_to_strings[EditorVCSInterface::CHANGE_TYPE_NEW] = TTR("New");
	change_type_to_strings[EditorVCSInterface::CHANGE_TYPE_MODIFIED] = TTR("Modified");
	change_type_to_strings[EditorVCSInterface::CHANGE_TYPE_RENAMED] = TTR("Renamed");
	change_type_to_strings[EditorVCSInterface::CHANGE_TYPE_DELETED] = TTR("Deleted");
	change_type_to_strings[EditorVCSInterface::CHANGE_TYPE_TYPECHANGE] = TTR("Typechange");
	change_type_to_strings[EditorVCSInterface::CHANGE_TYPE_UNMERGED] = TTR("Unmerged");

	change_type_to_color[EditorVCSInterface::CHANGE_TYPE_NEW] = EditorNode::get_singleton()->get_gui_base()->get_color("success_color", "Editor");
	change_type_to_color[EditorVCSInterface::CHANGE_TYPE_MODIFIED] = EditorNode::get_singleton()->get_gui_base()->get_color("warning_color", "Editor");
	change_type_to_color[EditorVCSInterface::CHANGE_TYPE_RENAMED] = EditorNode::get_singleton()->get_gui_base()->get_color("warning_color", "Editor");
	change_type_to_color[EditorVCSInterface::CHANGE_TYPE_DELETED] = EditorNode::get_singleton()->get_gui_base()->get_color("error_color", "Editor");
	change_type_to_color[EditorVCSInterface::CHANGE_TYPE_TYPECHANGE] = EditorNode::get_singleton()->get_gui_base()->get_color("font_color", "Editor");
	change_type_to_color[EditorVCSInterface::CHANGE_TYPE_UNMERGED] = EditorNode::get_singleton()->get_gui_base()->get_color("warning_color", "Editor");

	change_type_to_icon[EditorVCSInterface::CHANGE_TYPE_NEW] = EditorNode::get_singleton()->get_gui_base()->get_icon("StatusSuccess", "EditorIcons");
	change_type_to_icon[EditorVCSInterface::CHANGE_TYPE_MODIFIED] = EditorNode::get_singleton()->get_gui_base()->get_icon("StatusWarning", "EditorIcons");
	change_type_to_icon[EditorVCSInterface::CHANGE_TYPE_RENAMED] = EditorNode::get_singleton()->get_gui_base()->get_icon("StatusWarning", "EditorIcons");
	change_type_to_icon[EditorVCSInterface::CHANGE_TYPE_TYPECHANGE] = EditorNode::get_singleton()->get_gui_base()->get_icon("StatusWarning", "EditorIcons");
	change_type_to_icon[EditorVCSInterface::CHANGE_TYPE_DELETED] = EditorNode::get_singleton()->get_gui_base()->get_icon("StatusError", "EditorIcons");
	change_type_to_icon[EditorVCSInterface::CHANGE_TYPE_UNMERGED] = EditorNode::get_singleton()->get_gui_base()->get_icon("StatusWarning", "EditorIcons");

	version_control_dock = memnew(VBoxContainer);
	version_control_dock->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	version_control_dock->set_custom_minimum_size(Size2(0, 250));
	version_control_dock->hide();

	// VBoxContainer *vc_dock_vb = memnew(VBoxContainer);
	// version_control_dock->add_child(vc_dock_vb);

	HBoxContainer *diff_heading = memnew(HBoxContainer);
	diff_heading->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	version_control_dock->add_child(diff_heading);

	diff_title = memnew(Label);
	diff_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	diff_heading->add_child(diff_title);

	Label *view = memnew(Label);
	view->set_text(TTR("View:"));
	diff_heading->add_child(view);

	diff_view_type_select = memnew(OptionButton);
	diff_view_type_select->add_item("Split", DIFF_VIEW_TYPE_SPLIT);
	diff_view_type_select->add_item("Unified", DIFF_VIEW_TYPE_UNIFIED);
	diff_view_type_select->connect("item_selected", this, "_display_diff");
	diff_heading->add_child(diff_view_type_select);

	diff = memnew(RichTextLabel);
	diff->set_h_size_flags(TextEdit::SIZE_EXPAND_FILL);
	diff->set_v_size_flags(TextEdit::SIZE_EXPAND_FILL);
	diff->set_selection_enabled(true);
	version_control_dock->add_child(diff);
}

VersionControlEditorPlugin::~VersionControlEditorPlugin() {
	shut_down();
	memdelete(version_control_dock);
	memdelete(version_commit_dock);
	memdelete(version_control_actions);
}
