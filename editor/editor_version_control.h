#ifndef EDITOR_VERSIONING_H
#define EDITOR_VERSIONING_H

#include "editor/editor_plugin.h"
#include "scene/gui/container.h"

class EditorVersionControl : public VBoxContainer {

	GDCLASS(EditorVersionControl, VBoxContainer)

public:
	EditorVersionControl();
	~EditorVersionControl();
};

class VersionControlEditorPlugin : public EditorPlugin {

	GDCLASS(VersionControlEditorPlugin, EditorPlugin)

	EditorVersionControl *version_control_container;

public:
	VersionControlEditorPlugin(EditorVersionControl *p_node);
	~VersionControlEditorPlugin();
};

#endif
