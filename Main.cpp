#include "CCheat.h"
#include "plugin.h"
#include "ImGui/ImGuiEditor.h"

using namespace plugin;

#include <CTheScripts.h>
#include <filesystem>
#include "SimpleIni.h"

extern size_t strlcpy(char *dst, const char *src, size_t size);

void loadSnippets() {
	if (std::filesystem::exists("DYOM_ImGUI_Input.ini")) {
		CSimpleIniA ini;
		ini.LoadFile("DYOM_ImGUI_Input.ini");
		CSimpleIniA::TNamesDepend keys;
		ini.GetAllKeys("snippets", keys);
		for (const auto key : keys) {
			auto keyBuff = new char[16];
			strlcpy(keyBuff, key.pItem, 16);
			auto valueBuff = new char[64];
			strlcpy(valueBuff, ini.GetValue("snippets", key.pItem), 64);
			ImGuiEditor::getInstance().getSnippets().emplace_back(keyBuff, valueBuff);
		}
	}
}

void saveSnippets() {
	CSimpleIniA ini;
	for (auto &snippet : ImGuiEditor::getInstance().getSnippets()) {
		ini.SetValue("snippets", snippet.first.get(), snippet.second.get());
	}
	ini.SaveFile("DYOM_ImGUI_Input.ini");
}

class DYOM_ImGUI_Input {
public:
	DYOM_ImGUI_Input() {
		Events::initRwEvent += [] {
			ImGuiEditor::getInstance().Init();
		};
		Events::initGameEvent += [] {
			CTheScripts::ScriptSpace = *reinterpret_cast<char**>(0x44CA42 + 2);
			loadSnippets();
		};

		Events::gameProcessEvent += [] {
			static std::string cheatCode = "IMINPUT";
			std::ranges::reverse(cheatCode);
			if (!cheatCode.compare(0, cheatCode.size(), CCheat::m_CheatString, cheatCode.size())) {
				CCheat::m_CheatString[0] = '\0';
			}
		};
	}
} dyom;
