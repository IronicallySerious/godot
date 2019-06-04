#include "editor/editor_vcs_api.h"
#include "editor/plugins/version_control_editor_plugin.h"

void EditorVCS::_register_to_editor(String p_name) {

	if (!VersionControlEditorPlugin::get_singleton()->register_vcs(p_name)) {

		WARN_PRINT("Duplicate VCS entry trying to be registered.");
	}
}

void EditorVCS::get_commit_dock() {
}

String EditorVCS::get_vcs_name() {
	return vcs_name;
}

EditorVCS::EditorVCS(String p_vcs_name) {
}

EditorVCS::~EditorVCS() {
}
