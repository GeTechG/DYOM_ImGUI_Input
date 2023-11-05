#include "ImGuiEditor.h"

#include <utility>
#include "plugin.h"

#include <cstring>
#include <d3d9.h>
#include "CHud.h"
#include "CMessages.h"
#include "CTheScripts.h"
#include "ImGuiHook.h"
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_internal.h"

using namespace plugin;

extern void saveSnippets();

bool is_utf8(const char *string) {
	if (!string)
		return false;

	auto bytes = (const unsigned char*)string;
	while (*bytes) {
		if ((bytes[0] == 0x09 || bytes[0] == 0x0A || bytes[0] == 0x0D ||
				(0x20 <= bytes[0] && bytes[0] <= 0x7E)
			)
		) {
			bytes += 1;
			continue;
		}

		if (( // non-overlong 2-byte
				(0xC2 <= bytes[0] && bytes[0] <= 0xDF) &&
				(0x80 <= bytes[1] && bytes[1] <= 0xBF)
			)
		) {
			bytes += 2;
			continue;
		}

		if (( // excluding overlongs
				bytes[0] == 0xE0 &&
				(0xA0 <= bytes[1] && bytes[1] <= 0xBF) &&
				(0x80 <= bytes[2] && bytes[2] <= 0xBF)
			) ||
			( // straight 3-byte
				((0xE1 <= bytes[0] && bytes[0] <= 0xEC) ||
					bytes[0] == 0xEE ||
					bytes[0] == 0xEF) &&
				(0x80 <= bytes[1] && bytes[1] <= 0xBF) &&
				(0x80 <= bytes[2] && bytes[2] <= 0xBF)
			) ||
			( // excluding surrogates
				bytes[0] == 0xED &&
				(0x80 <= bytes[1] && bytes[1] <= 0x9F) &&
				(0x80 <= bytes[2] && bytes[2] <= 0xBF)
			)
		) {
			bytes += 3;
			continue;
		}

		if (( // planes 1-3
				bytes[0] == 0xF0 &&
				(0x90 <= bytes[1] && bytes[1] <= 0xBF) &&
				(0x80 <= bytes[2] && bytes[2] <= 0xBF) &&
				(0x80 <= bytes[3] && bytes[3] <= 0xBF)
			) ||
			( // planes 4-15
				(0xF1 <= bytes[0] && bytes[0] <= 0xF3) &&
				(0x80 <= bytes[1] && bytes[1] <= 0xBF) &&
				(0x80 <= bytes[2] && bytes[2] <= 0xBF) &&
				(0x80 <= bytes[3] && bytes[3] <= 0xBF)
			) ||
			( // plane 16
				bytes[0] == 0xF4 &&
				(0x80 <= bytes[1] && bytes[1] <= 0x8F) &&
				(0x80 <= bytes[2] && bytes[2] <= 0xBF) &&
				(0x80 <= bytes[3] && bytes[3] <= 0xBF)
			)
		) {
			bytes += 4;
			continue;
		}

		return false;
	}

	return true;
}

std::string UTF8_to_CP1251(const std::string &utf8) {
	if (!utf8.empty()) {
		if (!is_utf8(utf8.c_str())) {
			return utf8;
		}
		int wchlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), utf8.size(), nullptr, 0);
		if (wchlen > 0 && wchlen != 0xFFFD) {
			std::vector<wchar_t> wbuf(wchlen);
			MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), utf8.size(), &wbuf[0], wchlen);
			std::vector<char> buf(wchlen);
			WideCharToMultiByte(1251, 0, &wbuf[0], wchlen, &buf[0], wchlen, nullptr, nullptr);

			return std::string(&buf[0], wchlen);
		}
	}
	return std::string();
}

std::string cp1251_to_utf8(const char *str) {
	std::string res;
	WCHAR *ures = nullptr;
	char *cres = nullptr;

	int result_u = MultiByteToWideChar(1251, 0, str, -1, nullptr, 0);
	if (result_u != 0) {
		ures = new WCHAR[result_u];
		if (MultiByteToWideChar(1251, 0, str, -1, ures, result_u)) {
			int result_c = WideCharToMultiByte(CP_UTF8, 0, ures, -1, nullptr, 0, nullptr, nullptr);
			if (result_c != 0) {
				cres = new char[result_c];
				if (WideCharToMultiByte(CP_UTF8, 0, ures, -1, cres, result_c, nullptr, nullptr)) {
					res = cres;
				}
			}
		}
	}

	delete[] ures;
	delete[] cres;

	return res;
}

