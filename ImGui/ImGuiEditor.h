#pragma once
#include <memory>
#include <vector>

class ImGuiEditor {
private:
	bool render = false;
	std::vector<std::pair<std::unique_ptr<char[]>, std::unique_ptr<char[]>>> snippets_;

	ImGuiEditor() = default;
	ImGuiEditor(const ImGuiEditor &) = delete;
	ImGuiEditor& operator=(ImGuiEditor &) = delete;

public:
	static ImGuiEditor& getInstance() {
		static ImGuiEditor instance;
		return instance;
	}

	void Init();
	void Render();

	std::vector<std::pair<std::unique_ptr<char[]>, std::unique_ptr<char[]>>>& getSnippets();

	bool& isRender();
	void setRender(bool render);
};
