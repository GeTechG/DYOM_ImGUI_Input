#pragma once
#include <memory>
#include <vector>

class ImGuiHook
{
private:
	bool render = false;
	std::vector<std::pair<std::unique_ptr<char[]>, std::unique_ptr<char[]>>> snippets_;

	ImGuiHook() = default;
	ImGuiHook(const ImGuiHook&) = delete;
	ImGuiHook& operator=(ImGuiHook&) = delete;
public:
	static ImGuiHook& getInstance() {
		static ImGuiHook instance;
		return instance;
	}

	void Init();
	void Destroy();

	std::vector<std::pair<std::unique_ptr<char[]>, std::unique_ptr<char[]>>>& getSnippets();

	bool& isRender();
	void setRender(bool render);
};