constexpr char sym_ru[] = "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя";
constexpr char sym_sl[] = "AЂ‹‚ѓEE„€…†K‡–­OЊPCЏYЃX‰ЌЋЉђ‘’“”•a—ў™љee›џњќkћЇ®oЈpc¦yx ¤ҐЎ§Ё©Є«¬";

void GXTEncode(std::string &str, bool to_sl = true) {
	for (int i = 0; i < 66; i++) {
		std::string ru_utf8 = UTF8_to_CP1251(sym_ru);
		std::string sl_utf8 = UTF8_to_CP1251(sym_sl);
		if (to_sl)
			std::ranges::replace(str, ru_utf8[i], sl_utf8[i]);
		else
			std::ranges::replace(str, sl_utf8[i], ru_utf8[i]);
	}
}

std::string ReplaceAll(std::string str, const std::string &from, const std::string &to) {
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
	}
	return str;
}

size_t /* O - Length of string */
strlcpy(char *dst, /* O - Destination string */
        const char *src, /* I - Source string */
        size_t size) /* I - Size of destination string buffer */
{
	size_t srclen; /* Length of source string */


	/*
	 * Figure out how much room is needed...
	 */

	size--;

	srclen = strlen(src);

	/*
	 * Copy the appropriate amount...
	 */

	if (srclen > size)
		srclen = size;

	memcpy(dst, src, srclen);
	dst[srclen] = '\0';

	return (srclen);
}

size_t strlcat(char *destination, const char *source, size_t size) {
	size_t len;

	for (len = 0; len < size; len++) {
		if ('\0' == destination[len]) {
			break;
		}
	}

	return len + strlcpy(destination + len, source, size - len);
}

static char buffer[200] = "";

#include <cstring>


void setData() {
	char *adress = *reinterpret_cast<char**>(CTheScripts::ScriptSpace + 9889 * 4);
	if (adress != nullptr) {
		std::string text = UTF8_to_CP1251(buffer);
		text = ReplaceAll(text, "\n", "~n~");
		text = ReplaceAll(text, "+", "~");
		GXTEncode(text);
		strlcpy(adress, text.c_str(), 100);
		CHud::SetHelpMessage(adress, false, true, false);
	}
}


void checkSnippet(ImGuiInputTextCallbackData *data) {
	auto &hook = ImGuiEditor::getInstance();
	for (const auto &snippet : hook.getSnippets()) {
		const size_t lenSnippet = strlen(snippet.first.get());
		std::unique_ptr<char[]> eqSnipp(new char[16]);
		strcpy(eqSnipp.get(), buffer + data->CursorPos - lenSnippet);
		if (strcmp(snippet.first.get(), eqSnipp.get()) == 0) {
			data->DeleteChars(data->CursorPos - lenSnippet, lenSnippet);
			data->InsertChars(data->CursorPos, snippet.second.get());
			break;
		}
	}
}

inline void shortCut(ImGuiInputTextCallbackData *data) {
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_R, NULL, ImGuiInputFlags_RouteGlobalHigh)) {
		data->InsertChars(data->CursorPos, "+r+");
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_G, NULL, ImGuiInputFlags_RouteGlobalHigh)) {
		data->InsertChars(data->CursorPos, "+g+");
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_B, NULL, ImGuiInputFlags_RouteGlobalHigh)) {
		data->InsertChars(data->CursorPos, "+b+");
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, NULL, ImGuiInputFlags_RouteGlobalHigh)) {
		data->InsertChars(data->CursorPos, "+p+");
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, NULL, ImGuiInputFlags_RouteGlobalHigh)) {
		data->InsertChars(data->CursorPos, "\n");
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_L, NULL, ImGuiInputFlags_RouteGlobalHigh)) {
		data->InsertChars(data->CursorPos, "+l+");
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, NULL, ImGuiInputFlags_RouteGlobalHigh)) {
		data->InsertChars(data->CursorPos, "+y+");
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_W, NULL, ImGuiInputFlags_RouteGlobalHigh)) {
		data->InsertChars(data->CursorPos, "+w+");
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, NULL, ImGuiInputFlags_RouteGlobalHigh)) {
		data->InsertChars(data->CursorPos, "+s+");
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_H, NULL, ImGuiInputFlags_RouteGlobalHigh)) {
		data->InsertChars(data->CursorPos, "+h+");
	}

	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_M, NULL, ImGuiInputFlags_RouteGlobalHigh)) {
		checkSnippet(data);
	}
}

int inputCallback(ImGuiInputTextCallbackData *data) {
	shortCut(data);

	return 0;
}

void ImGuiEditor::Init() {
	ImGuiHook::Hook();
	ImGuiHook::preRenderCallback = [] {
		ImGuiHook::m_bShowMouse = getInstance().render;
	};
	ImGuiHook::windowCallback = [] {
		getInstance().Render();
	};
}

void ImGuiEditor::Render() {
	if (ImGui::IsKeyReleased(ImGuiKey_Home)) {
		getInstance().setRender(!getInstance().isRender());


		if (getInstance().isRender()) {
			char *adress = *reinterpret_cast<char**>(CTheScripts::ScriptSpace + 9889 * 4);
			if (adress != nullptr) {
				std::string str = adress;
				GXTEncode(str, false);
				std::string str2 = ReplaceAll(str, "+n+", "\n");
				strlcpy(buffer, cp1251_to_utf8(str2.c_str()).c_str(), 200);
			}
		}
	}

	const auto windowsSize = ImGui::GetIO().DisplaySize;

	ImGui::SetNextWindowPos(ImVec2(windowsSize.x, 0.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
	if (getInstance().isRender() && *reinterpret_cast<unsigned char*>(0xB7CB49) == FALSE) {
		if (ImGui::Begin("Text input", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (ImGui::BeginTabBar("##tabs")) {
				if (ImGui::BeginTabItem("Input")) {
					ImGui::Text("%d/100", UTF8_to_CP1251(buffer).size());
					auto fontSize = ImGui::GetFontSize();
					ImGui::InputTextMultiline("##textInput", buffer, 200, ImVec2(fontSize * 50.75f, fontSize * 8.75f),
					                          ImGuiInputTextFlags_CtrlEnterForNewLine +
					                          ImGuiInputTextFlags_CallbackAlways, inputCallback);

					if (ImGui::IsKeyReleased(ImGuiKey_Tab)) {
						setData();
					}
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Snippets")) {
					if (ImGui::Button("New")) {
						this->getSnippets().emplace_back(new char[16]{}, new char[64]{});
					}
					auto fontSize = ImGui::GetFontSize();
					if (ImGui::BeginChild("##snippetsInputs", ImVec2(fontSize * 50.75f, fontSize * 8.75f))) {
						for (int i = 0; i < this->getSnippets().size(); ++i) {
							ImGui::PushID(i);
							ImGui::SetNextItemWidth(fontSize * 10.25f);
							ImGui::InputTextWithHint("##snippetName", "snippet", this->getSnippets().at(i).first.get(),
							                         16);
							ImGui::SameLine();
							ImGui::SetNextItemWidth(fontSize * 40.41f);
							ImGui::InputTextWithHint("##snippetText", "text", this->getSnippets().at(i).second.get(),
							                         64,
							                         ImGuiInputTextFlags_CallbackAlways, inputCallback);
							ImGui::SameLine();
							if (ImGui::Button("X")) {
								this->getSnippets().erase(this->getSnippets().begin() + i);
								i = this->getSnippets().size();
							}
							ImGui::PopID();
						}
						ImGui::EndChild();
					}
					if (ImGui::Button("Save")) {
						saveSnippets();
						CMessages::AddMessage(const_cast<char*>("Snippets saved"), 1000, 1, true);
					}
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}


			if (ImGui::IsKeyReleased(ImGuiKey_Enter)) {
				getInstance().setRender(false);
			}
		}
		ImGui::End();
	}
}

std::vector<std::pair<std::unique_ptr<char[]>, std::unique_ptr<char[]>>>& ImGuiEditor::getSnippets() {
	return snippets_;
}

bool& ImGuiEditor::isRender() {
	return render;
}

void ImGuiEditor::setRender(bool render) {
	this->render = render;
}
